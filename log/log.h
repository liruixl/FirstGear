#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string>

#include "block_queue.h"

class Log
{
public:
    virtual ~Log(); //为啥子是虚的
    
    static Log* getInstance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::getInstance()->async_write_log();  //static 调用 static 类函数
    }

    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);


private:
    Log();

    void *async_write_log()
    {
        string single_log;
        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

    char dir_name[128];
    char log_name[128];
    locker m_mutex;
    FILE *m_fp;         //打开log的文件指针
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小 ?

    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    char *m_buf;
    bool m_is_async;    //是否同步标志位
    int m_close_log;   //关闭日志

    block_queue<string> *m_log_queue; //阻塞队列
};




#endif