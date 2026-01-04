#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/str.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// 将buf包装成双向循环链表的node
typedef struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
} buf_node_t;

// buf cache
static buf_node_t buf_cache[N_BLOCK_BUF];
static buf_node_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_node_t* buf_node, bool head_next)
{
    // 离开
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入
    if(head_next) { // 插入 head->next
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化
void buf_init()
{
    spinlock_init(&lk_buf_cache, "buf_cache");

    // 初始化头节点为双向循环链表
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;

    // 将所有buf加入链表（初始都在head.prev侧，表示可分配）
    for(int i = 0; i < N_BLOCK_BUF; i++) {
        buf_cache[i].buf.block_num = BLOCK_NUM_UNUSED;
        buf_cache[i].buf.buf_ref = 0;
        sleeplock_init(&buf_cache[i].buf.slk, "buf");
        // 插入到头节点的prev侧（可分配端）
        insert_head(&buf_cache[i], false);
    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 block_num)
{
    buf_node_t* bn;

    spinlock_acquire(&lk_buf_cache);

    // 查找已缓存的block
    for(bn = head_buf.next; bn != &head_buf; bn = bn->next) {
        if(bn->buf.block_num == block_num) {
            bn->buf.buf_ref++;
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&bn->buf.slk);
            return &bn->buf;
        }
    }

    // 未缓存，从链表尾端（LRU）找空闲buf
    for(bn = head_buf.prev; bn != &head_buf; bn = bn->prev) {
        if(bn->buf.buf_ref == 0) {
            bn->buf.block_num = block_num;
            bn->buf.buf_ref = 1;
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&bn->buf.slk);
            // 从磁盘读取
            virtio_disk_rw(&bn->buf, false);
            return &bn->buf;
        }
    }

    panic("buf_read: no free buf");
    return NULL;
}

// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    assert(sleeplock_holding(&buf->slk), "buf_write: not holding lock");
    virtio_disk_rw(buf, true);
}

// buf 释放
void buf_release(buf_t* buf)
{
    assert(sleeplock_holding(&buf->slk), "buf_release: not holding lock");

    sleeplock_release(&buf->slk);

    // 获取buf_node_t指针（buf_t是buf_node_t的第一个成员）
    buf_node_t* bn = (buf_node_t*)buf;

    spinlock_acquire(&lk_buf_cache);
    bn->buf.buf_ref--;
    if(bn->buf.buf_ref == 0) {
        // LRU: 移到链表头部（最近使用）
        insert_head(bn, true);
    }
    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_node_t* buf = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    while(buf != &head_buf)
    {
        buf_t* b = &buf->buf;
        printf("buf %d: ref = %d, block_num = %d\n", (int)(buf-buf_cache), b->buf_ref, b->block_num);
        for(int i = 0; i < 8; i++)
            printf("%d ",b->data[i]);
        printf("\n");
        buf = buf->next;
    }
    spinlock_release(&lk_buf_cache);
}