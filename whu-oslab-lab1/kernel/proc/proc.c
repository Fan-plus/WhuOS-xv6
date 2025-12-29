#include "proc/proc.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

// 获取当前 CPU 结构体指针
// 必须在关中断状态下调用，防止调度导致 hartid 变化
cpu_t* mycpu(void)
{
    int id = r_tp();  // tp 寄存器保存 hartid
    return &cpus[id];
}

// 获取当前 CPU 的 hartid
int mycpuid(void) 
{
    return r_tp();
}
