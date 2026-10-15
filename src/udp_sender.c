#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#include "../include/stop_wait.h"

int main()
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;

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

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(9000);

    StopWaitState state;

    stop_wait_init(
        &state,
        sock,
        &server_addr
    );

    const char *message = "Hello Stop-and-Wait";

    printf("Starting Stop-and-Wait sender...\n");
    printf("Initial RTO: %.3f seconds\n",
           rtt_get_rto(&state.rtt));

    int success = stop_wait_send(
        &state,
        (const uint8_t *)message,
        (uint16_t)strlen(message)
    );

    if (success)
    {
        printf("\nTransfer successful!\n");
    }
    else
    {
        printf("\nTransfer failed!\n");
    }

    printf("Packets sent: %d\n",
           state.packets_sent);

    printf("Retransmissions: %d\n",
           state.retransmissions);

    printf("Final RTO: %.3f seconds\n",
           rtt_get_rto(&state.rtt));

    closesocket(sock);
    WSACleanup();

    return success ? 0 : 1;
}