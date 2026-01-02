# whu-oslab-lab4

此分支为实验四： 首个用户态进程的创建

本实验的主要目的是实现不同特权级之间的切换，并且创建首个用户态进程，进行用户态`trap`的处理

运行结果大致如下：

```
  xv6-riscv Lab4: First User Process
========================================

Physical memory initialization...
pmem: kern_region [0x0000000080007000 - 0x0000000080407000], 1024 pages
pmem: user_region [0x0000000080407000 - 0x0000000088000000], 31737 pages
Done.

Kernel virtual memory initialization...
Kernel page table created.

Trap handler initialization...
Done.

Activating kernel page table...
Paging enabled.

PLIC and UART initialization...
Done.

Enabling S-mode interrupts...
Interrupts enabled.

CPU 0 initialization complete!

Creating First User Process (proczero)
========================================

CPU 1: Initialization complete, entering idle loop in main()...
[proc] Allocating process structure for proczero (pid=0)...
[proc] Trapframe allocated at 0x0x0000000087fb4000
[proc] User page table created at 0x0x0000000087fb3000
[proc] User stack mapped at VA 0x0x0000003fffffd000
[proc] User code (28 bytes) loaded at VA 0x0x0000000000001000
[proc] Process proczero created successfully!
[proc] Entry point: 0x0x0000000000001000, Stack pointer: 0x0x0000003fffffe000

 Switching from kernel to user mode...
CPU 0 will now run in user process proczero

[syscall] User process (pid=0) issued system call #1
[syscall] User process (pid=0) issued system call #2

========================================
  User Process Execution Complete!
========================================

[Result] proczero executed 2 system calls successfully.
[Result] CPU 0 is now trapped in user-mode infinite loop.
[Result] Other CPUs are trapped in main() idle loop.
```