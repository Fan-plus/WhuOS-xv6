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
    while(lk->locked) {
        proc_sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
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
    r = lk->locked && (lk->pid == myproc()->pid);
    spinlock_release(&lk->lk);
    return r;
}
