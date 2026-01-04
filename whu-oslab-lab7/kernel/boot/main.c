#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "dev/vio.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "fs/fs.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0 进行初始化
        print_init();
        pmem_init();
        kvm_init();
        trap_kernel_init();
        trap_kernel_inithart();
        kvm_inithart();
        plic_init();
        plic_inithart();
        uart_init();
        virtio_disk_init();
        mmap_init();
        proc_init();
        intr_on();
        
        printf("\n");
        printf("  xv6-riscv Lab7 - File System Test\n");
        printf("=====================================\n");
        
        printf("[main] calling virtio_disk_init...\n");
        virtio_disk_init();
        printf("[main] virtio_disk_init done\n");
        
        printf("[main] calling fs_init...\n");
        // 文件系统初始化 + 测试
        fs_init();
        
        // fs_init中会阻塞在while(1)，不会到达下面
        panic("fs_init returned");
    }
    
    return 0;
}