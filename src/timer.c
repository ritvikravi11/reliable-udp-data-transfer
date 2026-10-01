#include "../include/timer.h"
#include <windows.h>
#include <math.h>

static double get_current_time()
{
    static LARGE_INTEGER frequency;
    static int initialized = 0;

    LARGE_INTEGER counter;

    if (!initialized)
    {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    QueryPerformanceCounter(&counter);

    return (double)counter.QuadPart / frequency.QuadPart;
}

void rtt_init(RttEstimator *estimator)
{
    estimator->srtt = 0.0;
    estimator->rttvar = 0.0;
    estimator->rto = 1.0;
    estimator->initialized = 0;
}

void rtt_update(
    RttEstimator *estimator,
    double measured_rtt,
    int retransmitted
)
{
    if (retransmitted)
    {
        return;
    }

    if (!estimator->initialized)
    {
        estimator->srtt = measured_rtt;
        estimator->rttvar = measured_rtt / 2.0;
        estimator->rto =
            estimator->srtt + K * estimator->rttvar;

        estimator->initialized = 1;
    }
    else
    {
        estimator->rttvar =
            (1.0 - BETA) * estimator->rttvar +
            BETA * fabs(estimator->srtt - measured_rtt);

        estimator->srtt =
            (1.0 - ALPHA) * estimator->srtt +
            ALPHA * measured_rtt;

        estimator->rto =
            estimator->srtt + K * estimator->rttvar;
    }
}

double rtt_get_rto(const RttEstimator *estimator)
{
    return estimator->rto;
}

void timer_start(
    RetransmissionTimer *timer,
    uint32_t sequence_number
)
{
    timer->sequence_number = sequence_number;
    timer->start_time = get_current_time();
    timer->retransmitted = 0;
    timer->active = 1;
}

double timer_elapsed(
    const RetransmissionTimer *timer
)
{
    if (!timer->active)
    {
        return 0.0;
    }

    return get_current_time() - timer->start_time;
}

int timer_expired(
    const RetransmissionTimer *timer,
    double rto
)
{
    if (!timer->active)
    {
        return 0;
    }

    return timer_elapsed(timer) >= rto;
}

void timer_stop(RetransmissionTimer *timer)
{
    timer->active = 0;
}

void timer_mark_retransmitted(
    RetransmissionTimer *timer
)
{
    timer->retransmitted = 1;
    timer->start_time = get_current_time();
}

void timer_manager_init(TimerManager *manager)
{
    manager->count = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
    {
        manager->timers[i].active = 0;
    }
}

int timer_manager_start(
    TimerManager *manager,
    uint32_t sequence_number
)
{
    RetransmissionTimer *timer =
        timer_manager_get(manager, sequence_number);

    if (timer != NULL)
    {
        timer_start(timer, sequence_number);
        return 1;
    }

    if (manager->count >= MAX_TIMERS)
    {
        return 0;
    }

    timer = &manager->timers[manager->count];

    timer_start(timer, sequence_number);

    manager->count++;

    return 1;
}

int timer_manager_stop(
    TimerManager *manager,
    uint32_t sequence_number
)
{
    RetransmissionTimer *timer =
        timer_manager_get(manager, sequence_number);

    if (timer == NULL)
    {
        return 0;
    }

    timer_stop(timer);

    return 1;
}

RetransmissionTimer *timer_manager_get(
    TimerManager *manager,
    uint32_t sequence_number
)
{
    for (int i = 0; i < manager->count; i++)
    {
        if (manager->timers[i].sequence_number == sequence_number)
        {
            return &manager->timers[i];
        }
    }

    return NULL;
}

int timer_manager_expired(
    TimerManager *manager,
    uint32_t sequence_number,
    double rto
)
{
    RetransmissionTimer *timer =
        timer_manager_get(manager, sequence_number);

    if (timer == NULL)
    {
        return 0;
    }

    return timer_expired(timer, rto);
}

double timer_manager_next_timeout(
    TimerManager *manager,
    double rto
)
{
    double minimum = -1.0;

    for (int i = 0; i < manager->count; i++)
    {
        RetransmissionTimer *timer =
            &manager->timers[i];

        if (!timer->active)
        {
            continue;
        }

        double remaining =
            rto - timer_elapsed(timer);

        if (remaining < 0.0)
        {
            remaining = 0.0;
        }

        if (minimum < 0.0 || remaining < minimum)
        {
            minimum = remaining;
        }
    }

    return minimum;
}