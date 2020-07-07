#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <netinet/in.h>

#include <time.h>
#include "../log/log.h"

#include <iostream>

class util_timer;

class client_data
{
    sockaddr_in address; //port and ip
    int sockfd;
    util_timer* timer;
};

class util_timer
{
public:
    util_timer(): prev(nullptr), next(nullptr){}

public:
    time_t expire;
    
    //返回类型  (*指针变量名)(参数列表) ; 
    void (*cb_func)(client_data* );
    client_data * user_data;
    util_timer* prev;
    util_timer* next;
};


class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void tick();

    void print()
    {
        auto cur = head->next;
        while (cur != tail)
        {
            /* code */
            std::cout << cur->expire << " -> ";
            cur = cur->next;
        }
        std::cout << std::endl;
    }

private:
    //void add_timer(util_timer* timer, util_timer *lst_head);
    util_timer* head;
    util_timer* tail;

};

#endif