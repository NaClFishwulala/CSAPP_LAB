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
    int n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    //  TODO 读取并解析客户端的请求（主机名、路径或查询及之后的内容）
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("proxy received %d bytes on fd: %d , content: %s", n, connfd, buf);
        if(!strcasecmp(buf, "\r\n"))
            break;
    }


    // /* proxy先初始化连接服务器所需要的信息 */
    // host = "47.109.193.20";
    // port = "7109";
    // clientfd = Open_clientfd(host, port);
    // Rio_readinitb(&rio_forServer, clientfd);

    
    // TODO 转发浏览器请求至服务器 此时应该重新构造请求头?
}
