#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1024

typedef enum {
    PACKET_DATA,
    PACKET_ACK,
    PACKET_START,
    PACKET_END
} PacketType;

typedef struct {
    PacketType type;
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint16_t payload_length;
    uint32_t checksum;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} Packet;

uint32_t calculate_checksum(const Packet *packet);

int packet_validate(const Packet *packet);
int serialize_packet(const Packet *packet, uint8_t *buffer);
int deserialize_packet(Packet *packet, const uint8_t *buffer);

#endif