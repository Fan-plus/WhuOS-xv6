#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "memlayout.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    proc_t* p = myproc();
    uint64 new_heap_top;
    
    // 获取参数：新堆顶地址
    arg_uint64(0, &new_heap_top);
    
    // 如果参数为0，返回当前堆顶（查询模式）
    if (new_heap_top == 0) {
        return p->heap_top;
    }
    
    // 边界检查：新堆顶不能低于初始堆位置（代码段之后）
    if (new_heap_top < 2 * PGSIZE) {
        return -1;
    }
    
    // 边界检查：新堆顶不能太高（与用户栈冲突）
    // 用户栈在 TRAPFRAME 下方，这里预留一些空间
    uint64 stack_bottom = TRAPFRAME - p->ustack_pages * PGSIZE - PGSIZE;
    if (new_heap_top > stack_bottom) {
        return -1;
    }
    
    uint64 old_heap_top = p->heap_top;
    
    if (new_heap_top > old_heap_top) {
        // 堆增长
        uint32 len = new_heap_top - old_heap_top;
        p->heap_top = uvm_heap_grow(p->pgtbl, old_heap_top, len);
    } else if (new_heap_top < old_heap_top) {
        // 堆收缩
        uint32 len = old_heap_top - new_heap_top;
        p->heap_top = uvm_heap_ungrow(p->pgtbl, old_heap_top, len);
    }
    // 如果相等，不做任何操作
    
    return p->heap_top;
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    
    // 获取参数
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查长度是否页对齐
    if (len == 0 || len % PGSIZE != 0) {
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 如果 start 为 0，需要在 mmap 链表中查找合适的空闲区域
    if (start == 0) {
        mmap_region_t* curr = p->mmap;
        while (curr != NULL) {
            if (curr->npages >= npages) {
                // 找到足够大的空闲区域
                start = curr->begin;
                break;
            }
            curr = curr->next;
        }
        if (start == 0) {
            // 没有找到合适的区域
            return -1;
        }
    } else {
        // 检查 start 是否页对齐
        if (start % PGSIZE != 0) {
            return -1;
        }
    }
    
    // 调用 uvm_mmap 建立映射
    uvm_mmap(start, npages, PTE_R | PTE_W);
    
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    uint64 start;
    uint32 len;
    
    // 获取参数
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查参数是否有效
    if (start % PGSIZE != 0 || len == 0 || len % PGSIZE != 0) {
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 调用 uvm_munmap 解除映射
    uvm_munmap(start, npages);
    
    return 0;
}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}
