#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "lib/str.h"
#include "lib/print.h"

// 超级块在内存的副本
super_block_t sb;

#define FS_MAGIC 0x12345678
#define SB_BLOCK_NUM 0

// 输出super_block的信息
static void sb_print()
{
    printf("\nsuper block information:\n");
    printf("magic = %x\n", sb.magic);
    printf("block size = %d\n", sb.block_size);
    printf("inode blocks = %d\n", sb.inode_blocks);
    printf("data blocks = %d\n", sb.data_blocks);
    printf("total blocks = %d\n", sb.total_blocks);
    printf("inode bitmap start = %d\n", sb.inode_bitmap_start);
    printf("inode start = %d\n", sb.inode_start);
    printf("data bitmap start = %d\n", sb.data_bitmap_start);
    printf("data start = %d\n", sb.data_start);
}

// 测试用的辅助数据
static uint8 str[BLOCK_SIZE * 2];
static uint8 tmp[BLOCK_SIZE * 2];

// 比较两个缓冲区
static bool blockcmp(uint8* a, uint8* b)
{
    for(int i = 0; i < BLOCK_SIZE * 2; i++) {
        if(a[i] != b[i]) return false;
    }
    return true;
}

// 文件系统初始化 + inode读写测试
void fs_init()
{
    buf_init();

    buf_t* buf; 
    buf = buf_read(SB_BLOCK_NUM);
    memmove(&sb, buf->data, sizeof(sb));
    assert(sb.magic == FS_MAGIC, "fs_init: magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: block size");
    buf_release(buf);
    sb_print();

    // ========== inode读写测试开始 ==========
    printf("\n========== INODE READ/WRITE TEST ==========\n");
    
    // inode初始化
    inode_init();
    uint32 ret = 0;

    for(int i = 0; i < BLOCK_SIZE * 2; i++)
        str[i] = (uint8)i;

    // 创建新的inode
    inode_t* nip = inode_create(FT_FILE, 0, 0);
    inode_lock(nip);
    
    // 第一次查看
    printf("\n[1] Initial inode state:\n");
    inode_print(nip);

    // 第一次写入
    ret = inode_write_data(nip, 0, BLOCK_SIZE / 2, str, false);
    printf("\n[2] First write: %d bytes (expected %d)\n", ret, BLOCK_SIZE / 2);
    assert(ret == BLOCK_SIZE / 2, "inode_write_data: fail");

    // 第二次写入
    ret = inode_write_data(nip, BLOCK_SIZE / 2, BLOCK_SIZE + BLOCK_SIZE / 2, str + BLOCK_SIZE / 2, false);
    printf("[3] Second write: %d bytes (expected %d)\n", ret, BLOCK_SIZE + BLOCK_SIZE / 2);
    assert(ret == BLOCK_SIZE + BLOCK_SIZE / 2, "inode_write_data: fail");

    // 一次读取
    ret = inode_read_data(nip, 0, BLOCK_SIZE * 2, tmp, false);
    printf("[4] Read: %d bytes (expected %d)\n", ret, BLOCK_SIZE * 2);
    assert(ret == BLOCK_SIZE * 2, "inode_read_data: fail");

    // 第二次查看
    printf("\n[5] Final inode state:\n");
    inode_print(nip);
    
    inode_unlock_free(nip);

    // 测试结果
    printf("\n========== TEST RESULT ==========\n");
    if(blockcmp(tmp, str) == true)
        printf(">>> INODE READ/WRITE TEST: SUCCESS <<<\n");
    else
        printf(">>> INODE READ/WRITE TEST: FAILED <<<\n");

    printf("=================================\n");
    while (1); 
}