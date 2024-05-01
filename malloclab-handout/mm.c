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
 * |  31  ...  3  |  2  |  1  |  0  |
 * |--------------|-----|-----|-----|
 * |     块大小    | Bit | Bit | Bit |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |            有效载荷             |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |            填充                |
 *
 * 块大小: 29位，表示当前块的大小，为8的倍数
 * Bit 2: 1位，保留
 * Bit 1: 1位，0表示上个块空闲， 1表示上个块已分配
 * Bit 0: 1位，0表示当前块空闲， 1表示当前块已分配

 * 空闲块：
 * |  31  ...  3  |  2  |  1  |  0  |
 * |--------------|-----|-----|-----|
 * |     块大小    | Bit | Bit | Bit |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |             pred               |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |             succ               |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |            有效载荷             |
 * |--------------------------------|
 * |          31  ...  0            |
 * |--------------------------------|
 * |             填充               |
 * |--------------------------------|
 * |  31  ...  3  |  2  |  1  |  0  |
 * |--------------|-----|-----|-----|
 * |     块大小    | Bit | Bit | Bit |
 * 
 * 块大小: 29位，表示当前块的大小，为8的倍数
 * Bit 2: 1位，保留
 * Bit 1: 1位，0表示上个块空闲， 1表示上个块已分配
 * Bit 0: 1位，0表示当前块空闲， 1表示当前块已分配
 * pred: 指向上一个空闲块 
 * succ: 指向下一个空闲块
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
#define PACK(size, pre_alloc, alloc) ((size) | (pre_alloc << 1) | alloc)

/* 在地址p处读或写一个字 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 从地址p处读取块大小、前一个块的分配位或者当前块的分配位 */
#define GET_SIZE(p) (GET(p) & ~0x07)
#define GET_PRE_ALLOC(p) ((GET(p) & 0x02) >> 1)
#define GET_ALLOC(p) (GET(p) & 0x01)

/* 给定一个块指针bp, 计算该块的头地址或尾地址 */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)    /* 只有空闲块有尾地址 */

/* 给定一个空闲块块指针bp, 计算该空闲块的pred或succ地址 */
#define PREDP(bp) ((char *)(bp) + WSIZE)
#define SUCCP(bp) ((char *)(bp) + 2 * WSIZE)

/* 给定一个块指针bp, 计算下一个块的地址 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
// 是否需要计算上一个块的头地址 ?


/* 以下为空闲链表相关标量 */
char *heap_listp; /* 空闲链表指针 */

/* 以下为辅助函数的声明 */
static void *colaesce(void *ptr);
static void *extend_heap(size_t words);
static void *find_fit(size_t size);

/* 
 * mm_init - initialize the malloc package.
 * 成功返回0 失败返回-1
 */
int mm_init(void)
{
    /* 初始化一个空的堆 哨兵头块和哨兵尾块各占一字 */
    if((heap_listp = mem_sbrk(2 * WSIZE)) == (void *)-1)    /* mem_sbrk通过将内核的brk指针增加incr来扩展和收缩堆。如果成功返回旧值，失败返回-1 */
        return -1;
    PUT(heap_listp, PACK(WSIZE, 0, 1)); /* 哨兵头块 */
    PUT(heap_listp + WSIZE, PACK(WSIZE, 0, 1)); /* 哨兵尾块 */
    heap_listp += WSIZE;

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
    /* 将words进行双字对齐 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    /* 扩展堆大小 */
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    
    /* 初始化空闲块头部 尾部 pred succ */
    PUT(HDRP(bp), PACK(size, 1, 0));
    PUT(FTRP(bp), PACK(size, 1, 0));
    PUT(PREDP(bp), (unsigned int)NULL);
    PUT(SUCCP(bp), (unsigned int)NULL);
    PUT(NEXT_BLKP(bp), PACK(WSIZE, 0, 1));
    return colaesce(bp);
}

/**
 * @brief 合并相邻的空闲内存块
 * 
 * 此函数将传入的指针指向的空闲内存块与相邻的空闲内存块合并，并返回合并后的空闲内存块的起始地址。
 * 
 * @param ptr 指向待合并的空闲内存块的起始地址的指针
 * @return 合并后的空闲内存块的起始地址
 */
static void *colaesce(void *ptr)
{
    /* case1: 前面块和后面块是已分配的 */
    /* case2: 前面块已分配 后面块空闲 */
    /* case3: 前面块空闲 后面块已分配 */
    /* case4: 前面块和后面块都空闲 */
    return ptr;
}

static void *find_fit(size_t size)
{
    char *bp;
    return bp;
}












