#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0 进行初始化
        print_init();
        pmem_init();
        
        // 初始化陷阱处理
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化PLIC
        plic_init();
        plic_inithart();
        
        // 初始化UART（必须在PLIC之后，启用UART中断）
        uart_init();
        
        printf("cpu %d is booting!\n", cpuid);
        
        // 开启S-mode中断
        intr_on();
        
        printf("interrupts enabled, waiting for timer ticks...\n");
        
        __sync_synchronize();
        started = 1;
        
        // 简单测试：等待并打印ticks
        uint64 last_ticks = 0;
        while(1) {
            uint64 current_ticks = timer_get_ticks();
            if (current_ticks != last_ticks && current_ticks % 100 == 0) {
                printf("\nticks = %d\n", current_ticks);
                last_ticks = current_ticks;
            }
        }
        
    } else {
        // 其他CPU等待CPU 0初始化完成
        while(started == 0);
        __sync_synchronize();
        
        // 每个hart初始化自己的陷阱处理和PLIC
        trap_kernel_inithart();
        plic_inithart();
        
        printf("cpu %d is booting!\n", cpuid);
        
        // 开启S-mode中断
        intr_on();
        
        while(1);
    }
    
    return 0;
}