#include "sql_conn_pool.h"

#include <iostream>

SqlConnPool::SqlConnPool()
{
    this->MaxConn = 0;
    this->CurConn = 0;
    this->FreeConn = 0;
}
SqlConnPool::~SqlConnPool()
{
    lock.lock();
    CurConn = 0;
    FreeConn = 0;
    connList.clear(); //内部智能指针，deleter
    lock.unlock();
}

SqlConnPool* SqlConnPool::GetInstance()
{
    static SqlConnPool connPool;
    return &connPool;
}

void SqlConnPool::init(std::string url, int port, 
        std::string user, std::string password, 
        std::string dbname, unsigned int maxconn)
{
    this->url = url;
    this->port = port;
    this->user = user;
    this->password = password;
    this->dbname = dbname;


    lock.lock(); //保护conn list

    for(int i = 0; i < maxconn; i++)
    {
        MYSQL* con = NULL;
        con = mysql_init(con);
        if(con == NULL)
        {
            std::cout << "Error:" << mysql_error(con);
			exit(1);
        }
		con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, NULL, 0);

        if(con == NULL)
        {
            std::cout << "Error:" << mysql_error(con);
			exit(1);
        }

        //智能指针，lambda deleter
        connList.push_back(MysqlPtr(con, [](MYSQL* conn){
            mysql_close(conn);
            std::cout << "close one mysql conn." << std::endl;
            }));
        ++FreeConn;
    }

    reserve = sem(FreeConn);
    this->MaxConn = maxconn;

    lock.unlock();

    std::cout << "init SQLCONNPOOL";
    std::cout << " >>mysql -h" << url << " -u" << user << " -p" << password << std::endl;

}
    
MysqlPtr SqlConnPool::GetConnection()
{
    if(connList.size() == 0)
    {
        return NULL;
    }

    reserve.wait();
    lock.lock();

    auto conn = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;

    lock.unlock();

    return conn;
}
bool SqlConnPool::ReleaseConnection(MysqlPtr conn)
{
    if(!conn) //重载operator bool
    {
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    reserve.post();
    return true;

}
int SqlConnPool::GetFreeConn()
{
    return this->FreeConn;
}

void SqlConnPool::DestroyPool()
{
    //参见析构函数
}


connectionRAII::connectionRAII(SqlConnPool *connPool)
{
    conRAII = connPool->GetConnection();
    poolRAII = connPool;
}
connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}