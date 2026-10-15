#ifndef STOP_WAIT_H
#define STOP_WAIT_H

#include <stdint.h>
#include <winsock2.h>

#include "packet.h"
#include "timer.h"

#define STOP_WAIT_MAX_RETRIES 10
#define STOP_WAIT_BUFFER_SIZE 2048

typedef struct
{
    SOCKET socket;
    struct sockaddr_in peer_address;

    RttEstimator rtt;
    RetransmissionTimer timer;

    uint32_t next_sequence_number;

    int packets_sent;
    int packets_received;
    int retransmissions;
    int corrupted_packets;
    int duplicate_packets;

} StopWaitState;


/*
 * Initialize Stop-and-Wait state.
 */
void stop_wait_init(
    StopWaitState *state,
    SOCKET socket,
    const struct sockaddr_in *peer_address
);


/*
 * Send one DATA packet using Stop-and-Wait ARQ.
 *
 * Returns:
 *     1 = success
 *     0 = failure
 */
int stop_wait_send(
    StopWaitState *state,
    const uint8_t *data,
    uint16_t length
);


/*
 * Receive and validate one DATA packet.
 *
 * IMPORTANT:
 * This function does NOT send the ACK.
 * The caller decides when to send it.
 *
 * Returns:
 *     1  = valid DATA packet
 *     0  = invalid/corrupted packet
 *    -1  = socket error
 */
int stop_wait_receive(
    StopWaitState *state,
    Packet *received_packet
);


/*
 * Send an ACK for a DATA packet.
 */
int stop_wait_send_ack(
    StopWaitState *state,
    uint32_t sequence_number
);

#endif