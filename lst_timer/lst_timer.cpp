#include "lst_timer.h"
#include <cmath>

sort_timer_lst::sort_timer_lst()
{
    head = new util_timer;
    tail = new util_timer;
    
    head->expire = 0;
    tail->expire = -1;

    head->next = tail;
    tail->prev = head;
}
sort_timer_lst::~sort_timer_lst()
{
    util_timer* cur = head;
    while(cur)
    {
        auto temp = cur->next;
        delete cur;
        cur = temp;
    }
    head = nullptr;
    tail = nullptr;
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if(!timer)
    {
        return;
    }

    util_timer *cur = head;

    //cur -> timer -> cur->next
    while(cur->next != tail && timer->expire > cur->next->expire)
    {
        cur = cur->next;
    }

    timer->next = cur->next;
    timer->prev = cur;

    cur->next->prev = timer;
    cur->next = timer;

    std::cout << "add timer:" << timer->expire << std::endl;

}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if(!timer)
    {
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer)
    {
        return;
    }

    util_timer *cur = timer;

    //cur -> timer -> cur->next
    while(cur->next != tail && timer->expire > cur->next->expire)
    {
        cur = cur->next;
    }

    if(cur == timer) return;

    //delete
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;

    //join
    timer->next = cur->next;
    timer->prev = cur;

    cur->next->prev = timer;
    cur->next = timer;


}

void sort_timer_lst::tick()
{
    time_t cur_time = time(NULL);

    util_timer* cur = head->next;
    while(cur != tail)
    {
        if(cur_time < cur->expire)
        {
            break;
        }

        cur->cb_func(cur->user_data);
        auto tmp = cur->next;
        del_timer(cur);
        cur = tmp;
    }
}
