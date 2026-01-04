#include "lib/lock.h"
#include "proc/proc.h"
#include "proc/cpu.h"

// 初始化睡眠锁
void sleeplock_init(sleeplock_t* lk, char* name)
{
    spinlock_init(&lk->lk, "sleeplock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

// 获取睡眠锁
void sleeplock_acquire(sleeplock_t* lk)
{
    spinlock_acquire(&lk->lk);
    
    // 在没有进程的情况下（内核初始化阶段），使用自旋等待
    proc_t* p = myproc();
    while(lk->locked) {
        if(p != NULL) {
            proc_sleep(lk, &lk->lk);
        } else {
            // 无进程时自旋等待
            spinlock_release(&lk->lk);
            spinlock_acquire(&lk->lk);
        }
    }
    lk->locked = 1;
    lk->pid = (p != NULL) ? p->pid : -1;
    spinlock_release(&lk->lk);
}

// 释放睡眠锁
void sleeplock_release(sleeplock_t* lk)
{
    spinlock_acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    proc_wakeup(lk);
    spinlock_release(&lk->lk);
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t* lk)
{
    int r;
    spinlock_acquire(&lk->lk);
    proc_t* p = myproc();
    if(p != NULL) {
        r = lk->locked && (lk->pid == p->pid);
    } else {
        r = lk->locked && (lk->pid == -1);
    }
    spinlock_release(&lk->lk);
    return r;
}
