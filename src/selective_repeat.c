#include "../include/selective_repeat.h"

#include <string.h>

static int send_packet(SOCKET socket, const struct sockaddr_in *address,
                       const Packet *packet)
{
    uint8_t buffer[SR_BUFFER_SIZE];
    int length;

    if (packet == NULL || address == NULL) {
        return 0;
    }
    length = serialize_packet(packet, buffer);
    if (length <= 0) {
        return 0;
    }
    return sendto(socket, (const char *)buffer, length, 0,
                  (const struct sockaddr *)address, sizeof(*address)) == length;
}

static int decode_packet(const uint8_t *buffer, int length, Packet *packet)
{
    return buffer != NULL && packet != NULL && length >= 15 &&
           deserialize_packet(packet, buffer, (size_t)length) == 0 &&
           packet_validate(packet);
}

static unsigned int slot_for(uint32_t sequence, uint32_t window_size)
{
    return (unsigned int)(sequence % window_size);
}

static void update_rto(SelectiveRepeatState *state, RetransmissionTimer *timer)
{
    if (!timer->retransmitted) {
        rtt_update(&state->rtt, timer_elapsed(timer), 0);
        state->rto = rtt_get_rto(&state->rtt);
        if (state->rto < SR_MIN_RTO) {
            state->rto = SR_MIN_RTO;
        } else if (state->rto > SR_MAX_RTO) {
            state->rto = SR_MAX_RTO;
        }
        state->accepted_rtt_samples++;
    } else {
        state->rejected_rtt_samples++;
    }
}

static double clamp_rto(double rto)
{
    if (rto < SR_MIN_RTO) return SR_MIN_RTO;
    if (rto > SR_MAX_RTO) return SR_MAX_RTO;
    return rto;
}

void sr_init(SelectiveRepeatState *state, SOCKET socket,
             const struct sockaddr_in *address)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->socket = socket;
    state->window_size = SR_WINDOW_SIZE;
    rtt_init(&state->rtt);
    state->rto = SR_RTO;
    state->initial_rto = SR_RTO;
    if (address != NULL) {
        state->peer_address = *address;
    }
}

int sr_set_initial_rto(SelectiveRepeatState *state, double initial_rto)
{
        if (state == NULL || initial_rto < SR_MIN_RTO ||
            initial_rto > SR_MAX_RTO ||
            state->base != state->next_sequence_number) {
            return 0;
        }
        rtt_init_with_rto(&state->rtt, initial_rto);
        state->initial_rto = initial_rto;
        state->rto = initial_rto;
        return 1;
}

int sr_set_window_size(SelectiveRepeatState *state, uint32_t window_size)
{
    if (state == NULL || window_size == 0 ||
        window_size > SR_MAX_WINDOW_SIZE ||
        (uint64_t)window_size > SEQUENCE_SPACE_SIZE / 2ULL ||
        state->base != state->next_sequence_number) {
        return 0;
    }
    state->window_size = window_size;
    return 1;
}

int sr_send_ack(SelectiveRepeatState *state, uint32_t number)
{
    Packet ack;

    if (state == NULL) {
        return 0;
    }
    memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK;
    ack.sequence_number = number;
    ack.acknowledgement_number = number;
    ack.checksum = calculate_checksum(&ack);
    return send_packet(state->socket, &state->peer_address, &ack);
}

int sr_send(SelectiveRepeatState *state, const uint8_t *data,
            uint16_t length)
{
    uint32_t sequence;
    unsigned int slot;
    Packet *packet;

    if (state == NULL || data == NULL || length > MAX_PAYLOAD_SIZE ||
        state->window_size == 0 ||
        state->next_sequence_number - state->base >= state->window_size) {
        return 0;
    }

    sequence = state->next_sequence_number;
    slot = slot_for(sequence, state->window_size);
    if (state->active[slot] ||
        (state->window[slot].sequence_number != sequence &&
         state->timers[slot].active)) {
        return 0;
    }

    packet = &state->window[slot];
    memset(packet, 0, sizeof(*packet));
    packet->type = PACKET_DATA;
    packet->sequence_number = sequence;
    packet->payload_length = length;
    memcpy(packet->payload, data, length);
    packet->checksum = calculate_checksum(packet);

    if (!send_packet(state->socket, &state->peer_address, packet)) {
        return 0;
    }

    state->next_sequence_number++;
    state->active[slot] = 1;
    state->acknowledged[slot] = 0;
    state->retries[slot] = 0;
    timer_start(&state->timers[slot], sequence);
    state->packets_sent++;
    state->original_transmissions++;
    return 1;
}

int sr_receive(SelectiveRepeatState *state, Packet *packet)
{
    Packet local_packet;
    uint8_t buffer[SR_BUFFER_SIZE];
    int length;
    uint32_t number;
    unsigned int slot;

    if (state == NULL) {
        return -1;
    }
    if (packet == NULL) {
        packet = &local_packet;
    }
    length = recvfrom(state->socket, (char *)buffer, sizeof(buffer),
                      0, NULL, NULL);
    if (length == SOCKET_ERROR) {
        return -1;
    }
    if (!decode_packet(buffer, length, packet)) {
        state->corrupted_packets++;
        return 0;
    }
    if (packet->type != PACKET_ACK) {
        return 0;
    }

    state->packets_received++;
    state->ack_count++;
    number = packet->acknowledgement_number;
    if (state->base == state->next_sequence_number ||
        !sequence_in_window(state->base, number,
                            sequence_distance(state->base,
                                              state->next_sequence_number))) {
        state->duplicate_packets++;
        state->duplicate_ack_count_total++;
        return 0;
    }

    slot = slot_for(number, state->window_size);
    if (!state->active[slot] ||
        state->window[slot].sequence_number != number) {
        state->duplicate_packets++;
        state->duplicate_ack_count_total++;
        return 0;
    }

    update_rto(state, &state->timers[slot]);
    state->acknowledged[slot] = 1;
    state->active[slot] = 0;
    timer_stop(&state->timers[slot]);
    while (state->base < state->next_sequence_number &&
           !state->active[slot_for(state->base, state->window_size)]) {
        state->base++;
    }
    return 1;
}

int sr_retransmit_expired(SelectiveRepeatState *state)
{
    uint32_t sequence;
    int retransmitted = 0;

    if (state == NULL) {
        return -1;
    }
    for (sequence = state->base;
         sequence != state->next_sequence_number;
         ++sequence) {
        unsigned int slot = slot_for(sequence, state->window_size);
        if (!state->active[slot] ||
            !timer_expired(&state->timers[slot], state->rto)) {
            continue;
        }
        if (state->retries[slot] >= SR_MAX_RETRIES) {
            return -1;
        }
        if (!send_packet(state->socket, &state->peer_address,
                         &state->window[slot])) {
            return -1;
        }
        state->retries[slot]++;
        state->packets_sent++;
        state->retransmissions++;
        state->timeout_count++;
        state->rto = clamp_rto(state->rto * 2.0);
        timer_mark_retransmitted(&state->timers[slot]);
        retransmitted++;
    }
    return retransmitted;
}

int sr_flush(SelectiveRepeatState *state)
{
    if (state == NULL) {
        return 0;
    }
    while (state->base != state->next_sequence_number) {
        double remaining = state->rto;
        uint32_t sequence;
        fd_set read_set;
        struct timeval timeout;
        int result;

        for (sequence = state->base;
             sequence != state->next_sequence_number;
             ++sequence) {
            unsigned int slot = slot_for(sequence, state->window_size);
            if (state->active[slot]) {
                double candidate =
                    state->rto - timer_elapsed(&state->timers[slot]);
                if (candidate < remaining) {
                    remaining = candidate;
                }
            }
        }
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
            if (sr_retransmit_expired(state) < 0) {
                return 0;
            }
        } else if (sr_receive(state, NULL) < 0) {
            return 0;
        }
    }
    return 1;
}

void sr_receiver_init(SelectiveRepeatReceiverState *state, SOCKET socket,
                      const struct sockaddr_in *address)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->socket = socket;
    state->window_size = SR_WINDOW_SIZE;
    if (address != NULL) {
        state->peer_address = *address;
    }
}

int sr_receiver_set_window_size(SelectiveRepeatReceiverState *state,
                                uint32_t window_size)
{
    if (state == NULL || window_size == 0 ||
        window_size > SR_MAX_WINDOW_SIZE ||
        (uint64_t)window_size > SEQUENCE_SPACE_SIZE / 2ULL ||
        state->base != 0 || state->packets_received != 0) {
        return 0;
    }
    state->window_size = window_size;
    return 1;
}

static int receiver_ack(SelectiveRepeatReceiverState *state,
                        uint32_t number)
{
    Packet ack;

    memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK;
    ack.sequence_number = number;
    ack.acknowledgement_number = number;
    ack.checksum = calculate_checksum(&ack);
    return send_packet(state->socket, &state->peer_address, &ack);
}

int sr_receiver_process(SelectiveRepeatReceiverState *state,
                        const Packet *packet)
{
    unsigned int slot;

    if (state == NULL || packet == NULL) {
        return -1;
    }
    if (packet->type != PACKET_DATA || !packet_validate(packet)) {
        state->corrupted_packets++;
        return -1;
    }

    state->packets_received++;
    if (packet->sequence_number < state->base) {
        state->duplicate_packets++;
        return receiver_ack(state, packet->sequence_number) ? 0 : -1;
    }
    if (packet->sequence_number - state->base >= state->window_size) {
        state->out_of_window_packets++;
        return receiver_ack(
            state,
            state->base == 0 ? UINT32_MAX : state->base - 1U
        ) ? 0 : -1;
    }

    slot = slot_for(packet->sequence_number, state->window_size);
    if (state->present[slot]) {
        state->duplicate_packets++;
    } else {
        state->window[slot] = *packet;
        state->present[slot] = 1;
    }
    if (!receiver_ack(state, packet->sequence_number)) {
        return -1;
    }

    while (state->present[slot_for(state->base, state->window_size)]) {
        state->present[slot_for(state->base, state->window_size)] = 0;
        state->packets_delivered++;
        state->base++;
    }
    return 0;
}
