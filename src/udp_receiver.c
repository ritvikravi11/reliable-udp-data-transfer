#include <stddef.h>
#include "../include/packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>

#include "../include/stop_wait.h"

#define RECEIVER_PORT 9000

int main(void)
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n",
               WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(RECEIVER_PORT);

    if (bind(sock,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR)
    {
        printf("Bind failed on port %d: %d\n",
               RECEIVER_PORT, WSAGetLastError());

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    StopWaitState state;
    stop_wait_init(&state, sock, &server_addr);

    uint32_t expected_sequence = 0;
    uint32_t last_accepted_sequence = 0;
    int has_last_accepted = 0;

    const char *drop_env = getenv("DROP_FIRST_ACK");
    int drop_first_ack =
        drop_env != NULL && strcmp(drop_env, "1") == 0;
    int first_ack_dropped = 0;

    printf("Stop-and-Wait receiver listening on port %d...\n",
           RECEIVER_PORT);

    if (drop_first_ack)
    {
        printf("TEST MODE: first ACK will be dropped\n");
    }

    for (;;)
    {
        Packet packet;
        int result = stop_wait_receive(&state, &packet);

        if (result < 0)
        {
            printf("Receive failed; stopping receiver.\n");
            break;
        }

        if (result == 0)
        {
            continue;
        }

        state.packets_received++;

        if (packet.sequence_number == expected_sequence)
        {
            /* Accept and deliver a new DATA packet once. */
            printf("Received DATA seq=%u, length=%u\n",
                   packet.sequence_number,
                   (unsigned int)packet.payload_length);

            printf("Delivered payload: %.*s\n",
                   (int)packet.payload_length,
                   (const char *)packet.payload);

            last_accepted_sequence = packet.sequence_number;
            has_last_accepted = 1;
            expected_sequence++;

        }
        else if (has_last_accepted &&
                 packet.sequence_number == last_accepted_sequence)
        {
            /* Duplicate DATA: do not deliver it again. */
            state.duplicate_packets++;

            printf("Duplicate DATA seq=%u; "
                   "sending ACK again\n",
                   packet.sequence_number);
        }
        else
        {
            printf("Unexpected DATA seq=%u; expected=%u\n",
                   packet.sequence_number,
                   expected_sequence);
            continue;
        }

        /*
         * Simulate losing the first ACK.
         * The DATA packet has already been accepted, so
         * a retransmission will be recognized as a duplicate.
         */
        if (drop_first_ack && !first_ack_dropped)
        {
            first_ack_dropped = 1;
            printf("TEST MODE: intentionally dropped ACK seq=%u\n",
                   packet.sequence_number);
            continue;
        }

        if (!stop_wait_send_ack(&state, packet.sequence_number))
        {
            printf("Failed to send ACK seq=%u: %d\n",
                   packet.sequence_number,
                   WSAGetLastError());
            continue;
        }

        printf("Sent ACK seq=%u\n", packet.sequence_number);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
