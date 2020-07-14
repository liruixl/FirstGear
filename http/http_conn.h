#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../mysql/sql_conn_pool.h"
class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn(bool real_close = true);
    /*
        处理读缓冲区数据解析http：process_read
        如果请求完整，则生成http响应：process_write
    */
    void process();
    /* 
        读数据，由主线程调用，从TCP读缓冲区读出到用户指定的缓冲区
        可以使用ET/LT模式
    */
    bool read_once();
    /*
        从连接任务中已经准备好的缓存,主线程向连接socket TCP发送缓存写数据
        循环写. 如果error=EAGIN, 重新注册写事件
    */
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(SqlConnPool *connPool);

private:
    void init();

    /*处理读缓冲区的数据,解析http*/
    HTTP_CODE process_read();
    /*处理http请求,生成http响应,包括头部和内容,都放入内存中,注册写事件,以备主线程处理
    主要看怎么将文件内容写入到内存，如果文件内容很大呢?
    */
    bool process_write(HTTP_CODE ret);

    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);

    /*
        ****
        当得到一个完整正确的http请求时，分析目标文件属性，
        使用mmap将其映射到内存地址m_file_address处
    */
    HTTP_CODE do_request();

    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;

private:
    /*该http连接的socket和客户的socket地址*/
    int m_sockfd;
    sockaddr_in m_address;

    /*读缓冲区 1)已读入字节的下一个位置 2)正在分析字符的位置 3)正在解析的行的起始位置*/
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;

    /*写缓冲区 1)待发送的字节数*/
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    /*htpp请求相关参数解析 process_read会解析http请求*/
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN]; //doc_root + m_url 目标文件完整路径
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    /*请求的目标文件 process_write 会将文件集中写入*/
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
};

#endif
