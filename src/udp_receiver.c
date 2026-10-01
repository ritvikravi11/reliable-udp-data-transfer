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
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);

    Packet packet = {0};
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];

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
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(9000);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        printf("Bind failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("UDP receiver waiting on port 9000...\n");

    int received = recvfrom(
        sock,
        (char *)buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)&client_addr,
        &client_len
    );

    if (received == SOCKET_ERROR)
    {
        printf("Receive failed\n");
    }
    else if (received < 15)
    {
        printf("Invalid packet size\n");
    }
    else if (deserialize_packet(&packet, buffer) != 0)
    {
        printf("Packet deserialization failed\n");
    }
    else if (received != 15 + packet.payload_length)
    {
        printf("Invalid packet size\n");
    }
    else
    {
        printf("Received %d bytes\n", received);
        printf("Type: %d\n", packet.type);
        printf("Sequence number: %u\n", packet.sequence_number);
        printf("Acknowledgement number: %u\n", packet.acknowledgement_number);
        printf("Payload length: %u\n", packet.payload_length);
        printf("Payload: %.*s\n", packet.payload_length, packet.payload);

        if (packet_validate(&packet))
        {
            printf("Checksum: VALID\n");
        }
        else
        {
            printf("Checksum: INVALID\n");
        }
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}