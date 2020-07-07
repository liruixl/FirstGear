
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/socket.h>
#include "./CGImysql/sql_conn_pool.h"
#include "./log/block_queue.h"
#include "./log/log.h"
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./lst_timer/lst_timer.h"

#include "Task.h"
#include <unistd.h>

void tsetmysql_and_threadpool()
{
    //先从连接池中取一个连接
    //创建数据库连接池
    SqlConnPool *connPool = ConnPool();
    connPool->init("localhost", 3306, "root", "lirui", "youshuang", 8);

    threadpool<Task> tp(connPool);

    std::vector<Task *> tasks; 

    for(int i = 0; i < 30; i++)
    {
        Task * task = new Task;
        tasks.push_back(task);
        tp.addTask(task);
        sleep(1);
    }

    
    // for(int i = 0; i < 66; i++)
    // {
    //     delete tasks[i]; //线程还没执行我这就delete了？？
    // }

    // sleep(8); //bug
    // process 55 th request
    // process 56 th request
    // 段错误 (核心已转储)

    connectionRAII mysqlcon(connPool);
    MYSQL* mysql = mysqlcon.getMysqlConn();

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        std::cout << "SELECT error:" << mysql_error(mysql) << std::endl;
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        
        std::cout << temp1 << " " << temp2 << std::endl;
    }
}

void test_log()
{
    Log::get_instance()->init("lirui_log.txt", 0);

    Log::get_instance()->write_log(0, "%d-%d-%s", 1, 100, "debug log molude aaaaa");

    LOG_INFO("-----%s",  "I hopppppppe offfffffer");
    LOG_WARN("-----%s",  "I hopppppppe offfffffer");
    LOG_DEBUG("-----%s",  "I hopppppppe offfffffer");

}

void test_timer_lst()
{
    std::vector<long> timer = {30, 5, 4, 7, 23, 20, 14, 35, 63, 33};
    std::unordered_map<long, util_timer*> table;

    sort_timer_lst timerList;

    //add
    for(auto t : timer)
    {
        util_timer* node = new util_timer;
        node->expire = t;

        timerList.add_timer(node);
        table[t] = node;
    }
    timerList.print();


    //del
    //adjust

    std::vector<long> adjtimer = {4, 7, 23, 33, 63};
    
    for(auto t : adjtimer)
    {
        util_timer* node = table[t];
        node->expire  += 20;

        timerList.adjust_timer(node);
    }
    timerList.print();

}

int main()
{
    


    return 0;
}