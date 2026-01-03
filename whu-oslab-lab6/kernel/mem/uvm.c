#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"
#include "riscv.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 2（SV39三级页表）, level = 0 说明到达最低级页表
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    // 遍历页表的 512 个条目
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        
        // 检查 PTE 是否有效
        if (!(pte & PTE_V)) continue;
        
        if (level == 0) {
            // 最低级页表，pte 指向物理页
            // 释放该物理页
            uint64 pa = PTE_TO_PA(pte);
            pmem_free(pa, false);
        } else if (PTE_CHECK(pte)) {
            // 非叶子节点（R/W/X 全为0），指向下级页表
            pgtbl_t child = (pgtbl_t)PTE_TO_PA(pte);
            destroy_pgtbl(child, level - 1);
        } else {
            // 叶子节点（有 R/W/X 权限），释放物理页
            uint64 pa = PTE_TO_PA(pte);
            pmem_free(pa, false);
        }
    }
    
    // 释放当前页表本身
    pmem_free((uint64)pgtbl, false);
}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // 解除 trampoline 映射（不释放物理页，因为是共享的）
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);
    
    // 解除 trapframe 映射（需要释放物理页，因为是进程独有的）
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);
    
    // 递归释放整个页表（从顶级页表 level=2 开始）
    destroy_pgtbl(pgtbl, 2);
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top (代码段+数据段+堆) */
    // 注意：用户空间从 PGSIZE 开始（跳过空白保护页）
    copy_range(old, new, PGSIZE, heap_top);

    /* step-2: ustack (用户栈) */
    uint64 ustack_begin = TRAPFRAME - ustack_pages * PGSIZE;
    copy_range(old, new, ustack_begin, TRAPFRAME);

    /* step-3: mmap_region (内存映射区域)*/
    // 注意：这里传入的mmap链表记录的是"可分配"区域，不是已映射区域
    // 已映射的区域需要通过其他方式追踪，这里暂时留空
    // 如果需要拷贝已映射区域，需要单独实现
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    proc_t* p = myproc();
    uint64 len = npages * PGSIZE;
    
    // 遍历mmap链表，从空闲区域中分割
    mmap_region_t* prev = NULL;
    mmap_region_t* curr = p->mmap;
    
    while (curr != NULL) {
        uint64 region_end = curr->begin + curr->npages * PGSIZE;
        
        // 检查请求区域是否在当前空闲区域内
        if (begin >= curr->begin && begin + len <= region_end) {
            // 找到了包含请求区域的空闲区域
            
            if (begin == curr->begin && len == curr->npages * PGSIZE) {
                // 完全匹配，移除整个区域
                if (prev == NULL) {
                    p->mmap = curr->next;
                } else {
                    prev->next = curr->next;
                }
                mmap_region_free(curr);
            } else if (begin == curr->begin) {
                // 从头部分割
                curr->begin += len;
                curr->npages -= npages;
            } else if (begin + len == region_end) {
                // 从尾部分割
                curr->npages -= npages;
            } else {
                // 从中间分割，需要创建新节点
                mmap_region_t* new_region = mmap_region_alloc();
                new_region->begin = begin + len;
                new_region->npages = (region_end - begin - len) / PGSIZE;
                new_region->next = curr->next;
                curr->npages = (begin - curr->begin) / PGSIZE;
                curr->next = new_region;
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    // 为每个页面申请物理页并建立映射
    for (uint32 i = 0; i < npages; i++) {
        uint64 va = begin + i * PGSIZE;
        uint64 pa = (uint64)pmem_alloc(false);
        if (pa == 0) {
            panic("uvm_mmap: out of memory");
        }
        memset((void*)pa, 0, PGSIZE);
        vm_mappages(p->pgtbl, va, pa, PGSIZE, perm | PTE_U);
    }
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    proc_t* p = myproc();
    uint64 len = npages * PGSIZE;
    
    // 创建新的空闲区域
    mmap_region_t* new_region = mmap_region_alloc();
    new_region->begin = begin;
    new_region->npages = npages;
    new_region->next = NULL;
    
    // 将新区域插入到链表中（按地址排序）
    if (p->mmap == NULL || begin < p->mmap->begin) {
        // 插入到链表头
        new_region->next = p->mmap;
        p->mmap = new_region;
    } else {
        // 找到插入位置
        mmap_region_t* curr = p->mmap;
        while (curr->next != NULL && curr->next->begin < begin) {
            curr = curr->next;
        }
        new_region->next = curr->next;
        curr->next = new_region;
    }
    
    // 尝试与后继合并
    if (new_region->next != NULL && 
        new_region->begin + new_region->npages * PGSIZE == new_region->next->begin) {
        mmap_region_t* to_merge = new_region->next;
        new_region->next = to_merge->next;
        mmap_merge(new_region, to_merge, true);
    }
    
    // 尝试与前驱合并
    mmap_region_t* prev = NULL;
    mmap_region_t* curr = p->mmap;
    while (curr != new_region && curr != NULL) {
        prev = curr;
        curr = curr->next;
    }
    if (prev != NULL && 
        prev->begin + prev->npages * PGSIZE == new_region->begin) {
        prev->next = new_region->next;
        mmap_merge(prev, new_region, true);
    }
    
    // 解除页表映射并释放物理页
    vm_unmappages(p->pgtbl, begin, len, true);
}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top + len;
    
    // 计算需要映射的页面
    // old_aligned: 旧堆顶向上对齐到页边界
    // new_aligned: 新堆顶向上对齐到页边界
    uint64 old_aligned = PG_ROUND_UP(heap_top);
    uint64 new_aligned = PG_ROUND_UP(new_heap_top);
    
    // 为新增的页面分配物理页并建立映射
    for (uint64 va = old_aligned; va < new_aligned; va += PGSIZE) {
        uint64 pa = (uint64)pmem_alloc(false);
        if (pa == 0) {
            panic("uvm_heap_grow: out of memory");
        }
        memset((void*)pa, 0, PGSIZE);
        vm_mappages(pgtbl, va, pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    }
    
    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    
    // 计算需要释放的页面
    // old_aligned: 旧堆顶向上对齐到页边界
    // new_aligned: 新堆顶向上对齐到页边界
    uint64 old_aligned = PG_ROUND_UP(heap_top);
    uint64 new_aligned = PG_ROUND_UP(new_heap_top);
    
    // 释放不再需要的页面
    if (new_aligned < old_aligned) {
        uint64 npages = (old_aligned - new_aligned) / PGSIZE;
        vm_unmappages(pgtbl, new_aligned, npages * PGSIZE, true);
    }
    
    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;
    
    while (len > 0) {
        // 获取src所在页的起始地址
        va0 = PG_ROUND_DOWN(src);
        
        // 通过页表查找物理地址
        pte_t* pte = vm_getpte(pgtbl, va0, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyin: invalid page");
        }
        pa0 = PTE_TO_PA(*pte);
        
        // 计算当前页内可拷贝的字节数
        n = PGSIZE - (src - va0);
        if (n > len) n = len;
        
        // 执行拷贝：从物理地址对应位置拷贝到内核地址
        memmove((void*)dst, (void*)(pa0 + (src - va0)), n);
        
        len -= n;
        dst += n;
        src = va0 + PGSIZE;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;
    
    while (len > 0) {
        // 获取dst所在页的起始地址
        va0 = PG_ROUND_DOWN(dst);
        
        // 通过页表查找物理地址
        pte_t* pte = vm_getpte(pgtbl, va0, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyout: invalid page");
        }
        pa0 = PTE_TO_PA(*pte);
        
        // 计算当前页内可拷贝的字节数
        n = PGSIZE - (dst - va0);
        if (n > len) n = len;
        
        // 执行拷贝：从内核地址拷贝到物理地址对应位置
        memmove((void*)(pa0 + (dst - va0)), (void*)src, n);
        
        len -= n;
        src += n;
        dst = va0 + PGSIZE;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 n, va0, pa0;
    bool got_null = false;
    
    while (!got_null && maxlen > 0) {
        // 获取src所在页的起始地址
        va0 = PG_ROUND_DOWN(src);
        
        // 通过页表查找物理地址
        pte_t* pte = vm_getpte(pgtbl, va0, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyin_str: invalid page");
        }
        pa0 = PTE_TO_PA(*pte);
        
        // 计算当前页内可拷贝的字节数
        n = PGSIZE - (src - va0);
        if (n > maxlen) n = maxlen;
        
        // 逐字节拷贝，遇到'\0'终止
        char* p = (char*)(pa0 + (src - va0));
        while (n > 0) {
            if (*p == '\0') {
                *(char*)dst = '\0';
                got_null = true;
                break;
            }
            *(char*)dst = *p;
            n--;
            maxlen--;
            p++;
            dst++;
        }
        
        src = va0 + PGSIZE;
    }
}