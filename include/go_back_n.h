#ifndef GO_BACK_N_H
#define GO_BACK_N_H

#include <stdint.h>
#include <winsock2.h>

#include "packet.h"
#include "timer.h"

#define GBN_WINDOW_SIZE 4
#define GBN_MAX_WINDOW_SIZE 64
#define GBN_DEFAULT_RTO 1.0
#define GBN_MAX_RETRIES 10
#define GBN_BUFFER_SIZE (15 + MAX_PAYLOAD_SIZE)

typedef struct {
    SOCKET socket;
    struct sockaddr_in peer_address;
    uint32_t base;
    uint32_t next_sequence_number;
    uint32_t window_size;
    Packet window[GBN_MAX_WINDOW_SIZE];
    RetransmissionTimer timer;
    int retry_count;
    int timer_active;
    int packets_sent;
    int packets_received;
    int retransmissions;
    int corrupted_packets;
    int duplicate_packets;
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
int gbn_send(GoBackNState *state, const uint8_t *data, uint16_t length);
int gbn_receive(GoBackNState *state, Packet *packet);
int gbn_send_ack(GoBackNState *state, uint32_t acknowledgement_number);
int gbn_retransmit_expired(GoBackNState *state);
int gbn_flush(GoBackNState *state);

void gbn_receiver_init(GoBackNReceiverState *state, SOCKET socket,
                       const struct sockaddr_in *peer_address);
int gbn_receiver_process(GoBackNReceiverState *state, const Packet *packet);

#endif
