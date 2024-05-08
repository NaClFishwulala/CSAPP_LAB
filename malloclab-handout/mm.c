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

/**
 * 存储结构： 隐式空闲链表、显式空闲链表、分离空闲链表
 * 放置策略： 首次适配、下一次适配、最佳适配、分离适配
 */

/**
 * 存储结构设计：显式空闲链表（分配块 与 带边界标记的空闲块）
 *  * 空闲块(最小为4字)：
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
 * 分配块(理论最小为2字， 但考虑到free以后变为空闲块，分配块也应该为4字)：
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
 *

 * 
 * 堆结构(字节对齐指有效数据的起始地址应该是8字节的倍数)
 * |    a word    |            a word            |        a word        |  a word  | 3 words |words|a word|  3 words |       a word         |
 * |--------------|------------------------------|----------------------|----------|---------|-----|------|----------|----------------------|
 * |  双字对齐填充 | 哨兵空闲链表头(pred字段无实意) |哨兵空闲链表头(succ字段)|  序言块头 |序言块填充|....|结尾块头|结尾块填充|哨兵空闲链表尾(pred字段)|
 * 
 * 空闲链表排序策略：地址顺序
 * 分配块的策略：首次适配
 * 释放和合并策略：带边界标记的立即合并
 */

/* 以下为操作空闲链表的基本常数和宏 */
#define WSIZE 4 /* 字 头部/尾部大小 4字节 */
#define DSIZE 8 /* 双字 8字节 */
#define CHUNKSIZE (1 << 12) /* 每次堆扩展的字节 */ 
#define DUMMY_HEAD_SIZE DSIZE   
#define DUMMY_TAIL_SIZE WSIZE
#define PADDING_SIZE WSIZE  /* dummy_head前的填充字节 */
#define MIN_BLOCK_SIZE 4 * WSIZE /* 最小块大小要求为4字节 */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

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

/* 从地址p处设置或清除alloc/pre_alloc位 */
#define SET_ALLOC(p) (GET_VAL(p) |= 0x01)
#define CLEAR_ALLOC(p) (GET_VAL(p) &= ~0x01)
#define SET_PRE_ALLOC(p) (GET_VAL(p) |= 0x02)
#define CLEAR_PRE_ALLOC(p) (GET_VAL(p) &= ~0x02)

/* 地址p处的size值增加或减少val */
#define INC_SIZE(p, val) (GET_VAL(p) += (val))
#define DEC_SIZE(p, val) (GET_VAL(p) -= (val))

/* 给定一个块指针bp, 计算该块的头地址/尾地址/pred地址/succ地址 */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)    /* 已分配块没有尾地址 */
#define PREDP(bp) ((char *)(bp))
#define SUCCP(bp) ((char *)(bp) + WSIZE)

/* 给定一个块指针bp, 计算上一个块或下一个块的地址 */
#define PREV_BLKP(bp) ((char *)bp + GET_SIZE((char *)bp - DSIZE)) /* 只有上一个块是空闲块时, 才有存储该块信息的尾部 */ 
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* 以下为空闲链表相关宏 */
#define GET_DUMMY_HEAD ((char *)mem_heap_lo() + PADDING_SIZE) /* 获取空闲链表哨兵头部 */
#define GET_DUMMY_TAIL ((char *)mem_heap_hi() - DUMMY_TAIL_SIZE + 1)  /* 获取空闲链表哨兵尾部 */
#define GET_LIST_SUCC(bp) GET_ADDR(SUCCP(bp))
#define GET_LIST_PRED(bp) GET_ADDR(PREDP(bp))

/* 获取序言块以及结尾块地址 */
#define GET_BEGIN_BLOCK (GET_DUMMY_HEAD + DUMMY_HEAD_SIZE + WSIZE)
#define GET_END_BLOCK (GET_DUMMY_TAIL - MIN_BLOCK_SIZE + WSIZE)

/* 以下为辅助函数的声明 */
static void *colaesce(void *ptr);
static void *extend_heap(size_t words);
static void *find_pred(void *ptr);
static void *find_fit(size_t size);
static void place(char *ptr, size_t asize);
int mm_check(void);
/* 
 * mm_init - initialize the malloc package.
 * 成功返回0 失败返回-1
 */
int mm_init(void)
{
    printf("mm_init\n");
    char *heap_listp; /* 堆指针 */
    /* 8字节对齐指有效数据的起始地址应该是8字节的倍数 */
    /* 初始化一个空的堆 对齐填充(1字) + 哨兵空闲链表头(2字) + 序言块(4字) + 结尾块(4字) + 哨兵空闲链表尾(1字) */
    if((heap_listp = mem_sbrk(12 * WSIZE)) == (void *)-1)    /* mem_sbrk通过将内核的brk指针增加incr来扩展和收缩堆。如果成功返回旧值，失败返回-1 */
        return -1;
    PUT_VAL(heap_listp , 0); /* 对齐填充 */
    PUT_ADDR(heap_listp + PADDING_SIZE, NULL); /* 哨兵空闲链表头pred字段 无实意 */
    PUT_ADDR(heap_listp + PADDING_SIZE + WSIZE, GET_DUMMY_TAIL); /* 哨兵空闲链表头succ字段 存放下一个空闲块的地址 */
    /* 序言块和结尾块用于减少合并时的边界判断 */
    PUT_VAL(heap_listp + PADDING_SIZE + DUMMY_HEAD_SIZE, PACK(MIN_BLOCK_SIZE, 1, 1));
    // 
    // char *begin_bp = GET_BEGIN_BLOCK;
    // printf("begin_bp: %p size: %u pre_alloc_bit:%d alloc_bit:%d\n", 
    //         begin_bp, GET_SIZE(HDRP(begin_bp)), GET_PRE_ALLOC(HDRP(begin_bp)), GET_ALLOC(HDRP(begin_bp)));
    
    PUT_VAL(heap_listp + PADDING_SIZE + DUMMY_HEAD_SIZE + MIN_BLOCK_SIZE, PACK(MIN_BLOCK_SIZE, 1, 1));
    PUT_ADDR(heap_listp + PADDING_SIZE + DUMMY_HEAD_SIZE + 2 * MIN_BLOCK_SIZE, GET_DUMMY_HEAD); /* 哨兵空闲链表尾pred字段 存放上一个空闲块的地址 */

    mm_check();
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
    size_t asize;   /* 对齐后的块大小 */
    size_t extendsize;
    char *bp;

    if(size == 0)
        return NULL;
    /* 用户请求的空间并不包含头尾 分配的要带上一字的头 */
    size += WSIZE;
    /* 保证最小块为16字节， 向上双字取整 */
    if(size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE - 1)) / DSIZE);
    printf("mm_malloc user need size: %u, after add head size: %u asize: %u\n",size - WSIZE, size, asize); 
    mm_check();
    /* 首次适配，寻找合适的空闲块 */
    if((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        mm_check();
        return bp;
    }

    /* 没找到适配的空闲块 申请更多的内存来分配块 */
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    mm_check();
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    printf("mm_free\n");
    /* 释放后已分配块变为空闲块: 更改头部、添加尾部 */
    CLEAR_ALLOC(HDRP(ptr));
    PUT_VAL(FTRP(ptr), GET_VAL(HDRP(ptr)));
    /* 把下一个块的pre_alloc位值1 */
    SET_PRE_ALLOC(NEXT_BLKP(ptr));
    /* 在colaesce添加pred succ */
    colaesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    printf("mm_realloc\n");
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
    char *pred_ptr;
    char *dummy_tail;
    size_t size;

    dummy_tail = GET_DUMMY_TAIL;    /* 保存旧的dummy_tail */
    pred_ptr = GET_LIST_PRED(dummy_tail);   /* 保存旧dummy_tail的pred节点 */
    bp = GET_END_BLOCK;
    /* 将words进行双字对齐 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    /* 扩展堆大小 */
    if((long)(mem_sbrk(size)) == -1)   
        return NULL;
    printf("extend_heap size:%u bp:%p\n", size, bp);
    dummy_tail = GET_DUMMY_TAIL;    /* mem_sbrk后更新dummy_tail */
    /* 由于dummy_tail更新了 因此要更新pred节点的succ以及 新dummy_tail的pred */
    PUT_ADDR(SUCCP(pred_ptr), dummy_tail);
    PUT_ADDR(PREDP(dummy_tail), pred_ptr);
    /* 初始化空闲块头部/尾部 字段pred和succ在colaesce中更新 */
    size_t pre_alloc = GET_PRE_ALLOC(HDRP(bp));
    PUT_VAL(HDRP(bp), PACK(size, pre_alloc, 0));
    PUT_VAL(FTRP(bp), PACK(size, pre_alloc, 0));

    /* 更新结尾块 */
    PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK(MIN_BLOCK_SIZE, 0, 1));
    mm_check();
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
    printf("colaesce ptr:%p\n", ptr);
    /* 获取当前块的上一个相邻块和下一个相邻块的使用标记位 */
    size_t prev_alloc = GET_PRE_ALLOC(HDRP(ptr));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    char *pred_ptr = NULL;  /* 空闲链表的前继 */
    char *succ_ptr = NULL;  /* 空闲链表的后继 */
    
    

    if(prev_alloc && next_alloc) {          /* case1: 前面块和后面块是已分配的 */
        printf("case1 prev_alloc_bit:%d next_alloc_bit: %d\n", prev_alloc, next_alloc);
        if((pred_ptr = find_pred(ptr)) == NULL) /* 因为有哨兵空闲链表头存在，所以一定能找到，找不到说明错了 */
            return NULL;
        succ_ptr = GET_LIST_SUCC(pred_ptr); // pred现在指的不是succ_ptr
        PUT_ADDR(PREDP(ptr), pred_ptr);
        PUT_ADDR(SUCCP(ptr), succ_ptr);

        /* 更新前继空闲块的succ字段和后继空闲块的pred字段 */
        PUT_ADDR(SUCCP(pred_ptr), ptr);
        PUT_ADDR(PREDP(succ_ptr), ptr);
    }

    else if(prev_alloc && !next_alloc) {    /* case2: 前面块已分配 后面块空闲 */
        printf("case2 prev_alloc_bit:%d next_alloc_bit: %d\n", prev_alloc, next_alloc);
        /* 获取后一个块 */
        char *next_blkp = NEXT_BLKP(ptr);
        /* 从后面块获取前继节点和后继节点 */
        pred_ptr = GET_ADDR(PREDP(next_blkp));
        succ_ptr = GET_ADDR(FTRP(next_blkp));
        /* 更新头部大小和 将头部数据复制到新的尾部 */
        INC_SIZE(HDRP(ptr), GET_SIZE(HDRP(next_blkp)));
        PUT_VAL(FTRP(ptr), GET_VAL(HDRP(ptr)));
        /* 添加pred和succ字段 */
        PUT_ADDR(PREDP(ptr), pred_ptr);
        PUT_ADDR(SUCCP(ptr), succ_ptr);

        /* 更新前继节点的succ 更新后继节点的pred */
        PUT_ADDR(SUCCP(pred_ptr), ptr);
        PUT_ADDR(PREDP(succ_ptr), ptr);
    }
    
    else if(!prev_alloc && next_alloc) {    /* case3: 前面块空闲 后面块已分配 */
        printf("case3 prev_alloc_bit:%d next_alloc_bit: %d\n", prev_alloc, next_alloc);
        /* 获取前一个块 */
        char *prev_ptr = PREV_BLKP(ptr);
        /* 前继后继保持不变 更改头部大小和尾部大小即可 */
        INC_SIZE(HDRP(prev_ptr), GET_SIZE(HDRP(ptr)));
        PUT_VAL(FTRP(prev_ptr), GET_VAL(HDRP(prev_ptr)));
        ptr =  prev_ptr;
    }
    
    else if(!prev_alloc && !next_alloc) {   /* case4: 前面块和后面块都空闲 */
        printf("case4 prev_alloc_bit:%d next_alloc_bit: %d\n", prev_alloc, next_alloc);
        /* 获取合并后该块的前继节点 */
        char *prev_ptr = PREV_BLKP(ptr);
        char *next_blkp = NEXT_BLKP(ptr);
        /* 更改头部大小和尾部大小 */
        size_t inc_size = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(next_blkp));
        INC_SIZE(HDRP(prev_ptr), inc_size);
        PUT_VAL(FTRP(prev_ptr), GET_VAL(HDRP(prev_ptr)));
        /* 合并后的pred不无需更改 succ需要变为next_blkp的succ */
        succ_ptr = GET_LIST_SUCC(next_blkp);
        PUT_ADDR(SUCCP(prev_ptr), succ_ptr);

        /* 更新后继节点的pred */
        PUT_ADDR(PREDP(succ_ptr), prev_ptr);
        ptr =  prev_ptr;
    }
    // printf("after colaesce ptr: %p\n", ptr);
    mm_check();
    return ptr;
}

/** 
 * @brief 合并时相邻两个块均不空闲时，需要遍历整个空闲链表找到前继指针
 * 
 * @param ptr 需要寻找前继的块指针
 * 
 * @return 找到的前继指针
 */
static void *find_pred(void *ptr)
{
    // printf("find_pred\n");
    char *cur_bp = GET_DUMMY_HEAD;
    char *succ_bp = GET_LIST_SUCC(cur_bp);
    while(succ_bp != GET_DUMMY_TAIL) {
        // printf("cur_bp: %p succ_bp: %p GET_DUMMY_TAIL: %p\n", cur_bp, succ_bp, GET_DUMMY_TAIL);
        if(succ_bp >= (char *)ptr)
            break;
        cur_bp = succ_bp;
        succ_bp = GET_LIST_SUCC(cur_bp);
    }
    // printf("ptr: %p, its pred: %p in find_pred\n", ptr, cur_bp);
    return cur_bp;
}

/**  
 * @brief 首次适配搜索寻找合适大小的空闲块
 *  
 * @param asize 所需空闲块的大小
 * @return 返回找到的空闲块地址， 失败返回NULL
 */
static void *find_fit(size_t asize)
{
    // printf("find_fit asize:%u\n", asize);
    char *bp = GET_LIST_SUCC(GET_DUMMY_HEAD);
    while(bp != GET_DUMMY_TAIL) {
        if(GET_SIZE(HDRP(bp)) >= asize)
            return bp;
        bp = GET_LIST_SUCC(bp);
    }
    return NULL;
}

/**  
 * @brief 将请求块放置在空闲块的起始位置, 只有当剩余部分的大小等于或者超出最小块的大小时, 才进行分割
 *  
 * @param ptr 分配空间的块地址
 * @param asize 所需空间大小
 */
static void place(char *ptr, size_t asize)
{
    // printf("place asize:%u\n", asize);
    size_t surplus_size = GET_SIZE(HDRP(ptr)) - asize; /* 剩余块的大小 */
    char* pred_ptr = GET_LIST_PRED(ptr);
    char* succ_ptr = GET_LIST_SUCC(ptr);
    
    printf("place_func surplus_size:%u\n", surplus_size);
    SET_ALLOC(HDRP(ptr));   /* 更改ALLOC位,将空闲块转换成分配块 */
    /* 剩余大小如果小于最小块大小 则使用该空闲块全部空间 */
    if(surplus_size < MIN_BLOCK_SIZE) {
        /* 更新空闲链表 */
        PUT_ADDR(SUCCP(pred_ptr), succ_ptr);
        PUT_ADDR(PREDP(succ_ptr), pred_ptr);
        return;
    } 

    /* 当剩余部分的大小等于或者超出最小块的大小时, 进行分割 */
    DEC_SIZE(HDRP(ptr), surplus_size);  /* 更新头部大小 */
    /* 更新剩余的空闲块 */
    char *free_ptr = NEXT_BLKP(ptr);
    // printf("dummy_head:%p dummy_tail:%p surplus_size:%u\n", GET_DUMMY_HEAD, GET_DUMMY_TAIL, surplus_size);
    // printf("pred_ptr:%p ptr:%p free_ptr: %p succ_ptr:%p\n", pred_ptr, ptr, free_ptr, succ_ptr);
    PUT_VAL(HDRP(free_ptr), PACK(surplus_size, 1, 0));
    PUT_VAL(FTRP(free_ptr), PACK(surplus_size, 1, 0));
    PUT_ADDR(PREDP(free_ptr), pred_ptr);
    PUT_ADDR(SUCCP(free_ptr), succ_ptr);
    /* 更新空闲链表 */
    PUT_ADDR((SUCCP(pred_ptr)), free_ptr);
    PUT_ADDR(PREDP(succ_ptr), free_ptr);
    
    return;
}

/** 
 * @brief 
 * 1. 检查空闲链表的每个指针(pred succ)是否都指向有效空闲块
 * 2. 检查每个空闲块是否都被标记为free
 * 3. 检查是否有任何连续的空闲块以某种方式逃脱了合并
 * 4. 检查每个空闲块是否都在空闲链表中
 * 5. 堆块中的指针是否指向有效地址
 */
int mm_check(void)
{
    // TODO 任何分配的块是否重叠
    
    printf("mm_check begin\n");
    char *dummy_head = GET_DUMMY_HEAD;
    char *dummy_tail = GET_DUMMY_TAIL;
    char *begin_bp = GET_BEGIN_BLOCK;
    char *end_bp = GET_END_BLOCK;
    printf("dummy_head: %p, begin_bp: %p, end_bp: %p, dummy_tail: %p\n", dummy_head, begin_bp, end_bp, dummy_tail);

    /* 检查dummy_head的succ和dummy_tail的pred是否有效 */
    if(GET_LIST_SUCC(dummy_head) <= begin_bp || GET_LIST_SUCC(dummy_head) > dummy_tail || \
        GET_LIST_PRED(dummy_tail) < dummy_head || GET_LIST_PRED(dummy_tail) >= end_bp) { 
        printf("invaild pred or succ in dummy\n");
        return 0;
    }

    char *pred_free_ptr = dummy_head;   /* 上一个空闲块指针 */
    if(GET_LIST_SUCC(dummy_head) < dummy_head || GET_LIST_SUCC(dummy_head) > dummy_tail) {  /* dummy_head的succ是否有效 */
        printf("invaild dummy_head's succ_ptr\n");
        return 0;
    }
    char *cur_free_ptr = GET_LIST_SUCC(dummy_head); /* 当前空闲块指针 */
    char *cur_bp = begin_bp;   /* 当前块指针 */

    // 此处只能检查到序言块到dummy_tail前的块
    // TODO 新的思路: 最外层按照相邻块循环， 当循环到alloc为0时 判断该块是否在空闲链表中 可通过两次遍历的空闲块值和空闲链表的有效节点数每个空闲块是否都在空闲链表中
    while(cur_free_ptr != dummy_tail) {
        while(cur_bp != cur_free_ptr) { /* 对分配块的检查 */
            if(cur_bp <= dummy_head || cur_bp >= dummy_tail) {  /* 当前块是否有效 */
                printf("!!! invaild cur_bp: %p\n", cur_bp);
                return 0;
            }
            printf("cur_bp: %p size: %u pre_alloc_bit:%d alloc_bit:%d\n", \
                cur_bp, GET_SIZE(HDRP(cur_bp)), GET_PRE_ALLOC(HDRP(cur_bp)), GET_ALLOC(HDRP(cur_bp)));
            if(GET_ALLOC(HDRP(cur_bp)) == 0) {  /* 每个空闲块是否都在空闲链表中 */
                printf("!!! block is free, but not in links\n");
                return 0;
            }
            cur_bp = NEXT_BLKP(cur_bp);
        }
        /* 对空闲链表的检查 */
        printf("cur_free_ptr: %p head_size: %u head_pre_alloc_bit:%d head_alloc_bit:%d foot_size: %u foot_pre_alloc_bit:%d foot_alloc_bit:%d\n", \
                cur_free_ptr, GET_SIZE(HDRP(cur_free_ptr)), GET_PRE_ALLOC(HDRP(cur_free_ptr)), GET_ALLOC(HDRP(cur_free_ptr)), \
                GET_SIZE(FTRP(cur_free_ptr)), GET_PRE_ALLOC(FTRP(cur_free_ptr)), GET_ALLOC(FTRP(cur_free_ptr)));
        // printf("GET_VAL(HDRP(cur_free_ptr)): %u\n", GET_VAL(HDRP(cur_free_ptr)));
        if(GET_LIST_SUCC(cur_free_ptr) < dummy_head || GET_LIST_SUCC(cur_free_ptr) > dummy_tail) {  /* 当前块的succ是否有效 */
            printf("!!! invaild succ_ptr\n");
            return 0;
        }
        if(GET_LIST_PRED(cur_free_ptr) < dummy_head || GET_LIST_PRED(cur_free_ptr) > dummy_tail) {  /* 当前块的succ是否有效 */
            printf("!!! invaild pred_ptr\n");
            return 0;
        }
        if(GET_LIST_PRED(cur_free_ptr) != pred_free_ptr) {  /* 检查cur_free_ptr的pred是否是pred_free_ptr */
            printf("!!! pred error\n");
            return 0;
        }
        if(GET_ALLOC(HDRP(cur_free_ptr)) != 0) {  /* 是否每个空闲块都被标记为free */
            printf("!!! free block but alloc_bit is %d, not 0\n", GET_ALLOC(HDRP(cur_free_ptr)));
            return 0;
        }
        if(GET_PRE_ALLOC(HDRP(cur_free_ptr)) != 1) {  /* 否有任何连续的空闲块以某种方式逃脱了合并 */
            printf("!!! free block pre_alloc_bit is %d, not 1 neet to colaesce\n", GET_PRE_ALLOC(HDRP(cur_free_ptr)));
            return 0;
        }

        pred_free_ptr = cur_free_ptr;
        cur_free_ptr = GET_LIST_SUCC(cur_free_ptr);
        cur_bp = NEXT_BLKP(cur_bp);
    }
    if(GET_LIST_PRED(cur_free_ptr) < dummy_head || GET_LIST_PRED(cur_free_ptr) > dummy_tail) {  /* dummy_tail的pred是否有效 */
        printf("!!! invaild dummy_tail's pred_ptr\n");
        return 0;
    }
    printf("mm_check end\n");
    return 1;
}








