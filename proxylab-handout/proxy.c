#include <stdio.h>

#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NTHREADS 4
#define SBUFSIZE 16

void *thread(void *vargp);
void doit(int connfd);    //执行事务
int handle_clientRequest(rio_t *rio);   //  处理来自客户端的请求行+请求头 生成发往服务器的请求行 + 请求头
int handle_requestline(rio_t *rio, char *readClientBuf, char *writeServerBuf);  // 读取客户端的请求行 构建自己的请求行
int handle_requesthdrs(rio_t *rio, char *buf, char *hostname);   // 读取客户端的请求头 构建自己的请求头
int parse_uri(char *uri, char *hostname, char *path);    // 解析uri

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

sbuf_t sbuf;    // 存放已连接描述符的共享缓冲区

    
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
    for(int i = 0; i < NTHREADS; ++i)
        Pthread_create(&tid, NULL, thread, NULL);

    /* 主线程接收客户端的连接 */
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);

        /* 打印客户端信息 */
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
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
    }
}

void doit(int connfd)
{
    // /* 在proxy-server间 proxy作为客户端 */
    // int clientfd;
    // char *host, *port, buf[MAXLINE];
    // rio_t rio_forServer;    // 和服务器之间的rio 绑定在clientfd上
    // int n;  // 保存从rio中的文件缓存处读取了多少字节

    char errorMessage[MAXLINE];

    rio_t rio;

    Rio_readinitb(&rio, connfd);
    if(handle_clientRequest(&rio) == -1) {
        // 这里应该构建http响应报文 暂时先不管这里
        sprintf(errorMessage, "requestline error just support GET!\r\n");
        Rio_writen(connfd, errorMessage,sizeof(errorMessage));
        return;
    }
        

    // /* proxy先初始化连接服务器所需要的信息 */
    // host = "47.109.193.20";
    // port = "7109";
    // clientfd = Open_clientfd(host, port);
    // Rio_readinitb(&rio_forServer, clientfd);

    
    // TODO 转发浏览器请求至服务器 此时应该重新构造请求头?
}

// TODO  看看最后需不需要把buf参数写进函数里
int handle_clientRequest(rio_t *rio)
{
    char readClientBuf[MAXLINE];     // 从connfd中读数据保存至 readClientBuf 中
    char writeServerBuf[MAXLINE];    // 构建请求行 + 请求报头 将其转发至服务端
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];   // 用于保存请求行的 method uri version
    char hostname[MAXLINE], path[MAXLINE];  // 用于保存解析uri后的主机名及其路径

    //  TODO 读取并解析客户端的请求（主机名、路径或查询及之后的内容） 并构建相应的 向服务器转发的请求行与报头
    Rio_readlineb(rio, readClientBuf, MAXLINE);
    sscanf(readClientBuf, "%s %s %s", method, uri, version);
    /* 检查请求行是否正确 目前只支持 GET */
    if(strcmp(method, "GET"))
        return -1;
    printf("method: %s, uri: %s, version: %s\n", method, uri, version);
    /* 从uri中提取主机名 以及 路劲或查询及以后的内容 */
    if(parse_uri(uri, hostname, path) == -1)
        return -1;
    /* 构建转发至服务器的请求行 */
    sprintf(writeServerBuf, "%s %s %s\r\n", method, path, "HTTP/1.0");

    /* 读取客户端的请求头 构建自己的请求头 */
    handle_requesthdrs(rio, writeServerBuf, hostname);
    printf("%s\n", writeServerBuf);
    // TODO 与服务器建立连接 并发送
    
    return 0;
}

int parse_uri(char *uri, char *hostname, char *path)
{
    // example: http://www.cmu.edu/hub/index.html
    char *pHostnameBegin;
    char *pHostnameEnd;
    size_t hostnameLen;

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

    hostnameLen = pHostnameEnd - pHostnameBegin;
    strncpy(hostname, pHostnameBegin, hostnameLen);
    hostname[hostnameLen] = '\0';
    printf("hostname: %s\n", hostname);
    /* 提取path */
    strcpy(path, pHostnameEnd);
    printf("path: %s\n", path);
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
    // TODO 解析请求头并构建转发请求头
    while((n = Rio_readlineb(rio, readClientBuf, MAXLINE)) != 0) { 
        printf("recvie_byte: %d, content: %s\n", n, readClientBuf);
        if(strstr(readClientBuf, "Host") || strstr(readClientBuf, "User-Agent") || \
            strstr(readClientBuf, "Connetion") || strstr(readClientBuf, "Proxy-Connection")) {
            continue;
        } else {
            sprintf(buf, "%s%s", buf, readClientBuf);
        }
        if(!strcasecmp(readClientBuf, "\r\n")) {
            break;
        }
            
    }
    return 0;
}