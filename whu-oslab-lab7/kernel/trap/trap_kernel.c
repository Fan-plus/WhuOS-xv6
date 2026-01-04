#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "dev/vio.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    // 初始化系统时钟
    timer_create();
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    // 设置stvec寄存器指向kernel_vector
    // 所有S-mode的陷阱都会跳转到kernel_vector
    w_stvec((uint64)kernel_vector);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    // 从PLIC获取中断请求号
    int irq = plic_claim();
    
    if (irq == UART_IRQ) {
        // UART中断处理
        uart_intr();
    } else if (irq == VIRTIO_IRQ) {
        // VIRTIO磁盘中断处理
        virtio_disk_intr();
    } else if (irq != 0) {
        // 未知的外部中断
        printf("unexpected external interrupt irq=%d\n", irq);
    }
    // irq为0时直接忽略
    
    // 告知PLIC中断处理完成
    if (irq) {
        plic_complete(irq);
    }
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // 只有CPU 0更新全局时钟,防止 tricks被多个 CPU同时增加，实际上每个 CPU都会收到 时钟中断，但只需要一个 CPU负责计时
    if (mycpuid() == 0) {
        timer_update();
    }
    
    // 清除软件中断标志
    // 通过清除sip中的SSIP位来确认软件中断，如果不清除，CPU会认为中断还在挂起，处理完后立即再次中断，形成死循环
    w_sip(r_sip() & ~2);
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态,处理中断时应该禁用中断
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    // 用scause的低4位表示trap的类型
    int trap_id = scause & 0xf;
    // 最高位表示是中断还是异常
    int is_interrupt = (scause >> 63) & 1;

    // 中断异常处理核心逻辑
    if (is_interrupt) {
        // 中断处理
        switch (trap_id) {
            case 1: // S-mode software interrupt (由M-mode定时器中断触发)
                timer_interrupt_handler();
                // 如果有运行中的进程，触发调度
                if (myproc() != NULL && myproc()->state == RUNNING) {
                    proc_yield();
                }
                break;
            case 5: // S-mode timer interrupt
                timer_interrupt_handler();
                // 如果有运行中的进程，触发调度
                if (myproc() != NULL && myproc()->state == RUNNING) {
                    proc_yield();
                }
                break;
            case 9: // S-mode external interrupt
                external_interrupt_handler();
                break;
            default:
                printf("unknown interrupt: %s (trap_id=%d)\n", 
                       interrupt_info[trap_id], trap_id);
                printf("scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
                panic("trap_kernel_handler: unexpected interrupt");
                break;
        }
    } else {
        // 异常处理 - 目前不处理任何异常，直接报错
        printf("kernel exception: %s (trap_id=%d)\n", 
               exception_info[trap_id], trap_id);
        printf("scause=%p sepc=%p stval=%p\n", scause, sepc, stval);
        panic("trap_kernel_handler: unexpected exception");
    }
    
    // 恢复sepc和sstatus（可能在处理过程中被修改）
    w_sepc(sepc);
    w_sstatus(sstatus);
}