#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include "../include/packet.h"

int main()
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;

    Packet packet = {0};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(9000);

    packet.type = PACKET_DATA;
    packet.sequence_number = 1;
    packet.acknowledgement_number = 0;

    strcpy((char *)packet.payload, "Hello UDP");

    packet.payload_length = strlen((char *)packet.payload);

    packet.checksum = calculate_checksum(&packet);

    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];

    int packet_size = serialize_packet(&packet, buffer);

    if (packet_size < 0)
    {
        printf("Packet serialization failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    int sent = sendto(
        sock,
        (const char *)buffer,
        packet_size,
        0,
        (struct sockaddr *)&server_addr,
        sizeof(server_addr)
    );

    if (sent == SOCKET_ERROR)
    {
        printf("Send failed\n");
    }
    else
    {
        printf("Packet sent: %d bytes\n", sent);
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}