#include "cachelab.h"
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#define RIO_BUFSIZE 8192
#define MACHINE_BIT 64
#define BUFSIZE 16

typedef unsigned long long uint64_t;

typedef struct DListNode {
    uint64_t flag_key;
    char buf[BUFSIZE]; 
    struct DListNode *pre;
    struct DListNode *next;
}d_listNode;

typedef struct DLinkedList {
    uint64_t size;  // 当前链表共有多少个节点(两个哨兵头尾除外)
    struct DListNode *dummy_head;
    struct DListNode *dummy_tail;
    /* 用hash的话 测试用例flag位数最多有62位，HASHMAPSIZE应该为1 << 62
     * struct DListNode *hashTable[HASHMAPSIZE];
     */
}d_linkedList;

typedef struct CacheLists {
    uint64_t capacity;  // 每个链表最多有多少个节点(两个哨兵头尾除外)
    struct DLinkedList *linkedList;
}cache_lists;

typedef struct AddrInfo
{
    uint64_t flag;
    uint64_t s_index;
    uint64_t b_offset;
}addr_info;

void CacheListsInit(cache_lists *cache_addr, uint64_t size, uint64_t capacity); //size为有多少个链表(即共有多少个组S)，capacity为每个链表多少个节点(即每个组共有多设个行E)
void AddListNode(d_linkedList *linkedList, uint64_t capacity, uint64_t value); // 将值为value的添加至链表的dummy_head后
void DelListNode(d_linkedList *linkedList); //将链表dummy_tail前的节点删除
void MoveHead(d_linkedList *linkedList, d_listNode *node);  //将指定节点移至链表头部
d_listNode *findNode(d_linkedList *linkedList, uint64_t value); //查找链表中是否含有key值为value的节点

void LoadOps(cache_lists *myCache, addr_info* addrInfo);
void StoreOps(cache_lists *myCache, addr_info* addrInfo);
void ModifyOps(cache_lists *myCache, addr_info* addrInfo);

void setAddrInfo(uint64_t addr, uint64_t s, uint64_t b, addr_info* addrInfo); // 根据地址 提取相关信息

void printUsage() {
    printf("Usage: ./csim -s <s> -E <E> -b <b> -t <tracefile>\n");
}

int hits = 0;       // 缓存命中数
int misses= 0;     // 缓存未命中数
int evictions= 0;  // 驱逐缓存次数

int main(int argc, char *argv[])
{
    uint64_t s;         //组索引位数
    uint64_t b;         //块偏移位数
    uint64_t E;         //一个高速缓存组有E行
    char *tracefile;
    int opt;
    
    while ((opt = getopt(argc, argv, "s:E:b:t:")) != -1) {
        switch (opt) {
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                tracefile = optarg;
                break;
            default:
                printUsage();
                return 1;
        }
    }

    /* 
     * uint64_t flag = MACHINE_BIT - s - b; //标记位数 可以通过s b计算
     * uint64_t B = 1 << b;    //高速缓存块字节 如果只是模拟统计结果的话可以不用B
    */
    uint64_t S = 1 << s;    //高速缓存组个数
    
    FILE *fp;

    // 一共S组
    cache_lists myCache;
    CacheListsInit(&myCache, S, E);
    
    if((fp = fopen(tracefile, "r")) == NULL)
        printf("error open file");
    
    char line[256]; // 假设每行最多有 255 个字符
    while (fgets(line, sizeof(line), fp) != NULL) {
        if(line[0] == 'I')
            continue;
        char option;
        uint64_t addr;
        uint64_t size;
        sscanf(line, " %c %llx,%llx", &option, &addr, &size);
        //  从addr中提取出flag s_index b_offset(这个变量没用)
        addr_info addrInfo;
        setAddrInfo(addr, s, b, &addrInfo);
        // 根据option判断是什么操作, 根据示例，写策略应该是 写分配(未命中)+写回(命中)策略
        // 组选择 + 行匹配: 若命中，进行字选择（模拟可忽略）更新LRU; 若不命中则进行行替换 更新LRU
        switch (option)
        {
        case 'L':
            LoadOps(&myCache, &addrInfo);
            break;
        case 'S':
            StoreOps(&myCache, &addrInfo);
            break;
        case 'M':
            ModifyOps(&myCache, &addrInfo);       
            break;
        default:
            break;
        }
    }

    printSummary(hits, misses, evictions);
    return 0;
}

void setAddrInfo(uint64_t addr, uint64_t s, uint64_t b, addr_info* addrInfo)
{
    uint64_t b_mask = (1 << b) - 1;
    uint64_t s_mask = (1 << s) - 1;
    uint64_t f_mask = (1 << (MACHINE_BIT - s - b)) - 1;
    addrInfo->b_offset = addr & b_mask;
    addr >>= b;
    addrInfo->s_index = addr & s_mask;
    addr >>= s;
    addrInfo->flag = addr & f_mask;
    return;
}

void CacheListsInit(cache_lists *cache_addr, uint64_t size, uint64_t capacity)
{
    cache_addr->capacity = capacity;
    cache_addr->linkedList = (d_linkedList *)malloc(size * sizeof(d_linkedList));
    for(int i = 0; i < size; i++) {
        cache_addr->linkedList[i].dummy_head = (d_listNode *)malloc(sizeof(d_listNode));
        cache_addr->linkedList[i].dummy_tail = (d_listNode *)malloc(sizeof(d_listNode));
        cache_addr->linkedList[i].dummy_head->pre = NULL;
        cache_addr->linkedList[i].dummy_head->next = cache_addr->linkedList[i].dummy_tail;
        cache_addr->linkedList[i].dummy_tail->pre = cache_addr->linkedList[i].dummy_head;
        cache_addr->linkedList[i].dummy_tail->next = NULL;
        cache_addr->linkedList[i].size = 0;
    }
}

void AddListNode(d_linkedList *linkedList, uint64_t capacity, uint64_t value)
{
    if(linkedList->size == capacity) {
        //先删除再插入
        DelListNode(linkedList);
    }
    d_listNode *newNode = (d_listNode *)malloc(sizeof(d_listNode));
    newNode->flag_key = value;
    newNode->next = linkedList->dummy_head->next;
    newNode->pre = linkedList->dummy_head;
    linkedList->dummy_head->next->pre = newNode;
    linkedList->dummy_head->next = newNode;
    linkedList->size++;
}

void DelListNode(d_linkedList *linkedList)
{
    d_listNode *temp = linkedList->dummy_tail->pre;
    temp->pre->next = temp->next;
    linkedList->dummy_tail->pre = temp->pre;
    free(temp);
    linkedList->size--;
    evictions++;
}

void MoveHead(d_linkedList *linkedList, d_listNode *node)
{
    // 先把要移动的节点摘下来
    node->pre->next = node->next;
    node->next->pre = node->pre;
    // 把要移动的节点放到dummy_head之后
    node->next = linkedList->dummy_head->next;
    node->pre = linkedList->dummy_head;
    linkedList->dummy_head->next->pre = node;
    linkedList->dummy_head->next = node;
}

d_listNode *findNode(d_linkedList *linkedList, uint64_t value)
{
    d_listNode *node_ptr = linkedList->dummy_head->next;
    while(node_ptr != linkedList->dummy_tail) {
        if(node_ptr->flag_key == value) {
            hits++;
            return node_ptr;
        }
        node_ptr = node_ptr->next;
    }
    misses++;
    return NULL;
}

void LoadOps(cache_lists *myCache, addr_info* addrInfo)
{
    d_listNode *node = findNode(&myCache->linkedList[addrInfo->s_index], addrInfo->flag);
    if(node == NULL)
        AddListNode(&myCache->linkedList[addrInfo->s_index], myCache->capacity, addrInfo->flag);
    else
        MoveHead(&myCache->linkedList[addrInfo->s_index], node);
}

void StoreOps(cache_lists *myCache, addr_info* addrInfo)
{
    d_listNode *node = findNode(&myCache->linkedList[addrInfo->s_index], addrInfo->flag);
    if(node == NULL)
        AddListNode(&myCache->linkedList[addrInfo->s_index], myCache->capacity, addrInfo->flag);
    else
        MoveHead(&myCache->linkedList[addrInfo->s_index], node);
}

void ModifyOps(cache_lists *myCache, addr_info* addrInfo)
{
    LoadOps(myCache, addrInfo);
    StoreOps(myCache, addrInfo);
}