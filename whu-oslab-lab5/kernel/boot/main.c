#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "proc/proc.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0 进行初始化
        print_init();
        printf("\n");
        printf("  xv6-riscv Lab5\n");
        printf("========================================\n\n");
        
        printf("Physical memory initialization...\n");
        pmem_init();
        printf("Done.\n\n");
        
        printf("Kernel virtual memory initialization...\n");
        kvm_init();
        printf("Kernel page table created.\n\n");
        
        printf("Trap handler initialization...\n");
        trap_kernel_init();
        trap_kernel_inithart();
        printf("Done.\n\n");
        
        printf("Activating kernel page table...\n");
        kvm_inithart();
        printf("Paging enabled.\n\n");
        
        printf("PLIC and UART initialization...\n");
        plic_init();
        plic_inithart();
        uart_init();
        printf("Done.\n\n");
        
        printf("Enabling S-mode interrupts...\n");
        intr_on();
        printf("Interrupts enabled.\n\n");
        
        printf("CPU %d initialization complete!\n\n", cpuid);
        
        __sync_synchronize();
        started = 1;
        
        printf("Creating First User Process (proczero)\n");
        printf("========================================\n\n");
        
        // 创建并切换到第一个用户进程
        proc_make_fisrt();
        
        // 如果用户进程返回内核（不应该发生）
        printf("CPU %d: User process returned to kernel, entering idle loop...\n", cpuid);
        while(1);
        
    } else {
        // 其他CPU等待CPU 0初始化完成
        while(started == 0);
        __sync_synchronize();
        
        // 激活内核页表
        kvm_inithart();
        
        // 每个hart初始化自己的陷阱处理和PLIC
        trap_kernel_inithart();
        plic_inithart();
        
        // 开启S-mode中断
        intr_on();
        
        printf("CPU %d: Initialization complete, entering idle loop in main()...\n", cpuid);
        
        while(1);
    }
    
    return 0;
}