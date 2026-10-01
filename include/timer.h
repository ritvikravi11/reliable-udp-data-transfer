#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define ALPHA 0.125
#define BETA 0.25
#define K 4.0

typedef struct
{
    double srtt;
    double rttvar;
    double rto;
    int initialized;
} RttEstimator;

typedef struct
{
    uint32_t sequence_number;
    double start_time;
    int retransmitted;
    int active;
} RetransmissionTimer;

void rtt_init(RttEstimator *estimator);

void rtt_update(
    RttEstimator *estimator,
    double measured_rtt,
    int retransmitted
);

double rtt_get_rto(const RttEstimator *estimator);

void timer_start(
    RetransmissionTimer *timer,
    uint32_t sequence_number
);

double timer_elapsed(
    const RetransmissionTimer *timer
);

int timer_expired(
    const RetransmissionTimer *timer,
    double rto
);

void timer_stop(RetransmissionTimer *timer);

void timer_mark_retransmitted(
    RetransmissionTimer *timer
);

#define MAX_TIMERS 1024

typedef struct
{
    RetransmissionTimer timers[MAX_TIMERS];
    int count;
} TimerManager;

void timer_manager_init(TimerManager *manager);

int timer_manager_start(
    TimerManager *manager,
    uint32_t sequence_number
);

int timer_manager_stop(
    TimerManager *manager,
    uint32_t sequence_number
);

RetransmissionTimer *timer_manager_get(
    TimerManager *manager,
    uint32_t sequence_number
);

int timer_manager_expired(
    TimerManager *manager,
    uint32_t sequence_number,
    double rto
);
double timer_manager_next_timeout(
    TimerManager *manager,
    double rto
);
#endif