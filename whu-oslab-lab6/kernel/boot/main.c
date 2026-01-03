#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
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
        printf("  xv6-riscv Lab6 - Process Management\n");
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

        printf("MMAP region pool initialization...\n");
        mmap_init();
        printf("Done.\n\n");
        
        printf("Process module initialization...\n");
        proc_init();
        printf("Done.\n\n");
        
        printf("Enabling S-mode interrupts...\n");
        intr_on();
        printf("Interrupts enabled.\n\n");
        
        printf("CPU %d initialization complete!\n\n", cpuid);
        
        __sync_synchronize();
        started = 1;
        
        printf("Creating First User Process (proczero)\n");
        printf("========================================\n\n");
        
        // 创建第一个用户进程
        proc_make_first();
        
        printf("Entering scheduler on CPU %d...\n\n", cpuid);
        
        // 进入调度器，永不返回
        proc_scheduler();
        
        // 不应该到达这里
        panic("scheduler returned");
        
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
        
        printf("CPU %d: Initialization complete, entering scheduler...\n", cpuid);
        
        // 进入调度器
        proc_scheduler();
        
        // 不应该到达这里
        panic("scheduler returned");
    }
    
    return 0;
}