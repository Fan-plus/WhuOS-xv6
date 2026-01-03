#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "memlayout.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "dev/timer.h"

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

// 打印字符串
// uint64 addr  字符串地址
uint64 sys_print()
{
    char buf[128];
    arg_str(0, buf, 128);
    printf("%s", buf);
    return 0;
}

// 进程复制
uint64 sys_fork()
{
    return proc_fork();
}

// 进程等待
// uint64 addr  子进程退出时的exit_state需要放到这里 
uint64 sys_wait()
{
    uint64 addr;
    arg_uint64(0, &addr);
    return proc_wait(addr);
}

// 进程退出
// int exit_state
uint64 sys_exit()
{
    int exit_state;
    arg_uint32(0, (uint32*)&exit_state);
    proc_exit(exit_state);
    return 0;  // 实际上不会执行到这里
}

extern timer_t sys_timer;

// 进程睡眠一段时间
// uint32 ticks 睡眠时间(以ticks为单位)
// 成功返回0, 失败返回-1
uint64 sys_sleep()
{
    uint32 n;
    arg_uint32(0, &n);
    
    spinlock_acquire(&sys_timer.lk);
    uint64 ticks0 = sys_timer.ticks;
    
    while (sys_timer.ticks - ticks0 < n) {
        proc_sleep(&sys_timer.ticks, &sys_timer.lk);
    }
    
    spinlock_release(&sys_timer.lk);
    return 0;
}

// 执行一个ELF文件
// char* path
// char** argv
// 成功返回argc 失败返回-1
uint64 sys_exec()
{
    // TODO: 实现ELF加载器
    return -1;
}