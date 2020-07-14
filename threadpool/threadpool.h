#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <vector>
#include <iostream>

#include "../lock/locker.h"
#include "../mysql/sql_conn_pool.h"

template <typename T>
class threadpool{
public:
    threadpool(SqlConnPool* connPool, int thread_number = 8, int maxrequest = 10000);
    ~threadpool();
    bool addTask(T * request);

private:
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num;
    int m_max_requests;

    std::vector<pthread_t> m_threads;//用vector代替，数组
    //pthread_t *m_threads;
    
    //任务队列及线程同步
    std::list<T *> m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;

    bool m_stop;
    SqlConnPool* m_connPool; //单例类，其实并不需要成为成员变量的吧
};

template <typename T>
threadpool<T>::threadpool(SqlConnPool* connPool, int thread_number, int maxrequest)
    : m_connPool(connPool), m_thread_num(thread_number), m_max_requests(maxrequest),
    m_stop(false)
{
    if(m_thread_num <= 0 || maxrequest <= 0)
    {
        throw std::exception();
    }

    /*创建thread_number个线程，并将他们设置为脱离线程*/
    for(int i = 0; i < m_thread_num; i++)
    {
        printf( "creat %d threads!\n", i);
        pthread_t thread; //unsigned long
        if(pthread_create(&thread, NULL, worker, this) == 0) //成功
        {
            m_threads.push_back(thread);
        }
        else{
            throw std::exception();
        }

        //为啥要设置为脱离线程呢
        // if(pthread_detach(thread)) //成功时返回0，失败时返回其他值,注意参数
        // {
        //     throw std::exception();
        // }

    }

}
template <typename T>
threadpool<T>::~threadpool()
{
    m_stop = true;


    for(int i = 0; i < m_thread_num; i++) //这样可以
    {
        m_queuestat.post();
    }

    for(auto t : m_threads)
    {
        //m_queuestat.post(); //这样也不对，因为不一定哪个线程的到信号量
        pthread_join(t,NULL); //如果不是这个线程，那么就会被一直阻塞，所以还是不要用信号量的好
    }
}
template <typename T>
bool threadpool<T>::addTask(T * request)
{
    // std::list<T *> m_workqueue;
    // locker m_queuelocker;
    // sem m_queuestat;
    m_queuelocker.lock();

    if(m_workqueue.size() >  m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);

    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void * threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{

    while(!m_stop)
    {
        m_queuestat.wait();         //停止线程池，如果是join状态，要唤醒所有线程，信号量wait，要用post唤醒？
        m_queuelocker.lock();

        if(m_workqueue.empty())     //按照道理得到了信号量，这里不应该是空啊，如果在停止后用post唤醒，这里可能为空
        {
            m_queuelocker.unlock(); //只解锁？信号量不post吗
            continue;
        }

        T * requset = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!requset)
        {
            continue;
        }

        //request 持有mysql，mysqlcon管理着连接的回收
        //一定要是同一作用域
        //否则连接还在使用就被回收了
        connectionRAII mysqlcon(m_connPool); //如果连接已经被用光，这里返回NULL怎么办，不会拿不到，信号量消费者
        requset->mysql = mysqlcon.getMysqlConn();
        requset->process();

    }

}

#endif