#ifndef _SQLCONN_POOL_H_
#define _SQLCONN_POOL_H_

#include <list>
#include <string>
#include <memory>
#include <functional>
#include <mysql/mysql.h>

#include "../lock/locker.h"

//typedef std::shared_ptr<MYSQL> MysqlPtr;
//编译需要开启 -std=c++11
using MysqlPtr = std::shared_ptr<MYSQL>;
class SqlConnPool
{
public:

    ~SqlConnPool();

    //单例
    static SqlConnPool* GetInstance();
    void init(std::string url, int port, std::string user, std::string password, 
                std::string dbname, unsigned int maxconn);
    
public:

    MysqlPtr GetConnection();
    bool ReleaseConnection(MysqlPtr conn);
    int GetFreeConn();
    void DestroyPool();

    //作为deleter,报错,后用lamdba函数
    //error: invalid use of non-static member function
    //void closeConn(MYSQL* conn);

private:
    SqlConnPool();

    unsigned int MaxConn;
    unsigned int CurConn;
    unsigned int FreeConn;

private:
    std::list<MysqlPtr> connList;
    locker lock;  //可用std::mutex替代
    sem reserve;  //C++11没有提供信号量的支持

private:
    //mysql -h loaclhost -p 3306 -uroot -plirui
    //use dbname
    std::string url;
    std::string port;
    std::string user;
    std::string password;
    std::string dbname;
};


static auto ConnPool = std::function<SqlConnPool*()>(SqlConnPool::GetInstance);

class connectionRAII{

public:
	connectionRAII(SqlConnPool *connPool);
	~connectionRAII();

    MYSQL* getMysqlConn() {return conRAII.get();}
	
private:
	MysqlPtr conRAII;
	SqlConnPool *poolRAII;
};



#endif