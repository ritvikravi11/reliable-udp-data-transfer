#include "../include/stop_wait.h"

#include <stdio.h>
#include <string.h>

#define MIN_RTO 0.100
#define MAX_RTO 60.0


static int send_packet(
    SOCKET socket,
    const struct sockaddr_in *address,
    const Packet *packet
)
{
    uint8_t buffer[STOP_WAIT_BUFFER_SIZE];

    int size = serialize_packet(packet, buffer);

    if (size <= 0)
    {
        return 0;
    }

    int sent = sendto(
        socket,
        (const char *)buffer,
        size,
        0,
        (const struct sockaddr *)address,
        sizeof(*address)
    );

    return sent == size;
}


void stop_wait_init(
    StopWaitState *state,
    SOCKET socket,
    const struct sockaddr_in *peer_address
)
{
    memset(state, 0, sizeof(StopWaitState));

    state->socket = socket;
    state->peer_address = *peer_address;

    rtt_init(&state->rtt);

    state->next_sequence_number = 0;
}


int stop_wait_send_ack(
    StopWaitState *state,
    uint32_t sequence_number
)
{
    Packet ack;

    memset(&ack, 0, sizeof(Packet));

    ack.type = PACKET_ACK;
    ack.sequence_number = sequence_number;
    ack.acknowledgement_number = sequence_number;
    ack.payload_length = 0;

    ack.checksum = calculate_checksum(&ack);

    return send_packet(
        state->socket,
        &state->peer_address,
        &ack
    );
}


int stop_wait_send(
    StopWaitState *state,
    const uint8_t *data,
    uint16_t length
)
{
    if (state == NULL || data == NULL)
    {
        return 0;
    }

    if (length > MAX_PAYLOAD_SIZE)
    {
        return 0;
    }

    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.type = PACKET_DATA;
    packet.sequence_number = state->next_sequence_number;
    packet.acknowledgement_number = 0;
    packet.payload_length = length;

    memcpy(packet.payload, data, length);
    packet.checksum = calculate_checksum(&packet);

    uint32_t sequence_number = packet.sequence_number;
    int retransmitted = 0;
    int attempts = 0;

    double rto = rtt_get_rto(&state->rtt);
    if (rto < MIN_RTO)
    {
        rto = MIN_RTO;
    }

    timer_start(&state->timer, sequence_number);

    while (attempts < STOP_WAIT_MAX_RETRIES)
    {
        if (!send_packet(state->socket, &state->peer_address, &packet))
        {
            printf("Failed to send DATA\n");
            timer_stop(&state->timer);
            return 0;
        }

        state->packets_sent++;

        printf(
            "Sent DATA seq=%u%s\n",
            sequence_number,
            retransmitted ? " (retransmission)" : ""
        );

        /*
         * Keep waiting for the matching ACK until this packet's RTO expires.
         * Invalid, corrupted, unrelated, or non-ACK packets must not trigger
         * an immediate DATA retransmission.
         */
        while (1)
        {
            double elapsed = timer_elapsed(&state->timer);
            double remaining = rto - elapsed;

            if (remaining <= 0.0)
            {
                break;
            }

            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(state->socket, &read_set);

            struct timeval timeout;
            timeout.tv_sec = (long)remaining;
            timeout.tv_usec =
                (long)((remaining - timeout.tv_sec) * 1000000.0);

            int result = select(0, &read_set, NULL, NULL, &timeout);

            if (result == 0)
            {
                /* Recheck elapsed time before deciding the RTO has expired. */
                continue;
            }

            if (result == SOCKET_ERROR)
            {
                printf("select() failed: %d\n", WSAGetLastError());
                timer_stop(&state->timer);
                return 0;
            }

            if (!FD_ISSET(state->socket, &read_set))
            {
                continue;
            }

            uint8_t buffer[STOP_WAIT_BUFFER_SIZE];
            struct sockaddr_in sender_address;
            int sender_length = sizeof(sender_address);

            int received = recvfrom(
                state->socket,
                (char *)buffer,
                sizeof(buffer),
                0,
                (struct sockaddr *)&sender_address,
                &sender_length
            );

            if (received <= 0)
            {
                continue;
            }

            if (received < 15)
            {
                state->corrupted_packets++;
                continue;
            }

            Packet ack;
            if (deserialize_packet(&ack, buffer) != 0)
            {
                state->corrupted_packets++;
                continue;
            }

            if (received != 15 + ack.payload_length)
            {
                state->corrupted_packets++;
                continue;
            }

            if (!packet_validate(&ack))
            {
                state->corrupted_packets++;
                continue;
            }

            if (ack.type != PACKET_ACK)
            {
                continue;
            }

            if (ack.acknowledgement_number != sequence_number &&
                ack.sequence_number != sequence_number)
            {
                continue;
            }

            double measured_rtt = timer_elapsed(&state->timer);
            timer_stop(&state->timer);

            /*
             * Karn's Algorithm:
             * Ignore RTT samples for packets that were retransmitted.
             */
            rtt_update(&state->rtt, measured_rtt, retransmitted);

            if (state->rtt.rto < MIN_RTO)
            {
                state->rtt.rto = MIN_RTO;
            }

            printf("Received ACK seq=%u\n", sequence_number);
            printf("RTT: %.3f seconds\n", measured_rtt);
            printf("Current RTO: %.3f seconds\n", rtt_get_rto(&state->rtt));

            state->next_sequence_number++;
            return 1;
        }

        /* The RTO expired without receiving the matching ACK. */
        attempts++;

        if (attempts >= STOP_WAIT_MAX_RETRIES)
        {
            printf("Maximum retransmission attempts reached\n");
            timer_stop(&state->timer);
            return 0;
        }

        state->retransmissions++;
        retransmitted = 1;

        rto *= 2.0;
        if (rto > MAX_RTO)
        {
            rto = MAX_RTO;
        }

        printf(
            "Timeout seq=%u, retry=%d, new RTO=%.3f\n",
            sequence_number,
            attempts,
            rto
        );

        timer_start(&state->timer, sequence_number);
    }

    timer_stop(&state->timer);
    return 0;
}


/*
 * Receive and validate one DATA packet.
 *
 * IMPORTANT:
 * This function does NOT send the ACK.
 *
 * The caller decides when the ACK should be sent.
 * This allows us to test ACK loss deterministically.
 */
int stop_wait_receive(
    StopWaitState *state,
    Packet *received_packet
)
{
    if (state == NULL ||
        received_packet == NULL)
    {
        return -1;
    }

    uint8_t buffer[
        STOP_WAIT_BUFFER_SIZE
    ];

    struct sockaddr_in sender_address;

    int sender_length =
        sizeof(sender_address);

    int received = recvfrom(
        state->socket,
        (char *)buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)
            &sender_address,
        &sender_length
    );

    if (received == SOCKET_ERROR)
    {
        printf(
            "recvfrom failed: %d\n",
            WSAGetLastError()
        );

        return -1;
    }

    if (received < 15)
    {
        printf(
            "Packet rejected: too small "
            "(%d bytes)\n",
            received
        );

        state->corrupted_packets++;

        return 0;
    }

    if (deserialize_packet(
            received_packet,
            buffer) != 0)
    {
        printf(
            "Packet deserialization failed\n"
        );

        state->corrupted_packets++;

        return 0;
    }

    if (received !=
        15 + received_packet->payload_length)
    {
        printf(
            "Packet rejected: size mismatch "
            "(received=%d, expected=%d)\n",
            received,
            15 + received_packet->payload_length
        );

        state->corrupted_packets++;

        return 0;
    }

    if (!packet_validate(
            received_packet))
    {
        printf(
            "Packet rejected: "
            "checksum invalid\n"
        );

        state->corrupted_packets++;

        return 0;
    }

    if (received_packet->type !=
        PACKET_DATA)
    {
        printf(
            "Packet rejected: "
            "unexpected type %d\n",
            received_packet->type
        );

        return 0;
    }

    state->peer_address =
        sender_address;

    return 1;
}