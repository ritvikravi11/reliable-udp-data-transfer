#ifndef GO_BACK_N_H
#define GO_BACK_N_H

#include <stdint.h>
#include <winsock2.h>

#include "packet.h"
#include "timer.h"
#include "sequence.h"

#define GBN_WINDOW_SIZE 4
#define GBN_MAX_WINDOW_SIZE 64
#define GBN_DEFAULT_RTO 1.0
#define GBN_MIN_RTO 0.500
#define GBN_MAX_RTO 60.0
#define GBN_MAX_RETRIES 10
#define GBN_BUFFER_SIZE (15 + MAX_PAYLOAD_SIZE)

typedef struct {
    SOCKET socket;
    struct sockaddr_in peer_address;
    uint32_t base;
    uint32_t next_sequence_number;
    uint32_t window_size;
    Packet window[GBN_MAX_WINDOW_SIZE];
    double transmission_time[GBN_MAX_WINDOW_SIZE];
    unsigned char slot_retransmitted[GBN_MAX_WINDOW_SIZE];
    RetransmissionTimer timer;
    RttEstimator rtt;
    double rto;
    int retry_count;
    int duplicate_ack_count;
    int fast_retransmit_done;
    int timer_active;
    int packets_sent;
    int packets_received;
    int retransmissions;
    int corrupted_packets;
    int duplicate_packets;
    int original_transmissions;
    int timeout_count;
    int ack_count;
    int duplicate_ack_count_total;
    int accepted_rtt_samples;
    int rejected_rtt_samples;
    int fast_retransmissions;
    double initial_rto;
} GoBackNState;

typedef struct {
    SOCKET socket;
    struct sockaddr_in peer_address;
    uint32_t expected_sequence_number;
    int packets_received;
    int packets_delivered;
    int duplicate_packets;
    int out_of_order_packets;
    int corrupted_packets;
} GoBackNReceiverState;

void gbn_init(GoBackNState *state, SOCKET socket,
              const struct sockaddr_in *peer_address);
int gbn_set_window_size(GoBackNState *state, uint32_t window_size);
int gbn_set_initial_rto(GoBackNState *state, double initial_rto);
int gbn_send(GoBackNState *state, const uint8_t *data, uint16_t length);
int gbn_receive(GoBackNState *state, Packet *packet);
int gbn_send_ack(GoBackNState *state, uint32_t acknowledgement_number);
int gbn_retransmit_expired(GoBackNState *state);
int gbn_flush(GoBackNState *state);

void gbn_receiver_init(GoBackNReceiverState *state, SOCKET socket,
                       const struct sockaddr_in *peer_address);
int gbn_receiver_process(GoBackNReceiverState *state, const Packet *packet);

#endif
