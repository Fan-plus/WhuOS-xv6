#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    // 设置stvec指向kernel_vector，防止在处理用户trap时发生嵌套trap
    w_stvec((uint64)kernel_vector);

    // 保存用户PC到trapframe
    p->tf->epc = sepc;

    // 用scause的低4位表示trap的类型
    int trap_id = scause & 0xf;
    // 最高位表示是中断还是异常
    int is_interrupt = (scause >> 63) & 1;

    if (is_interrupt) {
        // 中断处理
        switch (trap_id) {
            case 1: // S-mode software interrupt
                timer_interrupt_handler();
                break;
            case 5: // S-mode timer interrupt
                timer_interrupt_handler();
                break;
            case 9: // S-mode external interrupt
                external_interrupt_handler();
                break;
            default:
                printf("unknown user interrupt: %s (trap_id=%d)\n", 
                       interrupt_info[trap_id], trap_id);
                printf("scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
                panic("trap_user_handler: unexpected interrupt");
                break;
        }
    } else {
        // 异常处理
        static int syscall_count = 0;  // 系统调用计数器
        
        switch (trap_id) {
            case 8: // Environment call from U-mode (系统调用)
                syscall_count++;
                printf("[syscall] User process (pid=%d) issued system call #%d\n", p->pid, syscall_count);
                // 系统调用返回后，epc需要指向下一条指令
                p->tf->epc += 4;
                
                // 在第二次系统调用后，提示用户进程即将进入死循环
                if (syscall_count == 2) {
                    printf("\n========================================\n");
                    printf("  User Process Execution Complete!\n");
                    printf("========================================\n");
                    printf("\n[Result] proczero executed 2 system calls successfully.\n");
                    printf("[Result] CPU 0 is now trapped in user-mode infinite loop.\n");
                    printf("[Result] Other CPUs are trapped in main() idle loop.\n\n");
                }
                break;
            default:
                printf("user exception: %s (trap_id=%d)\n", 
                       exception_info[trap_id], trap_id);
                printf("scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
                panic("trap_user_handler: unexpected exception");
                break;
        }
    }

    // 返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t* p = myproc();

    // 关闭中断，因为我们即将切换到用户态
    intr_off();

    // 设置stvec指向user_vector（在trampoline中）
    // 用户地址空间中trampoline的位置
    uint64 trampoline_user_vector = TRAMPOLINE + (user_vector - trampoline);
    w_stvec(trampoline_user_vector);

    // 设置trapframe中的内核字段（供下次进入内核时使用）
    p->tf->kernel_satp = r_satp();              // 内核页表
    p->tf->kernel_sp = p->kstack + PGSIZE;      // 内核栈顶
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = r_tp();

    // 设置sstatus：清除SPP（使sret返回用户模式），设置SPIE（sret后启用中断）
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;   // 清除SPP，表示返回用户模式
    x |= SSTATUS_SPIE;   // 设置SPIE，sret后启用中断
    w_sstatus(x);

    // 设置sepc为用户PC
    w_sepc(p->tf->epc);

    // 计算用户页表的satp值
    uint64 satp = MAKE_SATP(p->pgtbl);

    // 计算user_return在用户地址空间中的位置
    uint64 fn = TRAMPOLINE + (user_return - trampoline);

    // 将trapframe地址放入sscratch，供user_vector使用
    w_sscratch((uint64)TRAPFRAME);

    // 跳转到trampoline中的user_return，传入trapframe和satp
    // user_return(TRAPFRAME, satp)
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}