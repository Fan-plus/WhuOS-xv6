# whu-oslab-lab3

此分支为实验三：中断处理与时钟管理

# 时钟中断
- `xv6` 关于`M`模式下的初始化和中断处理，涉及`start.c` 和 `kernelves.S` 中的`timervec`
- 关于`S`模式下软件中断的处理,涉及`main.c` `trap.S`中的`kernelvec`、`trap.c`。其中`trap.c`的`kerneltrap()` 和 `devintr()` 居于核心调度的地位。


# 外部中断
此处简单以`uart`为例，介绍通过`PLIC`是心爱外部中断的过程，此后可以按情况增加其他种类的外部中断



# 编译运行

```
make qemu
```
此时的输出大致类似
```
qemu-system-riscv64 -machine virt -bios none -kernel kernel-qemu  -m 128M -smp 2 -nographic
pmem: kern_region [0x0000000080005000 - 0x0000000080405000], 1024 pages
pmem: user_region [0x0000000080405000 - 0x0000000088000000], 31739 pages
cpu 0 is booting!
interrupts enabled, waiting for timer ticks...
cpu 1 is booting!
TTTTTTTTTT
ticks = 100
TTTTTTTTTT
ticks = 200
TTTTTTTTTT
ticks = 300
TTTTTTTTTT
ticks = 400
TTTTTTTTTT
ticks = 500
...
```

代表时钟中断基本正确

此时如果想测试下基本的`uart`外部中断,可以直接键盘输入，会得到类似的结果
```
...(续上)
TTTTTTTTTT
ticks = 600
TTTTTdaTfadfaadicnasnT  cTacTaswhuoT
ticks = 700
sTssdwhTwuosdcaTscadascaTccaTTTTTT
ticks = 800
```
可以看到`T`和通过键盘输入的字符交错输出，代表`uart`外部中断基本正确