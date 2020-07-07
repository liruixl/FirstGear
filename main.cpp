
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <cassert>

#include <sys/socket.h>
#include <sys/epoll.h>

#include "./log/log.h"
#include "./lst_timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

#define SYNLOG  //同步写日志
//#define ASYNLOG //异步写日志

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//http_conn.cpp中定义
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

static int pipefd[2];
static sort_timer_lst timer_list;
//全局内核事件表
static int epollfd = 0;

/*
    信号处理函数
    基础API: send 管道
*/
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}


/*
    设置信号函数
    基础API：struct sigaction, sigaction函数
*/
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask); //信号集是干嘛的，sig不就是信号嘛
    //成功返回0，失败返回-1
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*
    定时处理任务
*/
void timer_handler()
{
    timer_list.tick();
    alarm(TIMESLOT); //__seconds
}

/*
    定时器回调含函数 void (*cb_func)(client_data* )
    删除注册事件，并关闭
*/
void cb_fun(client_data* user_data)
{
    int connfd = user_data->sockfd;
    epoll_ctl(epollfd, EPOLL_CTL_DEL, connfd, 0);
    assert(user_data);
    close(connfd);
    http_conn::m_user_count--;

    char * ipstr = inet_ntoa(user_data->address.sin_addr);
    LOG_INFO("close fd %d : %s", connfd, ipstr);
    Log::get_instance()->flush();
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[])
{
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog.txt",0,2000,80000,8);
#endif
#ifdef SYNLOG
    Log::get_instance()->init("ServerLog.txt",0,2000,80000,0);
#endif

    if(argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    addsig(SIGPIPE, SIG_IGN);

    //创建数据库连接池
    SqlConnPool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "lirui", "youshuang", 3306, 8);

    //创建线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }

    http_conn *user_http_conns = new http_conn[MAX_FD];
    assert(user_http_conns);
    user_http_conns->initmysql_result(connPool);

    //网络编程基础步骤
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    //inet_aton(ip, &address.sin_addr);
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR. &flag, sizeof(flag));
    
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    //epoll内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);  // 1
    http_conn::m_epollfd = epollfd;

    //用于传递信号的管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false); //2 

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);

    client_data *users_timer = new client_data[MAX_FD];


    while(!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll wait failure");
            break;
        }
        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            /*
                1.listenfd
                2.signal(pipefd[1] 可读) ： SIGALAM, SIGTERM
                3.connfd : error异常, epollin可读, epollout可写
            */

           if(sockfd == listenfd)
           {
                sockaddr_in client_addr;
                socklen_t client_addrlength = sizeof(client_addr);
#ifdef listenfdLT
                int connfd = accept(listenfd, (sockaddr *)&client_addr, &client_addrlength);
                if(connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }

                user_http_conns[connfd].init(connfd, client_addr); //保存http连接

                //初始化client_data数据
                //创建定时器, 超时时间，回调函数，client_data
                users_timer[connfd].address = client_addr;
                users_timer[connfd].sockfd = connfd;
                users_timer[connfd].timer = nullptr;

                util_timer* timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_fun;
                time_t cur = time(NULL);
                timer->expire = cur + 3*TIMESLOT;
                users_timer[connfd].timer = timer;

                timer_list.add_timer(timer);

#endif

#ifdef listenfdET
                while(1)
                {
                    
                }
#endif
            }

               
           else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
           {

           }

           else if(sockfd == pipefd[0] && events[i].events & EPOLLIN)
           {

           }
           else if(events[i].events & EPOLLIN)
           {

           }
           else if(events[i].events & EPOLLOUT)
           {

           }

           if(timeout)
           {
               timer_handler();
               timeout =false;
           }
        }
    }





    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] user_http_conns;
    delete[] users_timer;
    delete pool;
    return 0;
}