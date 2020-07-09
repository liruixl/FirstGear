// https://code.visualstudio.com/docs/cpp/config-linux

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


//日志模块始化 连接池初始化 线程池初始化
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
    SqlConnPool *connPool = SqlConnPool::GetInstance();
    connPool->init("localhost", 3306,"root", "lirui", "youshuang", 8);

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
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    
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

                }
                
                continue;
#endif
            }

               
           else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
           {
               //http_conn数组user_http_conns，http连接，类内没有动态new
               //client_data数组user_timer，存储用户信息及定时器
               //定时器链表
               //发生错误时
               util_timer* timer = users_timer[sockfd].timer;
               timer->cb_func(&users_timer[sockfd]);
               /*
                    不清除http_conn数组和client_data数组种的数据嘛？
                    sockfd下标的数据
               */

               if(timer)
               {
                   timer_list.del_timer(timer);
               }
           }
            //处理信号
           else if(sockfd == pipefd[0] && events[i].events & EPOLLIN)
           {
               /*
                读取管道数据
               */
               int sig;
               char signals[1024];

               ret = recv(pipefd[0], signals, sizeof(signals), 0);

               if(ret == -1) { continue; }
               else if(ret == 0) { continue; }
               else
               {
                   for(int i = 0; i < ret; ++i)
                   {
                       switch (signals[i])
                       {
                       case SIGALRM:
                       {
                           timeout = true;
                           break;
                       }
                       case SIGTERM:
                       {
                           stop_server = true;
                           break;
                       }
                       
                       default:
                           break;
                       }
                   }
               }

           }
           else if(events[i].events & EPOLLIN)
           {

                /*
                    可读事件
                    1读取TCP缓冲区到http连接缓冲区
                    2调整timer
                    3添加任务队列http_conn
                */
                util_timer * timer = users_timer[sockfd].timer;
                bool readok = user_http_conns[sockfd].read_once();
                if(readok)
                {
                    LOG_INFO("read data, deal with the client(%s)", inet_ntoa(user_http_conns[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //pool->addTask(&user_http_conns[sockfd]);
                    pool->addTask(user_http_conns + sockfd);

                    if(timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_list.adjust_timer(timer); 
                    }
                }
                else //读error
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer) timer_list.del_timer(timer);
                }
           }
           else if(events[i].events & EPOLLOUT)
           {
               /*
                    可写事件
                    1将http缓冲区响应数据写入TCP发送缓冲区
                    2调整计时器
               */
               util_timer* timer = users_timer[sockfd].timer;
               bool writeok = user_http_conns[sockfd].write();
               if(writeok)
               {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(user_http_conns[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_list.adjust_timer(timer);
                    }
               }
               else
               {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_list.del_timer(timer);
                    }
               }
           }
        }

        if(timeout)
        {
            
            timer_handler();
            timeout =false;
        
        }
    }//end while

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] user_http_conns;
    delete[] users_timer;
    delete pool;
    return 0;
}