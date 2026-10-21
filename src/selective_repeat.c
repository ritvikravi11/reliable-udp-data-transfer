#include "../include/selective_repeat.h"

#include <string.h>

static int send_packet(SOCKET socket, const struct sockaddr_in *address,
                       const Packet *packet)
{
    uint8_t buffer[SR_BUFFER_SIZE];
    int n = serialize_packet(packet, buffer);
    if (n <= 0) return 0;
    return sendto(socket, (const char *)buffer, n, 0,
                  (const struct sockaddr *)address, sizeof(*address)) == n;
}

static int decode(const uint8_t *buffer, int length, Packet *packet)
{
    return length >= 15 && deserialize_packet(packet, buffer, (size_t)length) == 0 &&
           packet_validate(packet);
}

void sr_init(SelectiveRepeatState *s, SOCKET socket,
             const struct sockaddr_in *address)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->socket = socket;
    if (address) s->peer_address = *address;
}

int sr_send_ack(SelectiveRepeatState *s, uint32_t number)
{
    if (!s) return 0;
    Packet ack; memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK;
    ack.sequence_number = number;
    ack.acknowledgement_number = number;
    ack.checksum = calculate_checksum(&ack);
    return send_packet(s->socket, &s->peer_address, &ack);
}

int sr_send(SelectiveRepeatState *s, const uint8_t *data, uint16_t length)
{
    if (!s || !data || length > MAX_PAYLOAD_SIZE ||
        s->next_sequence_number - s->base >= SR_WINDOW_SIZE) return 0;
    uint32_t sequence = s->next_sequence_number;
    unsigned int slot = sequence % SR_WINDOW_SIZE;
    Packet *p = &s->window[slot];
    memset(p, 0, sizeof(*p));
    p->type = PACKET_DATA; p->sequence_number = sequence;
    p->payload_length = length; memcpy(p->payload, data, length);
    p->checksum = calculate_checksum(p);
    if (!send_packet(s->socket, &s->peer_address, p)) return 0;
    s->next_sequence_number++;
    s->active[slot] = 1; s->acknowledged[slot] = 0; s->retries[slot] = 0;
    timer_start(&s->timers[slot], sequence);
    s->packets_sent++;
    return 1;
}

int sr_receive(SelectiveRepeatState *s, Packet *packet)
{
    if (!s) return -1;
    Packet local_packet;
    if (!packet) packet = &local_packet;
    uint8_t buffer[SR_BUFFER_SIZE];
    int n = recvfrom(s->socket, (char *)buffer, sizeof(buffer), 0, NULL, NULL);
    if (n == SOCKET_ERROR) return -1;
    if (!decode(buffer, n, packet)) { s->corrupted_packets++; return 0; }
    s->packets_received++;
    if (packet->type != PACKET_ACK) return 0;
    uint32_t number = packet->acknowledgement_number;
    if (number < s->base || number >= s->next_sequence_number) {
        s->duplicate_packets++; return 0;
    }
    unsigned int slot = number % SR_WINDOW_SIZE;
    if (!s->active[slot] || s->window[slot].sequence_number != number) return 0;
    s->acknowledged[slot] = 1; s->active[slot] = 0;
    timer_stop(&s->timers[slot]);
    while (s->base < s->next_sequence_number &&
           !s->active[s->base % SR_WINDOW_SIZE]) s->base++;
    return 1;
}

int sr_retransmit_expired(SelectiveRepeatState *s)
{
    if (!s) return -1;
    int count = 0;
    for (uint32_t n = s->base; n < s->next_sequence_number; ++n) {
        unsigned int slot = n % SR_WINDOW_SIZE;
        if (!s->active[slot] || !timer_expired(&s->timers[slot], SR_RTO)) continue;
        if (s->retries[slot]++ >= SR_MAX_RETRIES) return -1;
        if (!send_packet(s->socket, &s->peer_address, &s->window[slot])) return -1;
        timer_mark_retransmitted(&s->timers[slot]);
        s->packets_sent++; s->retransmissions++; count++;
    }
    return count;
}

int sr_flush(SelectiveRepeatState *s)
{
    if (!s) return 0;
    while (s->base != s->next_sequence_number) {
        double left = SR_RTO;
        for (uint32_t n = s->base; n < s->next_sequence_number; ++n) {
            unsigned int slot = n % SR_WINDOW_SIZE;
            if (s->active[slot]) {
                double remaining = SR_RTO - timer_elapsed(&s->timers[slot]);
                if (remaining < left) left = remaining;
            }
        }
        if (left < 0.0) left = 0.0;
        fd_set set; FD_ZERO(&set); FD_SET(s->socket, &set);
        struct timeval tv = {(long)left, (long)((left - (long)left) * 1000000.0)};
        int r = select(0, &set, NULL, NULL, &tv);
        if (r == SOCKET_ERROR) return 0;
        if (r == 0) { if (sr_retransmit_expired(s) < 0) return 0; }
        else if (sr_receive(s, NULL) < 0) return 0;
    }
    return 1;
}

void sr_receiver_init(SelectiveRepeatReceiverState *s, SOCKET socket,
                      const struct sockaddr_in *address)
{
    if (!s) return;
    memset(s, 0, sizeof(*s)); s->socket = socket;
    if (address) s->peer_address = *address;
}

static int receiver_ack(SelectiveRepeatReceiverState *s, uint32_t number)
{
    Packet ack; memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK; ack.sequence_number = number;
    ack.acknowledgement_number = number; ack.checksum = calculate_checksum(&ack);
    return send_packet(s->socket, &s->peer_address, &ack);
}

int sr_receiver_process(SelectiveRepeatReceiverState *s, const Packet *p)
{
    if (!s || !p) return -1;
    if (p->type != PACKET_DATA || !packet_validate(p)) {
        s->corrupted_packets++; return -1;
    }
    s->packets_received++;
    if (p->sequence_number < s->base) {
        s->duplicate_packets++; return receiver_ack(s, p->sequence_number) ? 0 : -1;
    }
    if (p->sequence_number >= s->base + SR_WINDOW_SIZE) {
        s->out_of_window_packets++;
        return receiver_ack(s, s->base == 0 ? UINT32_MAX : s->base - 1) ? 0 : -1;
    }
    unsigned int slot = p->sequence_number % SR_WINDOW_SIZE;
    if (!s->present[slot]) { s->window[slot] = *p; s->present[slot] = 1; }
    else s->duplicate_packets++;
    if (!receiver_ack(s, p->sequence_number)) return -1;
    while (s->present[s->base % SR_WINDOW_SIZE]) {
        s->present[s->base % SR_WINDOW_SIZE] = 0;
        s->packets_delivered++; s->base++;
    }
    return 0;
}
