#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;

// icache初始化
void inode_init()
{
    spinlock_init(&lk_icache, "icache");
    for(int i = 0; i < N_INODE; i++) {
        sleeplock_init(&icache[i].slk, "inode");
    }
}

/*---------------------- 与inode本身相关 -------------------*/

// 使用磁盘里的inode更新内存里的inode (write = false)
// 或 使用内存里的inode更新磁盘里的inode (write = true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
    assert(sleeplock_holding(&ip->slk), "inode_rw: not holding lock");
    
    // 计算inode所在的block
    uint32 block_num = sb.inode_start + ip->inode_num / INODE_PER_BLOCK;
    buf_t* buf = buf_read(block_num);
    
    // 磁盘中的inode地址（每个inode 64字节）
    uint8* dip = buf->data + (ip->inode_num % INODE_PER_BLOCK) * INODE_DISK_SIZE;
    
    if(write) {
        // 内存 -> 磁盘
        memmove(dip, &ip->type, INODE_DISK_SIZE);
        buf_write(buf);
    } else {
        // 磁盘 -> 内存
        memmove(&ip->type, dip, INODE_DISK_SIZE);
    }
    buf_release(buf);
}

// 在icache里查询inode
// 如果没有查询到则申请一个空闲inode
// 如果icache没有空闲inode则报错
// 注意: 获得的inode没有上锁
inode_t* inode_alloc(uint16 inode_num)
{    
    inode_t* empty = NULL;
    
    spinlock_acquire(&lk_icache);
    
    // 查找已缓存的inode
    for(int i = 0; i < N_INODE; i++) {
        if(icache[i].ref > 0 && icache[i].inode_num == inode_num) {
            icache[i].ref++;
            spinlock_release(&lk_icache);
            return &icache[i];
        }
        if(empty == NULL && icache[i].ref == 0)
            empty = &icache[i];
    }
    
    // 分配新的inode缓存位置
    if(empty == NULL)
        panic("inode_alloc: no free inode");
    
    empty->inode_num = inode_num;
    empty->ref = 1;
    empty->valid = false;
    
    spinlock_release(&lk_icache);
    return empty;
}

// 在磁盘里申请一个inode (操作bitmap, 返回inode_num)
// 向icache申请一个inode数据结构
// 填写内存里的inode并以此更新磁盘里的inode
// 注意: 获得的inode没有上锁
inode_t* inode_create(uint16 type, uint16 major, uint16 minor)
{
    // 在磁盘申请inode
    uint16 inode_num = bitmap_alloc_inode();
    
    // 在内存申请inode
    inode_t* ip = inode_alloc(inode_num);
    
    // 上锁并初始化
    inode_lock(ip);
    ip->type = type;
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    ip->size = 0;
    memset(ip->addrs, 0, sizeof(ip->addrs));
    ip->valid = true;
    
    // 写入磁盘
    inode_rw(ip, true);
    
    // 如果是目录，创建.和..  
    if(type == FT_DIR) {
        // 分配一个数据块
        ip->addrs[0] = bitmap_alloc_block();
        ip->size = 0;  // dir_add_entry会更新size
        inode_rw(ip, true);
    }
    
    inode_unlock(ip);
    return ip;
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    sleeplock_acquire(&ip->slk);
    
    // 释放数据块
    inode_free_data(ip);
    
    // 清除磁盘inode
    ip->type = FT_UNUSED;
    inode_rw(ip, true);
    
    // 释放bitmap
    bitmap_free_inode(ip->inode_num);
    
    sleeplock_release(&ip->slk);
    ip->valid = false;
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
void inode_free(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    
    if(ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // 最后一个引用且无链接，销毁inode
        inode_destroy(ip);
    }
    
    ip->ref--;
    spinlock_release(&lk_icache);
}

// ip->ref++ with lock
inode_t* inode_dup(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    ip->ref++;
    spinlock_release(&lk_icache);
    return ip;
}

// 给inode上锁
// 如果valid失效则从磁盘中读入
void inode_lock(inode_t* ip)
{
    assert(ip != NULL && ip->ref > 0, "inode_lock: invalid inode");
    
    sleeplock_acquire(&ip->slk);
    
    if(ip->valid == false) {
        inode_rw(ip, false);
        ip->valid = true;
    }
}

// 给inode解锁
void inode_unlock(inode_t* ip)
{
    assert(ip != NULL && sleeplock_holding(&ip->slk), "inode_unlock: not holding lock");
    sleeplock_release(&ip->slk);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    inode_unlock(ip);
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block();

    if(size == 1)
        return *entry;    

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(*entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);
    buf_release(buf);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    // 在第一个区域（一级映射）
    if(bn < N_ADDRS_1)
        return locate_block(&ip->addrs[bn], bn, 1);

    // 在第二个区域（二级映射）
    bn -= N_ADDRS_1;
    if(bn < N_ADDRS_2 * ENTRY_PER_BLOCK)
    {
        uint32 size = ENTRY_PER_BLOCK;
        uint32 idx = bn / size;
        uint32 b = bn % size;
        return locate_block(&ip->addrs[N_ADDRS_1 + idx], b, size);
    }

    // 在第三个区域（三级映射）
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;
    if(bn < N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK)
    {
        uint32 size = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
        uint32 idx = bn / size;
        uint32 b = bn % size;
        return locate_block(&ip->addrs[N_ADDRS_1 + N_ADDRS_2 + idx], b, size);
    }

    panic("inode_locate_block: overflow");
    return 0;
}

// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_read_data: not holding lock");
    
    // 边界检查
    if(offset > ip->size)
        return 0;
    if(offset + len > ip->size)
        len = ip->size - offset;
    
    uint32 total = 0;
    uint32 block_num, block_offset, read_len;
    
    while(total < len) {
        block_num = inode_locate_block(ip, offset / BLOCK_SIZE);
        block_offset = offset % BLOCK_SIZE;
        
        // 计算这次读取的字节数
        read_len = BLOCK_SIZE - block_offset;
        if(read_len > len - total)
            read_len = len - total;
        
        buf_t* buf = buf_read(block_num);
        
        if(user) {
            uvm_copyout(myproc()->pgtbl, (uint64)dst + total, 
                       (uint64)(buf->data + block_offset), read_len);
        } else {
            memmove((char*)dst + total, buf->data + block_offset, read_len);
        }
        
        buf_release(buf);
        total += read_len;
        offset += read_len;
    }
    
    return total;
}

// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_write_data: not holding lock");
    
    // 边界检查
    if(offset > ip->size)
        return 0;
    if(offset + len > INODE_MAXSIZE)
        return 0;
    
    uint32 total = 0;
    uint32 block_num, block_offset, write_len;
    
    while(total < len) {
        block_num = inode_locate_block(ip, offset / BLOCK_SIZE);
        block_offset = offset % BLOCK_SIZE;
        
        // 计算这次写入的字节数
        write_len = BLOCK_SIZE - block_offset;
        if(write_len > len - total)
            write_len = len - total;
        
        buf_t* buf = buf_read(block_num);
        
        if(user) {
            uvm_copyin(myproc()->pgtbl, (uint64)(buf->data + block_offset),
                      (uint64)src + total, write_len);
        } else {
            memmove(buf->data + block_offset, (char*)src + total, write_len);
        }
        
        buf_write(buf);
        buf_release(buf);
        total += write_len;
        offset += write_len;
    }
    
    // 更新size
    if(offset > ip->size) {
        ip->size = offset;
    }
    
    // 写回inode元数据
    inode_rw(ip, true);
    
    return total;
}

// 辅助 inode_free_data 做递归释放
static void data_free(uint32 block_num, uint32 level)
{  
    assert(block_num != 0, "data_free: block_num = 0");

    // block_num 是 data block
    if(level == 0) goto ret;

    // block_num 是 metadata block
    buf_t* buf = buf_read(block_num);
    for(uint32* addr = (uint32*)buf->data; addr < (uint32*)(buf->data + BLOCK_SIZE); addr++) 
    {
        if(*addr == 0) break;
        data_free(*addr, level - 1);
    }
    buf_release(buf);

ret:
    bitmap_free_block(block_num);
    return;
}

// 释放inode管理的 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有slk
void inode_free_data(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "inode_free_data: not holding lock");
    
    // 释放一级映射的block
    for(int i = 0; i < N_ADDRS_1; i++) {
        if(ip->addrs[i] != 0) {
            data_free(ip->addrs[i], 0);
            ip->addrs[i] = 0;
        }
    }
    
    // 释放二级映射的block
    for(int i = 0; i < N_ADDRS_2; i++) {
        if(ip->addrs[N_ADDRS_1 + i] != 0) {
            data_free(ip->addrs[N_ADDRS_1 + i], 1);
            ip->addrs[N_ADDRS_1 + i] = 0;
        }
    }
    
    // 释放三级映射的block
    for(int i = 0; i < N_ADDRS_3; i++) {
        if(ip->addrs[N_ADDRS_1 + N_ADDRS_2 + i] != 0) {
            data_free(ip->addrs[N_ADDRS_1 + N_ADDRS_2 + i], 2);
            ip->addrs[N_ADDRS_1 + N_ADDRS_2 + i] = 0;
        }
    }
    
    ip->size = 0;
    inode_rw(ip, true);
}

static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
};

// 输出inode信息
// for dubug
void inode_print(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inode_num, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}