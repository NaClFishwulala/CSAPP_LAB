#include "cachelab.h"
#include <stdlib.h>
#include <stdio.h>

#define RIO_BUFSIZE 8192


int main()
{
    unsigned int s = 4;
    unsigned int b = 4;
    unsigned int E = 1;         //一个高速缓存组有E行
    unsigned int flag = 64 - s - b; //标记位大小
    unsigned int B = 1 << b;    //高速缓存块字节
    unsigned int S = 1 << s;    //高速缓存组个数
    unsigned long cacheLineSize = 1 + flag + B; //每个高速缓存行的大小, 如果只是模拟统计结果的话可以不用B

    FILE *fp;
    int hits = 0; 
    int misses = 0;
    int evictions = 0;

    char** p;
    p = (char**)malloc(S * sizeof(char*));
    for(int i = 0; i < S; i++) 
        p[i] = (char*)calloc(cacheLineSize * E, sizeof(char));
    
    if((fp = fopen("traces/yi.trace", "r")) == NULL)
        printf("error open file");
    
    char line[256]; // 假设每行最多有 255 个字符
    while (fgets(line, sizeof(line), fp) != NULL) {
        char option;
        unsigned int addr;
        unsigned int size;
        sscanf(line, " %c %x,%x", &option, &addr, &size);
        //TODO 编写函数完成对缓存的更新
    }

    
    printSummary(hits, misses, evictions);
    return 0;
}
