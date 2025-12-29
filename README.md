# whu-oslab-lab1

此分支为实验一：RISC-V 引导和裸机启动

# 编译运行

```
make clean && make qemu
```

看到输出

```
qemu-system-riscv64 -machine virt -bios none -kernel kernel-qemu  -m 128M -smp 2 -nographic
cpu 0 is booting!
cpu 1 is booting!
```
