#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"

extern super_block_t sb;

// search and set bit
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    uint32 byte, shift;
    uint8 bit_cmp;

    buf_t* buf = buf_read(bitmap_block);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if((bit_cmp & buf->data[byte]) == 0) {
                buf->data[byte] |= bit_cmp;
                buf_write(buf);
                buf_release(buf);
                return byte * 8 + shift;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    buf_release(buf);
    panic("bitmap_search_and_set: no bit left");
    return 0;
}

// unset bit
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    uint32 byte = num / 8;
    uint32 shift = num % 8;
    uint8 bit_cmp = 1 << shift;

    buf_t* buf = buf_read(bitmap_block);
    if((buf->data[byte] & bit_cmp) == 0)
        panic("bitmap_unset: bit already free");
    buf->data[byte] &= ~bit_cmp;
    buf_write(buf);
    buf_release(buf);
}

uint32 bitmap_alloc_block()
{
    uint32 bit_num = bitmap_search_and_set(sb.data_bitmap_start);
    return sb.data_start + bit_num;
}

void bitmap_free_block(uint32 block_num)
{
    bitmap_unset(sb.data_bitmap_start, block_num - sb.data_start);
}

uint16 bitmap_alloc_inode()
{
    return (uint16)bitmap_search_and_set(sb.inode_bitmap_start);
}

void bitmap_free_inode(uint16 inode_num)
{
    bitmap_unset(sb.inode_bitmap_start, (uint32)inode_num);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}