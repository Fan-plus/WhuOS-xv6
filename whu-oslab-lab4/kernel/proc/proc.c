#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "trap/trap.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();

// 内核页表
extern pgtbl_t kernel_pagetable;

// 第一个进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t pgtbl;

    // 分配顶级页表
    pgtbl = (pgtbl_t)pmem_alloc(false);
    if (pgtbl == NULL) {
        panic("proc_pgtbl_init: failed to allocate page table");
    }
    memset(pgtbl, 0, PGSIZE);

    // 映射跳板页（和内核页表共享同一虚拟地址和物理页）
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 映射trapframe页
    vm_mappages(pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_fisrt()
{
    uint64 page;
    
    printf("[proc] Allocating process structure for proczero (pid=0)...\n");
    
    // pid 设置
    proczero.pid = 0;

    // 分配trapframe物理页
    page = (uint64)pmem_alloc(false);
    if (page == 0) {
        panic("proc_make_fisrt: failed to allocate trapframe");
    }
    memset((void*)page, 0, PGSIZE);
    proczero.tf = (trapframe_t*)page;
    printf("[proc] Trapframe allocated at 0x%p\n", page);

    // pagetable 初始化
    proczero.pgtbl = proc_pgtbl_init(page);
    printf("[proc] User page table created at 0x%p\n", proczero.pgtbl);

    // ustack 映射 + 设置 ustack_pages 
    page = (uint64)pmem_alloc(false);
    if (page == 0) {
        panic("proc_make_fisrt: failed to allocate user stack");
    }
    memset((void*)page, 0, PGSIZE);
    proczero.ustack_pages = 1;
    // 用户栈在 TRAPFRAME 下方
    uint64 ustack_va = TRAPFRAME - PGSIZE;
    vm_mappages(proczero.pgtbl, ustack_va, page, PGSIZE, PTE_R | PTE_W | PTE_U);
    printf("[proc] User stack mapped at VA 0x%p\n", ustack_va);

    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page = (uint64)pmem_alloc(false);
    if (page == 0) {
        panic("proc_make_fisrt: failed to allocate code page");
    }
    memset((void*)page, 0, PGSIZE);
    // 复制initcode到物理页
    memmove((void*)page, initcode, initcode_len);
    // 代码段在虚拟地址 PGSIZE (跳过最低的空白页)
    vm_mappages(proczero.pgtbl, PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    printf("[proc] User code (%d bytes) loaded at VA 0x%p\n", initcode_len, PGSIZE);

    // 设置 heap_top
    proczero.heap_top = 2 * PGSIZE;  // 代码段之后

    // tf字段设置
    proczero.tf->epc = PGSIZE;                     // 用户入口点（代码起始地址）
    proczero.tf->sp = ustack_va + PGSIZE;          // 用户栈顶（栈向下生长）
    proczero.tf->kernel_satp = r_satp();           // 内核页表
    proczero.tf->kernel_sp = KSTACK(0) + PGSIZE;   // 内核栈顶
    proczero.tf->kernel_trap = (uint64)trap_user_handler;
    proczero.tf->kernel_hartid = r_tp();

    // 内核字段设置
    proczero.kstack = KSTACK(0);
    proczero.ctx.ra = (uint64)trap_user_return;    // 上下文切换后跳转到trap_user_return
    proczero.ctx.sp = KSTACK(0) + PGSIZE;          // 内核栈顶

    // 设置CPU当前运行的进程
    mycpu()->proc = &proczero;
    
    printf("[proc] Process proczero created successfully!\n");
    printf("[proc] Entry point: 0x%p, Stack pointer: 0x%p\n", proczero.tf->epc, proczero.tf->sp);
    printf("\n Switching from kernel to user mode...\n");
    printf("CPU 0 will now run in user process proczero\n\n");

    // 上下文切换（从CPU上下文切换到进程上下文）
    swtch(&mycpu()->ctx, &proczero.ctx);
}
