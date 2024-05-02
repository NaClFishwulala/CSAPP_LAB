/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "EggsHead",
    /* First member's full name */
    "NaClFishwulala",
    /* First member's email address */
    "1392377067@qq.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 存储结构： 隐式空闲链表、显式空闲链表、分离空闲链表
 * 放置策略： 首次适配、下一次适配、最佳适配、分离适配
 */

/* 存储结构设计：显式空闲链表（分配块 与 带边界标记的空闲块）
 * 分配块：
 * 
 * |  31  ...  3  |  2  |     1   |  0  |
 * |--------------|-----|---------|-----|
 * |     块大小    | Bit |Pre_Alloc|Alloc|
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |              有效载荷               |
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |                填充                 |
 *
 * 块大小: 29位，表示当前块的大小，为8的倍数
 * Bit 2: 1位，保留
 * Bit 1: 1位，保留
 * Alloc 0: 1位，0表示当前块空闲， 1表示当前块已分配

 * 空闲块：
 * |  31  ...  3  |  2  |     1   |  0  |
 * |--------------|-----|---------|-----|
 * |     块大小    | Bit |Pre_Alloc|Alloc|
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |               pred                 |
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |               succ                 |
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |               有效载荷              |
 * |------------------------------------|
 * |            31  ...  0              |
 * |------------------------------------|
 * |                填充                |
 * |------------------------------------|
 * |  31  ...  3  |  2  |     1   |  0  |
 * |--------------|-----|---------|-----|
 * |     块大小    | Bit |Pre_Alloc|Alloc|
 * 
 * 块大小: 29位，表示当前块的大小，为8的倍数
 * Bit 2: 1位，保留
 * Pre_Alloc: 1位，0表示上个块空闲， 1表示上个块已分配
 * Alloc: 1位，0表示当前块空闲， 1表示当前块已分配
 * pred: 指向上一个空闲块 
 * succ: 指向下一个空闲块
 * 
 * 堆结构
 * |    a word    |            a word            |        a word        |  a word  |words|a word |       a word         |
 * |--------------|------------------------------|----------------------|----------|-----|-------|----------------------|
 * |  双字对齐填充 | 哨兵空闲链表头(pred字段无实意) |哨兵空闲链表头(succ字段)|  序言块  | ... | 结尾块 |哨兵空闲链表尾(pred字段)|
 * 
 * 空闲链表排序策略：地址顺序
 * 分配块的策略：首次适配
 * 释放和合并策略：带边界标记的立即合并
 */

/* 以下为操作空闲链表的基本常数和宏 */
#define WSIZE 4 /* 字 头部/尾部大小 4字节 */
#define DSIZE 8 /* 双字 8字节 */
#define CHUNKSIZE (1 << 12) /* 每次堆扩展的字节 */ 

/* 将块大小、前一个块和当前块的分配位打包成一个字 */
#define PACK(size, pre_alloc, alloc) ((size) | (pre_alloc << 1) | (alloc)) 

/* 在地址p处读或写一个字 */
#define GET_VAL(p) (*(unsigned int *)(p))
#define PUT_VAL(p, val) (*(unsigned int *)(p) = (val))

/* 在地址p处获取或保存一个地址 */
#define GET_ADDR(p) (*(char **)(p))
#define PUT_ADDR(p, addr) (*(char **)(p) = (addr))

/* 从地址p处读取块大小、前一个块的分配位或者当前块的分配位 */
#define GET_SIZE(p) (GET_VAL(p) & ~0x07)
#define GET_PRE_ALLOC(p) ((GET_VAL(p) & 0x02) >> 1)
#define GET_ALLOC(p) (GET_VAL(p) & 0x01)

/* 给定一个块指针bp, 计算该块的头地址或尾地址 */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)    /* 只有空闲块有尾地址 */

/* 给定一个空闲块块指针bp, 计算该空闲块的pred或succ地址 */
#define PREDP(bp) ((char *)(bp))
#define SUCCP(bp) ((char *)(bp) + WSIZE)

/* 给定一个块指针bp, 计算下一个块的地址 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* 以下为空闲链表相关标量 */
// 用mem_heap_lo获取空闲链表哨兵头部 存下一个空闲块的地址
// 用mem_heap_hi获取空闲链表哨兵尾部 存上一个空闲块的地址


/* 以下为辅助函数的声明 */
static void *colaesce(void *ptr);
static void *extend_heap(size_t words);
static void *find_pred(void *ptr);
static void *find_fit(size_t size);

/* 
 * mm_init - initialize the malloc package.
 * 成功返回0 失败返回-1
 */
int mm_init(void)
{
    char *heap_listp; /* 堆指针 */
    /* 初始化一个空的堆 填充(1字) + 哨兵空闲链表头(2字) + 序言块(1字) + 结尾块(1字) + 哨兵空闲链表尾(1字) */
    if((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)    /* mem_sbrk通过将内核的brk指针增加incr来扩展和收缩堆。如果成功返回旧值，失败返回-1 */
        return -1;
    PUT_VAL(heap_listp, 0); /* 填充 */
    PUT_ADDR(heap_listp + WSIZE, NULL); /* 哨兵空闲链表头pred字段 存放下一个空闲块的地址 */
    PUT_ADDR(heap_listp + 2 * WSIZE, mem_heap_hi()); /* 哨兵空闲链表头succ字段 存放下一个空闲块的地址 */
    /* 序言块和结尾块用于减少合并时的边界判断 */
    PUT_VAL(heap_listp + 3 * WSIZE, PACK(WSIZE, 1, 1));
    PUT_VAL(heap_listp + 4 * WSIZE, PACK(WSIZE, 1, 1));
    PUT_ADDR(heap_listp + 5 * WSIZE, mem_heap_lo()); /* 哨兵空闲链表尾pred字段 存放上一个空闲块的地址 */
    // heap_listp += 2 * WSIZE;

    /* 以CHUNKSIZE字节大小来扩展一个空的堆 */
    if(extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    size_t pre_alloc = GET_PRE_ALLOC(HDRP(ptr));
    char *pred_ptr;
    char *succ_ptr;
    /* 释放后已分配块变为空闲块 更改头部 添加尾部 添加pred succ*/
    PUT_VAL(HDRP(ptr), PACK(size, pre_alloc, 0));
    PUT_VAL(FTRP(ptr), PACK(size, pre_alloc, 0));
    if((pred_ptr = find_pred(ptr)) == NULL) /* 因为有哨兵空闲链表头存在，所以一定能找到，找不到说明错了 */
        return -1;
    succ_ptr = GET_ADDR(SUCCP(pred_ptr));
    PUT_ADDR(PREDP(ptr), pred_ptr);
    PUT_ADDR(SUCCP(ptr), succ_ptr);
    /* 更新前继空闲块的succ字段和后继空闲块的pred字段 */
    PUT_ADDR(SUCCP(pred_ptr), ptr);
    PUT_ADDR(PREDP(succ_ptr), ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/** 
 *在 `.c` 文件中定义的 `static` 函数不需要在 `.h` 文件中声明。
 * `static` 函数的作用域被限制在定义它的源文件内，因此其他源文件无法访问该函数。
 * 因此，在头文件中声明这样的函数是没有必要的。
 * 通常情况下，头文件中声明的函数是为了在多个源文件中共享接口，而 `static` 函数是不会共享给其他源文件的，因此不需要在头文件中进行声明。
 */

/**
 * @brief 扩展堆大小
 * 
 * 此函数将传入的字进行双字对齐后，将堆的大小扩展，并把旧的brk指针与前面的空闲块合并，返回合并后的空闲块起始地址
 * 
 * @param words 为双字对齐前的扩展的字（4字节）大小
 * @return 按照双字大小扩展brk，并将旧的brk与前一块空闲块合并后的起始地址 失败返回NULL
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    char *list_tail;

    /* 将words进行双字对齐 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    
    /* 扩展堆大小 */
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    /* 保存有效空闲链表尾 bp现在是原来保存前继节点的哨兵空闲链表尾 */
    list_tail = GET_ADDR(bp);
    /* 初始化空闲块头部 尾部 pred succ */
    size_t pre_alloc = GET_PRE_ALLOC(HDRP(bp));
    PUT_VAL(HDRP(bp), PACK(size, pre_alloc, 0));
    PUT_VAL(FTRP(bp), PACK(size, pre_alloc, 0));
    PUT_ADDR(PREDP(bp), list_tail);
    PUT_ADDR(SUCCP(bp), mem_heap_hi());
    /* 更新结尾块和哨兵空闲链表尾 */
    PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK(WSIZE, 0, 1));    
    PUT_ADDR(NEXT_BLKP(bp), bp); 
    return colaesce(bp);
}

/**
 * @brief 合并相邻的空闲内存块
 * 
 * 此函数将传入的指针指向的空闲内存块与相邻的空闲内存块合并，并返回合并后的空闲内存块的起始地址。
 * 合并操作case的情况并不在意pred和succ 因为合并的是地址相邻而不是链表操作 只有当合并完成后，才会更新pred和succ
 * @param ptr 指向待合并的空闲内存块的起始地址的指针
 * @return 合并后的空闲内存块的起始地址
 */
static void *colaesce(void *ptr)
{
    /* 获取当前块的上一个相邻块和下一个相邻块的使用标记位 */
    size_t prev_alloc = GET_PRE_ALLOC(HDRP(ptr));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    // 请求块ptr在堆的起始处或结尾处的特殊情况

    if(prev_alloc && next_alloc) {          /* case1: 前面块和后面块是已分配的 */
        // find_pred(ptr);
        // 更新pred和succ
    }

    /* case2: 前面块已分配 后面块空闲 */
    /* case3: 前面块空闲 后面块已分配 */
    /* case4: 前面块和后面块都空闲 */
    return ptr;
}

// 空闲块合并后还需要一个操作去搜索合适的前驱
static void *find_pred(void *ptr)
{
    return ptr;
}

/**  
 * @brief 寻找合适大小的空闲块
 *  
 * @param size 所需空闲块的大小
 * @return 返回找到的空闲块地址， 失败返回NULL
 */
static void *find_fit(size_t size)
{
    char *bp;
    return bp;
}












