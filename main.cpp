
#include <iostream>
#include <string>
#include "./CGImysql/sql_conn_pool.h"

int main()
{
    std::cout << "hello world!" << std::endl;

    //先从连接池中取一个连接
    //创建数据库连接池
    SqlConnPool *connPool = ConnPool();
    connPool->init("localhost", 3306, "root", "lirui", "youshuang", 8);

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