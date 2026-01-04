# WhuOS-xv6
Operating System Practice

# 介绍目录
 
- whu-oslab-lab 目录是最后的代码项目，与分支Lab7的代码是一致的
- 每个分支下的Doc目录是该实验所用到的文档
- xv6-riscv 是用于参考的xv6原项目
- oslab-main 是加了中文注释，重构了一些目录结构的xv6原项目

此二者是主要的参考项目

whu-oslab-lab的目录结构：
```
.
├── common.mk
├── include
│   ├── common.h
│   ├── dev
│   │   ├── plic.h
│   │   ├── timer.h
│   │   ├── uart.h
│   │   ├── vio.h
│   │   └── virtio.h
│   ├── fs
│   │   ├── bitmap.h
│   │   ├── buf.h
│   │   ├── dir.h
│   │   ├── file.h
│   │   ├── fs.h
│   │   └── inode.h
│   ├── lib
│   │   ├── lock.h
│   │   ├── print.h
│   │   └── str.h
│   ├── mem
│   │   ├── mmap.h
│   │   ├── pmem.h
│   │   └── vmem.h
│   ├── memlayout.h
│   ├── proc
│   │   ├── cpu.h
│   │   ├── initcode.h
│   │   └── proc.h
│   ├── riscv.h
│   ├── syscall
│   │   ├── syscall.h
│   │   ├── sysfunc.h
│   │   └── sysnum.h
│   └── trap
│       └── trap.h
├── kernel
│   ├── boot
│   │   ├── entry.S
│   │   ├── main.c
│   │   ├── Makefile
│   │   └── start.c
│   ├── dev
│   │   ├── Makefile
│   │   ├── plic.c
│   │   ├── timer.c
│   │   ├── uart.c
│   │   └── virtio.c
│   ├── fs
│   │   ├── bitmap.c
│   │   ├── buf.c
│   │   ├── dir.c
│   │   ├── file.c
│   │   ├── fs.c
│   │   ├── inode.c
│   │   └── Makefile
│   ├── kernel.ld
│   ├── lib
│   │   ├── Makefile
│   │   ├── print.c
│   │   ├── sleeplock.c
│   │   ├── spinlock.c
│   │   └── str.c
│   ├── Makefile
│   ├── mem
│   │   ├── kvm.c
│   │   ├── Makefile
│   │   ├── mmap.c
│   │   ├── pmem.c
│   │   └── uvm.c
│   ├── proc
│   │   ├── cpu.c
│   │   ├── Makefile
│   │   ├── proc.c
│   │   └── switch.S
│   ├── syscall
│   │   ├── Makefile
│   │   ├── syscall.c
│   │   ├── sysfile.c
│   │   └── sysfunc.c
│   └── trap
│       ├── Makefile
│       ├── trampoline.S
│       ├── trap_kernel.c
│       ├── trap.S
│       └── trap_user.c
├── LICENSE
├── Makefile
├── mkfs
│   ├── Makefile
│   └── mkfs.c
├── test
│   ├── 路径测试.md
│   ├── 目录测试.md
│   └── inode读写测试.md
└── user
    ├── initcode.c
    ├── Makefile
    ├── syscall_arch.h
    ├── syscall_num.h
    ├── sys.h
    ├── _test
    ├── test.c
    ├── type.h
    ├── user.ld
    ├── user_lib.c
    ├── userlib.h
    └── user_syscall.c
```


# 测试

每个实验的测试结果在对应分支的 README.md 或者 实验报告中有所体现


