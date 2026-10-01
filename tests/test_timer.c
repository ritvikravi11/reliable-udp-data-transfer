#include <stdio.h>
#include <windows.h>
#include "../include/timer.h"

int main()
{
    RttEstimator estimator;
    RetransmissionTimer timer;

    rtt_init(&estimator);

    printf("Initial RTO: %.3f seconds\n", rtt_get_rto(&estimator));

    rtt_update(&estimator, 0.200, 0);

    printf("\nAfter RTT sample 1:\n");
    printf("SRTT: %.3f seconds\n", estimator.srtt);
    printf("RTTVAR: %.3f seconds\n", estimator.rttvar);
    printf("RTO: %.3f seconds\n", estimator.rto);

    rtt_update(&estimator, 0.300, 0);

    printf("\nAfter RTT sample 2:\n");
    printf("SRTT: %.3f seconds\n", estimator.srtt);
    printf("RTTVAR: %.3f seconds\n", estimator.rttvar);
    printf("RTO: %.3f seconds\n", estimator.rto);

    double old_rto = estimator.rto;

    rtt_update(&estimator, 2.000, 1);

    printf("\nAfter retransmitted packet:\n");
    printf("SRTT: %.3f seconds\n", estimator.srtt);
    printf("RTO: %.3f seconds\n", estimator.rto);

    if (estimator.rto == old_rto)
    {
        printf("Karn's Algorithm: PASS\n");
    }
    else
    {
        printf("Karn's Algorithm: FAIL\n");
    }

    timer_start(&timer, 1);

    printf("\nTimer started for sequence number %u\n",
           timer.sequence_number);

    Sleep(700);

    printf("Elapsed time: %.3f seconds\n",
           timer_elapsed(&timer));

    if (timer_expired(&timer, estimator.rto))
    {
        printf("Timer expired\n");
    }
    else
    {
        printf("Timer has not expired\n");
    }

    timer_mark_retransmitted(&timer);

    printf("Packet marked as retransmitted\n");

    timer_stop(&timer);

    printf("Timer stopped\n");

    return 0;
}