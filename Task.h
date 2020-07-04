#ifndef TASK_H
#define TASK_H

#include "./CGImysql/sql_conn_pool.h"
#include <iostream>

#include <unistd.h>
class Task
{

public:


    MYSQL* mysql;

    void process()
    {
        sleep(2);
        i++;
        std::cout << "process " << i << " th request" << std::endl;
    }

    static int i;
};

int Task::i = 0;


#endif