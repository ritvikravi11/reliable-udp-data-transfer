
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <winsock2.h>
#include "../include/packet.h"

int main(void) {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in addr;
    Packet packet = {0};
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock startup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    packet.type = PACKET_DATA;
    packet.sequence_number = 1;
    packet.acknowledgement_number = 0;
    strcpy((char *)packet.payload, "Hello through channel emulator");
    packet.payload_length = (uint16_t)strlen((char *)packet.payload);
    packet.checksum = calculate_checksum(&packet);

    int packet_size = serialize_packet(&packet, buffer);
    if (packet_size <= 0) {
        printf("Packet serialization failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(9001);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int sent = sendto(sock, (const char *)buffer, packet_size, 0,
                      (struct sockaddr *)&addr, sizeof(addr));

    if (sent == SOCKET_ERROR) {
        printf("Send failed: %d\n", WSAGetLastError());
    } else {
        printf("Valid test packet sent: %d bytes\n", sent);
    }

    closesocket(sock);
    WSACleanup();
    return sent == SOCKET_ERROR ? 1 : 0;
}
