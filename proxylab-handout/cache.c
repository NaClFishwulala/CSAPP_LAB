#include "cache.h"

void InitCache(CacheList *cacheList) {
    /* 设置有关共享内容的相关变量 */
    cacheList->readCount = 0;
    Sem_init(&cacheList->mutexReadCount, 0, 1);
    Sem_init(&cacheList->mutexCache, 0, 1);
    /* 初始化共享缓存空间 */
    cacheList->totalSize = 0;
    cacheList->dummyHead = (CacheNode *)malloc(sizeof(CacheNode));
    cacheList->dummuTail = (CacheNode *)malloc(sizeof(CacheNode));
    cacheList->dummyHead->pre = NULL;
    cacheList->dummyHead->next = cacheList->dummuTail;
    cacheList->dummuTail->pre = cacheList->dummyHead;
    cacheList->dummuTail->next = NULL;
}

CacheNode *ReadCache(CacheList *cacheList, char *uri) {
    P(&cacheList->mutexReadCount);
    ++(cacheList->readCount);
    if(cacheList->readCount == 1)
        P(&cacheList->mutexCache);
    V(&cacheList->mutexReadCount);

    CacheNode *cacheNode = FindCacheNode(cacheList, uri);

    P(&cacheList->mutexReadCount);
    --cacheList->readCount;
    if(cacheList->readCount == 0)
        V(&cacheList->mutexCache);
    V(&cacheList->mutexReadCount);
    return cacheNode;
}

void WriteCache(CacheList *cacheList, CacheNode *cacheInfo) {
    P(&cacheList->mutexCache);
    while((MAX_CACHE_SIZE - cacheList->totalSize) < cacheInfo->cacheResponseBodySize) // 判断是否需要删除节点
        DelCacheNode(cacheList);
    AddCacheNode(cacheList, cacheInfo);
    V(&cacheList->mutexCache);
}

CacheNode *FindCacheNode(CacheList *cacheList, char *uri) {
    CacheNode *ptr = cacheList->dummyHead;
    while(ptr != cacheList->dummuTail) {
        if(strcmp(ptr->uri, uri) == 0) {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

void DelCacheNode(CacheList *cacheList) {
    CacheNode *delNode = cacheList->dummuTail->pre;
    cacheList->dummuTail->pre = delNode->pre;
    delNode->pre->next = cacheList->dummuTail;
    size_t delSize = delNode->cacheResponseBodySize;
    cacheList->totalSize -= delSize;
    free(delNode);
}   

void AddCacheNode(CacheList *cacheList, CacheNode *cacheInfo) {
    cacheInfo->pre = cacheList->dummyHead;
    cacheInfo->next = cacheList->dummyHead->next;
    cacheList->dummyHead->next->pre = cacheInfo;
    cacheList->dummyHead->next = cacheInfo;
    cacheList->totalSize += cacheInfo->cacheResponseBodySize;
}