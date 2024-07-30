#ifndef CACHE_H
#define CACHE_H
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheNode {
    char uri[MAXLINE];  // 缓存的key 目前只对应缓存GET的请求及响应
    char responseMeta[MAXLINE]; // 缓存响应行 + 响应头
    size_t cacheResponseBodySize;
    char *cacheResponseBody;
    struct CacheNode *pre;
    struct CacheNode *next;
}CacheNode;

typedef struct CacheList {
    int readCount;  // 统计当前在临界区的写者数量
    sem_t mutexReadCount;    // 保护共享变量 writeCount
    sem_t mutexCache;    // 保护缓冲区
    size_t totalSize;   // 目前保存的响应body总大小
    CacheNode *dummyHead;
    CacheNode *dummuTail;
}CacheList;



void InitCache(CacheList *cacheList);
CacheNode *ReadCache(CacheList *cacheList, char *uri);   // 读缓存
void WriteCache(CacheList *cacheList, CacheNode *cacheInfo);  // 写缓存

CacheNode *FindCacheNode(CacheList *cacheList, char *uri);
void DelCacheNode(CacheList *cacheList);    // 删除缓存
void AddCacheNode(CacheList *cacheList, CacheNode *cacheInfo);
#endif