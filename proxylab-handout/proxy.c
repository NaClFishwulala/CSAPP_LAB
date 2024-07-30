#include <stdio.h>

#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

#define NTHREADS 4
#define SBUFSIZE 16

void *thread(void *vargp);
void doit(int connfd);    //执行事务
int handle_requesthdrs(rio_t *rio, char *buf, char *hostname);   // 读取客户端的请求头 构建自己的请求头
size_t handle_serverResponse(rio_t *serverRio, char *readServerBuf);   // 连接请求的服务器 向其发送代理构建的http请求 接收并转发http响应 
int parse_uri(char *uri, char *hostname, char *port, char *path);    // 解析uri
void ProxyResponse(int connfd, char *statusCode, char *message, size_t messageSize);    // 用于代理服务器成功找到缓存或发送出错消息直接给客户端

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

sbuf_t sbuf;    // 存放已连接描述符的共享缓冲区
CacheList m_cacheList;  // 共享缓存区

    
/*  1. 接收浏览器请求
    2. 转发浏览器请求至服务器
    3. 接收服务器数据
    4. 将服务器数据缓存并转发给请求的浏览器 */
int main(int argc, char **argv)
{
    /* 在client-proxy间 proxy作为服务器 */
    int listenfd, connfd;   // 声明侦听描述符和连接描述符
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // 保持代码协议的无关性
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    if(argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    /* client-proxy间 proxy作为服务器 初始化相关信息 */
    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    InitCache(&m_cacheList);
    for(int i = 0; i < NTHREADS; ++i)
        Pthread_create(&tid, NULL, thread, NULL);

    /* 主线程接收客户端的连接 */
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* 打印客户端信息 */
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to connfd: %d(%s, %s)\n",connfd, client_hostname, client_port);

        sbuf_insert(&sbuf, connfd);
        fflush(stdout);
    }
    exit(0);
    return 0;
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while(1) {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
        printf("Close connfd: %d\n", connfd);
        fflush(stdout);
    }
}

void doit(int connfd)
{
    rio_t clientRio;    // 读客户端fd中缓存
    rio_t serverRio;    // 读服务端fd中缓存
    int proxyfd;
    char readClientBuf[MAXLINE];    // 来自客户端的缓存
    char readServerBuf[MAXLINE];    // 来自服务器的响应行+头部 透传直接返回给客户端 不需要toClientBuf
    char *responseBody; // 响应体
    size_t responseBodySize;    
    CacheNode *m_cacheNode;
    char toServerBuf[MAXLINE];  // 保存发往服务器的缓存
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];   // 用于保存请求行的 method uri version
    char path[MAXLINE];  // 用于保存解析uri后的主机名及其路径
    char hostname[MAXLINE], port[MAXLINE];

    /* 处理客户端请求 */
    Rio_readinitb(&clientRio, connfd);  // 将已连接描述符和clientRio的读缓存区联系起来
    Rio_readlineb(&clientRio, readClientBuf, MAXLINE);  //读取请求行 
    sscanf(readClientBuf, "%s %s %s", method, uri, version);    // 提取请求行的相关信息
    /* 检查请求行是否正确 目前只支持 GET */
    if(strcmp(method, "GET"))
        return;
    /* 从uri中提取主机名 以及 路劲或查询及以后的内容 */
    if(parse_uri(uri, hostname, port, path) == -1)
        return;
    printf("hostname: %s, port: %s, path: %s\n", hostname, port, path);
    fflush(stdout);
    /* 查缓存 如果找到则直接响应 否则向服务器请求*/
    CacheNode *cacheNode = ReadCache(&m_cacheList, uri);
    if(cacheNode != NULL) { 
        Rio_writen(connfd, cacheNode->responseMeta, strlen(cacheNode->responseMeta));
        Rio_writen(connfd, cacheNode->cacheResponseBody, cacheNode->cacheResponseBodySize);
        // ProxyResponse(connfd, "200", cacheNode->cacheResponseBody, cacheNode->cacheResponseBodySize);
        printf("find cache!\n");
        fflush(stdout);
        return;
    } 

    /* 构建转发至服务器的请求行 */
    sprintf(toServerBuf, "%s %s %s\r\n", method, path, "HTTP/1.0");
    /* 读取客户端的请求头 构建自己的请求头 */
    handle_requesthdrs(&clientRio, toServerBuf, hostname);
 
    /* 代理与服务器建立连接 并转发重新构建的http请求 */
    proxyfd = Open_clientfd(hostname, port);
    Rio_readinitb(&serverRio, proxyfd);
    Rio_writen(proxyfd, toServerBuf, strlen(toServerBuf));

    // 接收服务器响应
    responseBodySize = handle_serverResponse(&serverRio, readServerBuf);    // readServerBuf 保存响应行 + 响应头

    responseBody = (char *)malloc(responseBodySize * sizeof(char));
    // 读取服务器的响应报头及主体 并转发至客户端
    Rio_readnb(&serverRio, responseBody, responseBodySize); // 不能用逐行读取readlineb参数

    /* 将服务器响应发送至客户端 */
    Rio_writen(connfd, readServerBuf, strlen(readServerBuf));   
    Rio_writen(connfd, responseBody, responseBodySize);

    // 根据fileSize判断是否需要缓存
    if(responseBodySize <= MAX_OBJECT_SIZE) {
        m_cacheNode = (CacheNode *)malloc(sizeof(CacheNode));   // 给m_cacheNode分配空间
        strcpy(m_cacheNode->uri, uri);
        strcpy(m_cacheNode->responseMeta, readServerBuf);
        m_cacheNode->cacheResponseBodySize = responseBodySize;
        m_cacheNode->cacheResponseBody =  (char *)malloc(responseBodySize * sizeof(char));
        memcpy(m_cacheNode->cacheResponseBody, responseBody, responseBodySize);
        WriteCache(&m_cacheList, m_cacheNode);
    }
}

/**
 * @brief 解析给定的URI至hostname port path中
 * 
 * @param uri 需要被解析的uri
 * @param hostname 保存解析结果的hostname
 * @param port 保存解析结果的port 如果uri中没有，则设置成80
 * @param path 保存解析结果的path
 * 
 * @return 成功返回0 失败返回-1
 */
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    // example1: http://www.cmu.edu/hub/index.html
    // example2: http://www.cmu.edu:8080/hub/index.html
    char *pHostnameBegin;
    char *pHostnameEnd; // pHostnameEnd 可能会包含port
    char *pPortBegin;
    size_t hostnameLen;
    size_t portLen = 0;

    // TODO 提取协议部分判断 提高程序健壮性
    /* 提取hostname */
    pHostnameBegin = strstr(uri, "http://");
    if (pHostnameBegin == NULL) 
        return -1;
    else
        pHostnameBegin += strlen("http://");

    pHostnameEnd = strchr(pHostnameBegin, '/');
    if (pHostnameEnd == NULL) 
        return -1;

    pPortBegin = strchr(pHostnameBegin, ':');
    if(pPortBegin == NULL) {
        strcpy(port, "80");
        hostnameLen = pHostnameEnd - pHostnameBegin;
    } else {
        hostnameLen = pPortBegin - pHostnameBegin;
        pPortBegin += strlen(":");
        portLen = pHostnameEnd - pPortBegin;
        strncpy(port, pPortBegin, portLen);
        port[portLen] = '\0';
    }

    strncpy(hostname, pHostnameBegin, hostnameLen);
    hostname[hostnameLen] = '\0';
    /* 提取path */
    strcpy(path, pHostnameEnd);
    return 0;
}

int handle_requesthdrs(rio_t *rio, char *buf, char *hostname)
{
    int n;
    char readClientBuf[MAXLINE];
    // 必须有header-name: Host User-Agent Connetion Proxy-Connection
    sprintf(buf, "%sHost: %s\r\n", buf, hostname);
    sprintf(buf, "%sUser-Agent: %s", buf, user_agent_hdr);
    sprintf(buf, "%sConnetion: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    // 解析请求头并构建转发请求头
    while((n = Rio_readlineb(rio, readClientBuf, MAXLINE)) != 0) { 
        printf("connfd: %d %s", rio->rio_fd, readClientBuf);
        fflush(stdout);
        if(strstr(readClientBuf, "Host") || strstr(readClientBuf, "User-Agent") || \
            strstr(readClientBuf, "Connetion") || strstr(readClientBuf, "Proxy-Connection")) {
            continue;
        } else {
            sprintf(buf, "%s%s", buf, readClientBuf);
        }
        if(!strcmp(readClientBuf, "\r\n")) {
            break;
        }
            
    }
    return 0;
}

// readServerBuf用于保存来自服务器的http的响应
// 返回文件大小
size_t handle_serverResponse(rio_t *serverRio, char *readServerBuf) 
{
    char buf[MAXLINE];
    char *pContentLen;
    size_t fileSize = 0;
    int n;

    /* 保存来自服务器的http响应 */
    Rio_readlineb(serverRio, buf, MAXLINE);    // 读响应行
    sprintf(readServerBuf, "%s%s", readServerBuf, buf);
    printf("proxyfd: %d %s", serverRio->rio_fd, buf);
    
    /* 读响应报头 并获取 "Content-length" 字段的大小 */
    while((n = Rio_readlineb(serverRio, buf, MAXLINE)) != 0) {
        if((pContentLen = strstr(buf, "Content-length: ")) != NULL) {
            pContentLen += strlen("Content-length: ");
            fileSize = atoi(pContentLen);
        }
        printf("proxyfd: %d %s", serverRio->rio_fd, buf);
        fflush(stdout);
        sprintf(readServerBuf, "%s%s", readServerBuf, buf);
        if(!strcmp(buf, "\r\n"))
            break;
    }
    return fileSize;
}

void ProxyResponse(int connfd, char *statusCode, char *message, size_t messageSize)
{
    char buf[MAXLINE];

    if(!strcmp(statusCode, "200")) {
        sprintf(buf, "HTTP/1.0 %s OK\r\n", statusCode);
        Rio_writen(connfd, buf, strlen(buf));
        sprintf(buf, "Content-length: %d\r\n\r\n", messageSize);
        Rio_writen(connfd, buf, strlen(buf));
        Rio_writen(connfd, message, messageSize);
    }
    return;
}