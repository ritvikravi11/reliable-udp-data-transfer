#include "../include/packet.h"

uint32_t calculate_checksum(const Packet *packet)
{
    uint32_t sum = 0;

    sum += packet->type;
    sum += packet->sequence_number;
    sum += packet->acknowledgement_number;
    sum += packet->payload_length;

    for (uint16_t i = 0; i < packet->payload_length; i++)
    {
        sum += packet->payload[i];
    }

    return sum;
}

int packet_validate(const Packet *packet)
{
    if (packet == NULL)
    {
        return 0;
    }

    if (packet->payload_length > MAX_PAYLOAD_SIZE)
    {
        return 0;
    }

    if (calculate_checksum(packet) != packet->checksum)
    {
        return 0;
    }

    return 1;
}

int serialize_packet(const Packet *packet, uint8_t *buffer)
{
    if (packet == NULL || buffer == NULL)
    {
        return -1;
    }

    if (packet->payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

buffer[0] = (uint8_t)packet->type;
buffer[1] = (uint8_t)(packet->sequence_number >> 24);
buffer[2] = (uint8_t)(packet->sequence_number >> 16);
buffer[3] = (uint8_t)(packet->sequence_number >> 8);
buffer[4] = (uint8_t)packet->sequence_number;

buffer[5] = (uint8_t)(packet->acknowledgement_number >> 24);
buffer[6] = (uint8_t)(packet->acknowledgement_number >> 16);
buffer[7] = (uint8_t)(packet->acknowledgement_number >> 8);
buffer[8] = (uint8_t)packet->acknowledgement_number;

buffer[9] = (uint8_t)(packet->payload_length >> 8);
buffer[10] = (uint8_t)packet->payload_length;

buffer[11] = (uint8_t)(packet->checksum >> 24);
buffer[12] = (uint8_t)(packet->checksum >> 16);
buffer[13] = (uint8_t)(packet->checksum >> 8);
buffer[14] = (uint8_t)packet->checksum;

for (uint16_t i = 0; i < packet->payload_length; i++)
{
    buffer[15 + i] = packet->payload[i];
}

return 15 + packet->payload_length;
}
int deserialize_packet(Packet *packet, const uint8_t *buffer)
{
    if (packet == NULL || buffer == NULL)
    {
        return -1;
    }

    packet->type = (PacketType)buffer[0];

    packet->sequence_number =
        ((uint32_t)buffer[1] << 24) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 8) |
        (uint32_t)buffer[4];

    packet->acknowledgement_number =
        ((uint32_t)buffer[5] << 24) |
        ((uint32_t)buffer[6] << 16) |
        ((uint32_t)buffer[7] << 8) |
        (uint32_t)buffer[8];

    packet->payload_length =
        ((uint16_t)buffer[9] << 8) |
        (uint16_t)buffer[10];

    if (packet->payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    packet->checksum =
        ((uint32_t)buffer[11] << 24) |
        ((uint32_t)buffer[12] << 16) |
        ((uint32_t)buffer[13] << 8) |
        (uint32_t)buffer[14];

    for (uint16_t i = 0; i < packet->payload_length; i++)
    {
        packet->payload[i] = buffer[15 + i];
    }

    return 0;
}