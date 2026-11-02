#include "../include/go_back_n.h"

#include <stdio.h>
#include <string.h>

#define GBN_ACK_NONE UINT32_MAX
#define GBN_DEFAULT_INITIAL_RTO GBN_DEFAULT_RTO

static int send_packet(
    GoBackNState *state,
    const Packet *packet
)
{
    uint8_t buffer[GBN_BUFFER_SIZE];
    int length;
    int sent;

    if (state == NULL || packet == NULL) {
        return 0;
    }
    length = serialize_packet(packet, buffer);
    if (length <= 0) {
        return 0;
    }
    sent = sendto(
        state->socket,
        (const char *)buffer,
        length,
        0,
        (const struct sockaddr *)&state->peer_address,
        sizeof(state->peer_address)
    );
    return sent == length;
}

static int send_ack_packet(
    SOCKET socket,
    const struct sockaddr_in *peer_address,
    uint32_t acknowledgement_number
)
{
    Packet ack;
    uint8_t buffer[GBN_BUFFER_SIZE];
    int length;
    int sent;

    memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK;
    ack.sequence_number = acknowledgement_number;
    ack.acknowledgement_number = acknowledgement_number;
    ack.checksum = calculate_checksum(&ack);
    length = serialize_packet(&ack, buffer);
    if (length <= 0) {
        return 0;
    }
    sent = sendto(
        socket,
        (const char *)buffer,
        length,
        0,
        (const struct sockaddr *)peer_address,
        sizeof(*peer_address)
    );
    return sent == length;
}

static int decode_packet(
    const uint8_t *buffer,
    int length,
    Packet *packet
)
{
    if (buffer == NULL || packet == NULL || length < 15) {
        return 0;
    }
    if (deserialize_packet(packet, buffer, (size_t)length) != 0) {
        return 0;
    }
    return packet_validate(packet);
}

static void stop_base_timer(GoBackNState *state)
{
    timer_stop(&state->timer);
    state->timer_active = 0;
    state->retry_count = 0;
}

static double clamp_rto(double rto)
{
    if (rto < GBN_MIN_RTO) return GBN_MIN_RTO;
    if (rto > GBN_MAX_RTO) return GBN_MAX_RTO;
    return rto;
}

static void start_base_timer(GoBackNState *state)
{
    timer_start(&state->timer, state->base);
    state->timer_active = 1;
}

static int retransmit_window(GoBackNState *state)
{
    uint32_t sequence;

    if (state->retry_count >= GBN_MAX_RETRIES) {
        return -1;
    }
    for (sequence = state->base;
         sequence != state->next_sequence_number;
         ++sequence) {
        unsigned int slot = sequence % GBN_MAX_WINDOW_SIZE;
        if (!send_packet(
                state,
                &state->window[slot])) {
            return -1;
        }
        state->packets_sent++;
        state->retransmissions++;
        state->slot_retransmitted[slot] = 1;
    }
    state->retry_count++;
    state->duplicate_ack_count = 0;
    state->fast_retransmit_done = 1;
    timer_mark_retransmitted(&state->timer);
    return 1;
}

static void update_rto(GoBackNState *state, double sample)
{
    rtt_update(&state->rtt, sample, 0);
    state->rto = rtt_get_rto(&state->rtt);
    state->rto = clamp_rto(state->rto);
    state->accepted_rtt_samples++;
}

void gbn_init(
    GoBackNState *state,
    SOCKET socket,
    const struct sockaddr_in *peer_address
)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->socket = socket;
    state->window_size = GBN_WINDOW_SIZE;
    rtt_init(&state->rtt);
    state->rto = GBN_DEFAULT_RTO;
    state->initial_rto = GBN_DEFAULT_INITIAL_RTO;
    if (peer_address != NULL) {
        state->peer_address = *peer_address;
    }
}

int gbn_set_initial_rto(GoBackNState *state, double initial_rto)
{
    if (state == NULL || initial_rto <= 0.0 ||
        initial_rto < GBN_MIN_RTO || initial_rto > GBN_MAX_RTO ||
        state->base != state->next_sequence_number) {
        return 0;
    }
    rtt_init_with_rto(&state->rtt, initial_rto);
    state->initial_rto = initial_rto;
    state->rto = initial_rto;
    return 1;
}

int gbn_set_window_size(GoBackNState *state, uint32_t window_size)
{
    if (state == NULL || window_size == 0 ||
        window_size > GBN_MAX_WINDOW_SIZE ||
        state->base != state->next_sequence_number) {
        return 0;
    }
    state->window_size = window_size;
    return 1;
}

int gbn_send_ack(GoBackNState *state, uint32_t acknowledgement_number)
{
    if (state == NULL) {
        return 0;
    }
    return send_ack_packet(
        state->socket,
        &state->peer_address,
        acknowledgement_number
    );
}

int gbn_send(GoBackNState *state, const uint8_t *data, uint16_t length)
{
    Packet *packet;
    uint32_t sequence;

    if (state == NULL || data == NULL || length > MAX_PAYLOAD_SIZE ||
        state->window_size == 0 ||
        state->next_sequence_number - state->base >= state->window_size) {
        return 0;
    }

    sequence = state->next_sequence_number;
    packet = &state->window[sequence % GBN_MAX_WINDOW_SIZE];
    memset(packet, 0, sizeof(*packet));
    packet->type = PACKET_DATA;
    packet->sequence_number = sequence;
    packet->payload_length = length;
    memcpy(packet->payload, data, length);
    packet->checksum = calculate_checksum(packet);

    if (!send_packet(state, packet)) {
        return 0;
    }
    state->next_sequence_number++;
    state->packets_sent++;
    state->original_transmissions++;
    state->transmission_time[sequence % GBN_MAX_WINDOW_SIZE] =
        timer_elapsed(&state->timer);
    state->slot_retransmitted[sequence % GBN_MAX_WINDOW_SIZE] = 0;
    if (!state->timer_active) {
        state->retry_count = 0;
        start_base_timer(state);
        state->transmission_time[sequence % GBN_MAX_WINDOW_SIZE] =
            timer_elapsed(&state->timer);
    }
    return 1;
}

int gbn_receive(GoBackNState *state, Packet *packet)
{
    uint8_t buffer[GBN_BUFFER_SIZE];
    Packet local_packet;
    int received;
    uint32_t acknowledgement;

    if (state == NULL) {
        return -1;
    }
    if (packet == NULL) {
        packet = &local_packet;
    }

    received = recvfrom(
        state->socket,
        (char *)buffer,
        sizeof(buffer),
        0,
        NULL,
        NULL
    );
    if (received == SOCKET_ERROR) {
        return -1;
    }
    if (!decode_packet(buffer, received, packet)) {
        state->corrupted_packets++;
        return 0;
    }
    if (packet->type != PACKET_ACK) {
        return 0;
    }

    state->packets_received++;
    state->ack_count++;
    acknowledgement = packet->acknowledgement_number;
    if (acknowledgement == GBN_ACK_NONE ||
        state->base == state->next_sequence_number ||
        !sequence_in_window(state->base, acknowledgement,
                            sequence_distance(state->base,
                                              state->next_sequence_number))) {
        state->duplicate_packets++;
        state->duplicate_ack_count++;
        state->duplicate_ack_count_total++;
        return 0;
    }

    {
        unsigned int slot = state->base % GBN_MAX_WINDOW_SIZE;
        if (state->slot_retransmitted[slot]) {
            state->rejected_rtt_samples++;
        } else {
            update_rto(state, timer_elapsed(&state->timer));
        }
    }
    do {
        state->slot_retransmitted[state->base % GBN_MAX_WINDOW_SIZE] = 0;
        state->base++;
    } while (state->base != acknowledgement + 1U);
    state->retry_count = 0;
    state->duplicate_ack_count = 0;
    state->fast_retransmit_done = 0;
    if (state->base == state->next_sequence_number) {
        stop_base_timer(state);
    } else {
        start_base_timer(state);
    }
    return 1;
}

int gbn_retransmit_expired(GoBackNState *state)
{
    if (state == NULL || !state->timer_active ||
        !timer_expired(&state->timer, state->rto)) {
        return 0;
    }
    state->timeout_count++;
    state->rto = clamp_rto(state->rto * 2.0);
    return retransmit_window(state);
}

int gbn_flush(GoBackNState *state)
{
    if (state == NULL) {
        return 0;
    }
    while (state->base != state->next_sequence_number) {
        fd_set read_set;
        struct timeval timeout;
        double remaining = state->rto - timer_elapsed(&state->timer);
        int result;

        if (remaining < 0.0) {
            remaining = 0.0;
        }
        FD_ZERO(&read_set);
        FD_SET(state->socket, &read_set);
        timeout.tv_sec = (long)remaining;
        timeout.tv_usec =
            (long)((remaining - timeout.tv_sec) * 1000000.0);
        result = select(0, &read_set, NULL, NULL, &timeout);
        if (result == SOCKET_ERROR) {
            return 0;
        }
        if (result == 0) {
            if (gbn_retransmit_expired(state) < 0) {
                return 0;
            }
        } else if (gbn_receive(state, NULL) < 0) {
            return 0;
        }
    }
    return 1;
}

void gbn_receiver_init(
    GoBackNReceiverState *state,
    SOCKET socket,
    const struct sockaddr_in *peer_address
)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->socket = socket;
    if (peer_address != NULL) {
        state->peer_address = *peer_address;
    }
}

int gbn_receiver_process(
    GoBackNReceiverState *state,
    const Packet *packet
)
{
    uint32_t acknowledgement;
    int delivered = 0;

    if (state == NULL || packet == NULL) {
        return -1;
    }
    if (packet->type != PACKET_DATA || !packet_validate(packet)) {
        state->corrupted_packets++;
        return -1;
    }

    state->packets_received++;
    if (packet->sequence_number == state->expected_sequence_number) {
        state->packets_delivered++;
        state->expected_sequence_number++;
        acknowledgement = packet->sequence_number;
        delivered = 1;
    } else if (sequence_before(packet->sequence_number,
                               state->expected_sequence_number)) {
        state->duplicate_packets++;
        acknowledgement = state->expected_sequence_number == 0
            ? GBN_ACK_NONE : state->expected_sequence_number - 1U;
    } else {
        state->out_of_order_packets++;
        acknowledgement = state->expected_sequence_number == 0
            ? GBN_ACK_NONE : state->expected_sequence_number - 1U;
    }

    if (!send_ack_packet(
            state->socket,
            &state->peer_address,
            acknowledgement)) {
        return -1;
    }
    return delivered;
}
