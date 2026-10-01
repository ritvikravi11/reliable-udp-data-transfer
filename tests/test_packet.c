#include <stdio.h>
#include <string.h>
#include "../include/packet.h"

int main()
{
    Packet original;
    Packet restored;

    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];

    memset(&original, 0, sizeof(Packet));
    memset(&restored, 0, sizeof(Packet));

    original.type = PACKET_DATA;
    original.sequence_number = 12345;
    original.acknowledgement_number = 67890;

    strcpy((char *)original.payload, "Hello Serialization");
    original.payload_length = strlen((char *)original.payload);

    original.checksum = calculate_checksum(&original);

    int size = serialize_packet(&original, buffer);

    if (size < 0)
    {
        printf("Serialization failed\n");
        return 1;
    }

    printf("Serialized size: %d\n", size);

    if (deserialize_packet(&restored, buffer) != 0)
    {
        printf("Deserialization failed\n");
        return 1;
    }

    printf("Type: %d\n", restored.type);
    printf("Sequence number: %u\n", restored.sequence_number);
    printf("Acknowledgement number: %u\n", restored.acknowledgement_number);
    printf("Payload length: %u\n", restored.payload_length);
    printf("Payload: %.*s\n", restored.payload_length, restored.payload);
    printf("Checksum: %u\n", restored.checksum);

    if (packet_validate(&restored))
    {
        printf("Packet is valid\n");
    }
    else
    {
        printf("Packet is invalid\n");
    }

    return 0;
}