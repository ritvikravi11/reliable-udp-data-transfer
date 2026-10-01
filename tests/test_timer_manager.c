#include <stdio.h>
#include <windows.h>
#include "../include/timer.h"

int main()
{
    TimerManager manager;

    timer_manager_init(&manager);

    printf("Timer Manager initialized\n");

   timer_manager_start(&manager, 1);

Sleep(100);

timer_manager_start(&manager, 2);

Sleep(100);

timer_manager_start(&manager, 3);

    printf("Started timers for sequences 1, 2 and 3\n");

    RetransmissionTimer *timer1 =
        timer_manager_get(&manager, 1);

    RetransmissionTimer *timer2 =
        timer_manager_get(&manager, 2);

    RetransmissionTimer *timer3 =
        timer_manager_get(&manager, 3);

    if (timer1 != NULL &&
        timer2 != NULL &&
        timer3 != NULL)
    {
        printf("All three timers found\n");
    }
    else
    {
        printf("Timer lookup failed\n");
        return 1;
    }

    Sleep(200);

    double rto = 0.5;

    printf("\nChecking timer expiration:\n");

    printf("Sequence 1: %s\n",
           timer_manager_expired(&manager, 1, rto)
               ? "EXPIRED"
               : "NOT EXPIRED");

    printf("Sequence 2: %s\n",
           timer_manager_expired(&manager, 2, rto)
               ? "EXPIRED"
               : "NOT EXPIRED");

    printf("Sequence 3: %s\n",
           timer_manager_expired(&manager, 3, rto)
               ? "EXPIRED"
               : "NOT EXPIRED");

    timer_manager_stop(&manager, 2);

    printf("\nTimer for sequence 2 stopped\n");

    if (!timer_manager_expired(&manager, 2, rto))
    {
        printf("Stopped timer is inactive: PASS\n");
    }
    else
    {
        printf("Stopped timer is inactive: FAIL\n");
    }
double next_timeout =
    timer_manager_next_timeout(&manager, rto);

printf("\nNext timeout: %.3f seconds\n", next_timeout);
    printf("\nTimer Manager test completed\n");

    return 0;
}