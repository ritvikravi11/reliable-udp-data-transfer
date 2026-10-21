#ifndef SELECTIVE_REPEAT_H
#define SELECTIVE_REPEAT_H

#include <stdint.h>
#include <winsock2.h>
#include "packet.h"
#include "timer.h"

#define SR_WINDOW_SIZE 4
#define SR_MAX_RETRIES 10
#define SR_BUFFER_SIZE (15 + MAX_PAYLOAD_SIZE)
#define SR_RTO 1.0

typedef struct {
    SOCKET socket;
    struct sockaddr_in peer_address;
    uint32_t base;
    uint32_t next_sequence_number;
    Packet window[SR_WINDOW_SIZE];
    RetransmissionTimer timers[SR_WINDOW_SIZE];
    unsigned char acknowledged[SR_WINDOW_SIZE];
    unsigned char active[SR_WINDOW_SIZE];
    unsigned int retries[SR_WINDOW_SIZE];
    int packets_sent;
    int packets_received;
    int retransmissions;
    int corrupted_packets;
    int duplicate_packets;
} SelectiveRepeatState;

typedef struct {
    SOCKET socket;
    struct sockaddr_in peer_address;
    uint32_t base;
    Packet window[SR_WINDOW_SIZE];
    unsigned char present[SR_WINDOW_SIZE];
    int packets_received;
    int packets_delivered;
    int duplicate_packets;
    int out_of_window_packets;
    int corrupted_packets;
} SelectiveRepeatReceiverState;

void sr_init(SelectiveRepeatState *, SOCKET, const struct sockaddr_in *);
int sr_send(SelectiveRepeatState *, const uint8_t *, uint16_t);
int sr_receive(SelectiveRepeatState *, Packet *);
int sr_send_ack(SelectiveRepeatState *, uint32_t);
int sr_retransmit_expired(SelectiveRepeatState *);
int sr_flush(SelectiveRepeatState *);

void sr_receiver_init(SelectiveRepeatReceiverState *, SOCKET,
                      const struct sockaddr_in *);
int sr_receiver_process(SelectiveRepeatReceiverState *, const Packet *);

#endif
