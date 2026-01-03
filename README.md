# whu-oslab-lab6

此分支为实验六:进程管理

# 测试代码
```
#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - 34 * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 8096 * PGSIZE) 

char *str1, *str2;

int main()
{
    syscall(SYS_print, "\nuser begin\n");

    // 测试MMAP区域
    str1 = (char*)syscall(SYS_mmap, MMAP_BEGIN, PGSIZE);
    
    // 测试HEAP区域
    long long top = syscall(SYS_brk, 0);
    str2 = (char*)top;
    syscall(SYS_brk, top + PGSIZE);

    str1[0] = 'M';
    str1[1] = 'M';
    str1[2] = 'A';
    str1[3] = 'P';
    str1[4] = '\n';
    str1[5] = '\0';

    str2[0] = 'H';
    str2[1] = 'E';
    str2[2] = 'A';
    str2[3] = 'P';
    str2[4] = '\n';
    str2[5] = '\0';

    int pid = syscall(SYS_fork);

    if(pid == 0) { // 子进程
        for(int i = 0; i < 100000000; i++);
        syscall(SYS_print, "child: hello\n");
        syscall(SYS_print, str1);
        syscall(SYS_print, str2);

        syscall(SYS_exit, 1);
        syscall(SYS_print, "child: never back\n");
    } else {       // 父进程
        int exit_state;
        syscall(SYS_wait, &exit_state);
        if(exit_state == 1)
            syscall(SYS_print, "parent: hello\n");
        else
            syscall(SYS_print, "parent: error\n");
    }

    while(1);
    return 0;
}
```

**把上述代码替换user目录下的initcode.c，在terminal下user目录下，执行：make init就可以直接替换initcode.h中的16进制代码**

# 测试结果
得到类似下面的输出
前面带[Debug]的是自己加的输出，**不带的是initcode.c程序对应的输出信息**
```
  xv6-riscv Lab6 - Process Management
========================================

[Debug] main: CPU 0 creating first user process
[proc] proczero created (pid=0), entry=0x0x0000000000001000, sp=0x0x0000003fffffe000
[Debug] main: CPU 0 entering scheduler
[Debug] main: CPU 1 initialized, entering scheduler
[Debug] scheduler: CPU 0 entering scheduler
[Debug] scheduler: CPU 1 entering scheduler
[Debug] scheduler: CPU 1 -> pid 0

user begin
[Debug] fork: pid 0 calling fork
[Debug] fork: pid 0 created child pid 2
[Debug] wait: pid 0 waiting for child
[Debug] wait: pid 0 sleeping, waiting for child to exit
[Debug] scheduler: CPU 1 -> pid 2
[Debug] scheduler: CPU 0 -> pid 2
child: hello
MMAP
HEAP
[Debug] exit: pid 2 exiting with state 1
[Debug] exit: pid 2 waking up parent pid 0
[Debug] exit: pid 2 becoming ZOMBIE
[Debug] scheduler: CPU 1 -> pid 0
[Debug] wait: pid 0 woken up
[Debug] wait: pid 0 found zombie child pid 2, exit_state=1
parent: hello
[Debug] scheduler: CPU 0 -> pid 0
```