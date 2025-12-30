/*
 * pmem.c - 物理内存分配器
 * 
 * 采用空闲链表管理物理页面，支持多核并发分配
 * 将内核和用户物理页分开管理，防止用户程序耗尽内核内存
 */

#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "riscv.h"
#include "memlayout.h"

/*
 * ======== 数据结构定义 ========
 * 
 * 物理页链表节点：
 * 我们不需要额外的元数据来管理空闲页，而是直接利用空闲页本身的前8字节
 * 来存储指向下一个空闲页的指针。这样做的好处是零额外内存开销。
 * 
 * 示意图：
 *   +----------------+    +----------------+    +----------------+
 *   | next 指针 (8B) |    | next 指针 (8B) |    | next 指针 (8B) |
 *   | (剩余 4088B)   | -> | (剩余 4088B)   | -> | (剩余 4088B)   | -> NULL
 *   +----------------+    +----------------+    +----------------+
 *        page A               page B               page C
 */
typedef struct page_node {
    struct page_node* next;  // 指向下一个空闲页的指针
} page_node_t;

/*
 * 内存分配区域：
 * 包含该区域的起止地址、保护锁、可用页计数和空闲页链表头
 * 
 * 为什么要分开内核和用户区域？
 * 如果恶意用户程序不断申请内存不释放，可能耗尽所有物理页，
 * 导致内核无法工作。分开管理后，用户耗尽自己的配额不会影响内核。
 */
typedef struct alloc_region {
    uint64 begin;             // 区域起始物理地址
    uint64 end;               // 区域终止物理地址
    spinlock_t lk;            // 自旋锁，保护并发访问
    uint32 allocable;         // 当前可分配的页面数量
    page_node_t list_head;    // 链表头节点（本身不代表任何页，只是链表入口）
} alloc_region_t;

// 内核区域和用户区域分开管理
static alloc_region_t kern_region;  // 内核专用
static alloc_region_t user_region;  // 用户程序专用

/*
 * ======== 工具函数 ========
 */

// memset：将内存区域的每个字节设置为指定值
//模拟标准库实现，c赋值给char的时候自动截断，只保留最低8位
//memset本身不分配内存，只是填充已经分配的内存
void* memset(void *dst, int c, uint64 n)
{
    char *cdst = (char *)dst;
    for (uint64 i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

/*
 * region_init - 初始化一个内存区域
 * 
 * @region: 要初始化的区域结构体
 * @name: 区域名称（用于调试）
 * @start: 起始地址
 * @end: 结束地址
 * 
 * 这个函数会：
 * 1. 设置区域的起止地址
 * 2. 初始化自旋锁
 * 3. 将区域内所有物理页加入空闲链表
 */
static void region_init(alloc_region_t *region, char *name, void *start, void *end)
{
    region->begin = (uint64)start;
    region->end = (uint64)end;
    region->allocable = 0;
    region->list_head.next = NULL;  // 链表初始为空
    
    spinlock_init(&region->lk, name);
    
    // 将起始地址向上对齐到页边界（4KB对齐）
    // 例如：0x80001234 向上对齐到 0x80002000
    char *p = (char*)PG_ROUND_UP((uint64)start);
    
    // 遍历所有完整的物理页，加入空闲链表
    for (; p + PGSIZE <= (char*)end; p += PGSIZE) {
        // 使用"头插法"将页面加入链表
        // 头插法：新节点插入链表头部，效率O(1)
        page_node_t *node = (page_node_t*)p;
        node->next = region->list_head.next;
        region->list_head.next = node;
        region->allocable++;
    }
}

/*
 * ======== 公开接口 ========
 */

/*
 * pmem_init - 初始化物理内存分配器
 * 
 * 内存布局：
 *   ALLOC_BEGIN                                           ALLOC_END
 *   |<-------- 内核区域 -------->|<-------- 用户区域 -------->|
 *   |     KERNEL_PAGES 个页      |          剩余页           |
 *   
 * 只在系统启动时由CPU 0调用一次
 */
void pmem_init(void)
{
    // 计算内核区域的结束地址
    uint64 kern_end = (uint64)ALLOC_BEGIN + KERNEL_PAGES * PGSIZE;
    
    // 确保内核区域不超过可用内存
    if (kern_end > (uint64)ALLOC_END) {
        kern_end = (uint64)ALLOC_END;
    }
    
    // 初始化内核区域
    region_init(&kern_region, "kern_pmem", ALLOC_BEGIN, (void*)kern_end);
    
    // 初始化用户区域（从内核区域结束到ALLOC_END）
    region_init(&user_region, "user_pmem", (void*)kern_end, ALLOC_END);
    
    printf("pmem: kern_region [%p - %p], %d pages\n", 
           kern_region.begin, kern_region.end, kern_region.allocable);
    printf("pmem: user_region [%p - %p], %d pages\n", 
           user_region.begin, user_region.end, user_region.allocable);
}

/*
 * pmem_alloc - 分配一个物理页
 * 
 * @in_kernel: true表示从内核区域分配，false表示从用户区域分配
 * @return: 分配的页地址，失败返回NULL
 * 
 * 分配过程（头移除）：
 *   分配前：list_head -> [page A] -> [page B] -> ...
 *   分配后：list_head -> [page B] -> ...
 *   返回：page A
 */
void* pmem_alloc(bool in_kernel)
{
    // 根据参数选择对应的区域
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    
    // 获取锁，保护链表操作
    spinlock_acquire(&region->lk);
    
    // 从链表头取出一个空闲页
    page_node_t *page = region->list_head.next;
    
    if (page != NULL) {
        // 将链表头指向下一个节点（移除当前节点）
        region->list_head.next = page->next;
        region->allocable--;
    }
    
    spinlock_release(&region->lk);
    
    if (page != NULL) {
        // 将分配的页面清零，避免信息泄露和方便使用
        memset(page, 0, PGSIZE);
    }
    
    return (void*)page;
}

/*
 * pmem_free - 释放一个物理页
 * 
 * @page: 要释放的页地址
 * @in_kernel: true表示释放到内核区域，false表示释放到用户区域
 * 
 * 释放过程（头插入）：
 *   释放前：list_head -> [page B] -> [page C] -> ...
 *   释放后：list_head -> [page A] -> [page B] -> [page C] -> ...
 */
void pmem_free(uint64 page, bool in_kernel)
{
    // 根据参数选择对应的区域
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    
    // 安全检查：确保地址是页对齐的（低12位必须为0）
    if ((page % PGSIZE) != 0) {
        panic("pmem_free: page not aligned");
    }
    
    // 安全检查：确保地址在对应区域范围内
    if (page < region->begin || page >= region->end) {
        panic("pmem_free: page out of region bounds");
    }
    
    // 填充垃圾数据（0x01重复），帮助检测释放后继续使用的bug
    memset((void*)page, 1, PGSIZE);
    
    // 获取锁
    spinlock_acquire(&region->lk);
    
    // 使用头插法将页面加回链表
    page_node_t *node = (page_node_t*)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    
    spinlock_release(&region->lk);
}
