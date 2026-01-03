# whu-oslab-lab5

此分支为实验五： 用户态虚拟内存管理和系统调用

## 测试代码
```
// in initcode.c
#include "sys.h"

int main()
{
    long long heap_top = syscall(SYS_brk, 0);

    heap_top = syscall(SYS_brk, heap_top + 4096 * 10);

    heap_top = syscall(SYS_brk, heap_top - 4096 * 5);

    while(1);
    return 0;
}
```

```
unsigned char initcode[] = {
  0x13, 0x01, 0x01, 0xff, 0x23, 0x34, 0x81, 0x00, 0x13, 0x04, 0x01, 0x01,
  0x93, 0x08, 0x10, 0x00, 0x13, 0x05, 0x00, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xb7, 0xa7, 0x00, 0x00, 0x33, 0x05, 0xf5, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xb7, 0xb7, 0xff, 0xff, 0x33, 0x05, 0xf5, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x6f, 0x00, 0x00, 0x00
};
unsigned int initcode_len = 52;
```

**可以用上面的代码替换user目录下的initcode.c，在terminal下user目录下，执行：make init就可以直接替换initcode.h中的16进制代码。
或者直接用下面的代码放到initcode.h中**

## 测试结果
```
$ make qemu

  xv6-riscv Lab5
========================================

Physical memory initialization...
pmem: kern_region [0x000000008000a000 - 0x000000008040a000], 1024 pages
pmem: user_region [0x000000008040a000 - 0x0000000088000000], 31734 pages
Done.

...

Creating First User Process (proczero)
========================================

[proc] Allocating process structure for proczero (pid=0)...
[proc] Trapframe allocated at 0x0x0000000087fb4000
[proc] User page table created at 0x0x0000000087fb3000
[proc] User stack mapped at VA 0x0x0000003fffffd000
[proc] User code (52 bytes) loaded at VA 0x0x0000000000001000
[proc] Process proczero created successfully!

 Switching from kernel to user mode...
CPU 0 will now run in user process proczero

[syscall] User process (pid=0) issued system call #1
[syscall] User process (pid=0) issued system call #2
[syscall] User process (pid=0) issued system call #3

========================================
  User Process Execution Complete!
========================================

[Result] proczero executed 3 system calls successfully.
[Result] CPU 0 is now trapped in user-mode infinite loop.
[Result] Other CPUs are trapped in main() idle loop.

```
最后得到的结果大致如下，系统正确的运行了上面代码的三次系统调用。