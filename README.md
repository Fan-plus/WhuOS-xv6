# whu-oslab-lab2

此分支为实验二：页表和内存管理

# 编译运行

## 内存第一阶段测试

**此时main函数设置为**
```
volatile static int started = 0;

volatile static int over_1 = 0, over_2 = 0;

static int* mem[1024];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;

        for(int i = 0; i < 512; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_1 = 1;
        
        while(over_1 == 0 || over_2 == 0);
        
        for(int i = 0; i < 512; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        for(int i = 512; i < 1024; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_2 = 1;

        while(over_1 == 0 || over_2 == 0);

        for(int i = 512; i < 1024; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);        
 
    }
    while (1);    
}
```

```
make clean && make qemu
```
会得到类似下面的输出
```
...
mem = 0x0000000080026000, data = 16843009
mem = 0x0000000080025000, data = 16843009
mem = 0x0000000080024000, data = 16843009
mem = 0x0000000080023000, data = 16843009
mem = 0x0000000080022000, data = 16843009
mem = 0x0000000080021000, data = 16843009
mem = 0x0000000080020000, data = 16843009
mem = 0x000000008001f000, data = 16843009
mem = 0x000000008001e000, data = 16843009
mem = 0x000000008001d000, data = 16843009
mem = 0x000000008001c000, data = 16843009
mem = 0x000000008001b000, data = 16843009
mem = 0x0000000080037000, data = 16843009
mem = 0x0000000080019000, data = 16843009
mem = 0x000000008001a000, data = 16843009
mem = 0x0000000080017000, data = 16843009
mem = 0x0000000080016000, data = 16843009
mem = 0x0000000080015000, data = 16843009
mem = 0x0000000080014000, data = 16843009
mem = 0x0000000080013000, data = 16843009
mem = 0x0000000080012000, data = 16843009
mem = 0x0000000080011000, data = 16843009
mem = 0x0000000080010000, data = 16843009
mem = 0x000000008000f000, data = 16843009
mem = 0x000000008000e000, data = 16843009
mem = 0x000000008000d000, data = 16843009
mem = 0x000000008000c000, data = 16843009
mem = 0x000000008000b000, data = 16843009
mem = 0x000000008000a000, data = 16843009
mem = 0x0000000080009000, data = 16843009
mem = 0x0000000080008000, data = 16843009
cpu 0 alloc over
mem = 0x0000000080018000, data = 16843009
mem = 0x0000000080007000, data = 16843009
mem = 0x0000000080006000, data = 16843009
cpu 1 alloc over
cpu 1 free over
cpu 0 free over
QEMU: Terminated
```


## 内存第二阶段测试
**此时main函数设置为**
```
int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        // started = 1;

        pgtbl_t test_pgtbl = pmem_alloc(true);
        uint64 mem[5];
        for(int i = 0; i < 5; i++)
            mem[i] = (uint64)pmem_alloc(false);

        printf("\ntest-1\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
        vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE / 2, PTE_R | PTE_W);
        vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE - 1, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, VA_MAX - PGSIZE, mem[4], PGSIZE, PTE_W);
        vm_print(test_pgtbl);

        printf("\ntest-2\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
        vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
        vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
        vm_print(test_pgtbl);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
         
    }
    while (1);    
}
```

此时再次`make clean && make qemu`，会得到类似下面的输出
```
test-1

page table 0x0000000080403000
..0: pte 0x... pa 0x...
 ..0: pte 0x... pa 0x...
  ..0: pte 0x... R              ← 地址0(VPN[2]=0, VPN[1]=0, VPN[0]=0)，只读
  ..10: pte 0x... R W           ← 地址40KB(VPN[2]=0, VPN[1]=0, VPN[0]=10)，可读写
 ..1: pte 0x... pa 0x...
  ..0: pte 0x... R X            ← 地址2MB(VPN[2]=0, VPN[1]=1, VPN[0]=0)，可读执行
..1: ...
  ..0: pte 0x... R X            ← 地址1GB(VPN[2]=1, VPN[1]=0, VPN[0]=0)，可读执行
..255: ...
  ..511: pte 0x... W            ← 最大地址(VPN[2]=255, VPN[1]=511, VPN[0]=511)，只写

test-2

page table 0x0000000080403000
..0: pte 0x... pa 0x...
 ..0: pte 0x... pa 0x...
  ..0: pte 0x... W              ← 权限已从R变为W
 ..1: pte 0x... pa 0x...        ← ..10 消失（已删除）
..1: ...                         ← 地址2MB消失（已删除）
..255: ...                       ← 最大地址仍存在
```