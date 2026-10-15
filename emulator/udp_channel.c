#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "channel.h"

#define DEFAULT_EMULATOR_PORT 9001
#define DEFAULT_RECEIVER_PORT 9000
#define MAX_DATAGRAM_SIZE 2048

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET incoming = INVALID_SOCKET;
    SOCKET outgoing = INVALID_SOCKET;
    struct sockaddr_in listen_addr, receiver_addr, sender_addr;
    int sender_len = sizeof(sender_addr);
    char buffer[MAX_DATAGRAM_SIZE];
    char modified[MAX_DATAGRAM_SIZE];
    ChannelConfig config;
    ChannelStats stats = {0};
    unsigned int delay_ms = 0;
    int listen_port = DEFAULT_EMULATOR_PORT;
    int receiver_port = DEFAULT_RECEIVER_PORT;

    if (argc > 1)
        listen_port = atoi(argv[1]);
    if (argc > 2)
        receiver_port = atoi(argv[2]);

    if (listen_port < 1 || listen_port > 65535 ||
        receiver_port < 1 || receiver_port > 65535) {
        fprintf(stderr, "Ports must be between 1 and 65535\n");
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    incoming = socket(AF_INET, SOCK_DGRAM, 0);
    outgoing = socket(AF_INET, SOCK_DGRAM, 0);

    if (incoming == INVALID_SOCKET || outgoing == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        goto cleanup;
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = htons((u_short)listen_port);

    if (bind(incoming, (struct sockaddr *)&listen_addr,
             sizeof(listen_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed on port %d\n", listen_port);
        goto cleanup;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiver_addr.sin_port = htons((u_short)receiver_port);

   
    channel_default_config(&config);

    /*
     * Optional arguments:
     * listen_port receiver_port loss duplicate corrupt delay_ms seed
     *
     * Example:
     * udp_channel.exe 9001 9000 0.20 0.10 0.05 100 12345
     */
    if (argc > 3) config.loss_rate = atof(argv[3]);
    if (argc > 4) config.duplicate_rate = atof(argv[4]);
    if (argc > 5) config.corrupt_rate = atof(argv[5]);
    if (argc > 6) {
        config.min_delay_ms = (unsigned int)atoi(argv[6]);
        config.max_delay_ms = config.min_delay_ms;
    }
    config.seed = (argc > 7) ? (uint32_t)strtoul(argv[7], NULL, 10) : 12345;

    if (!channel_validate_config(&config)) {
        fprintf(stderr, "Invalid channel configuration. Rates must be between 0 and 1.\\n");
        WSACleanup();
        return 1;
    }

    channel_init(config.seed);

    printf("Settings: loss=%.2f duplicate=%.2f corrupt=%.2f delay=%u ms seed=%u\\n",
           config.loss_rate, config.duplicate_rate, config.corrupt_rate,
           config.min_delay_ms, config.seed);

    printf("Channel emulator listening on %d; forwarding to %d\n",
           listen_port, receiver_port);
    printf("Default mode: no impairments. Waiting for one datagram...\n");

    {
        int received = recvfrom(
            incoming, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&sender_addr, &sender_len
        );

        int result;

        if (received == SOCKET_ERROR) {
            fprintf(stderr, "Receive failed\n");
            goto cleanup;
        }

        result = channel_process(
            &config, &stats,
            (const uint8_t *)buffer, (size_t)received,
            (uint8_t *)modified, sizeof(modified), &delay_ms
        );

        if (result < 0) {
            fprintf(stderr, "Channel processing failed\n");
            goto cleanup;
        }

        printf("Received %d bytes; channel result %d; delay %u ms\n",
               received, result, delay_ms);

        if (result == 0) {
            printf("Datagram dropped by channel\n");
        } else {
            if (delay_ms > 0)
                Sleep(delay_ms);

            if (sendto(outgoing, modified, received, 0,
                       (struct sockaddr *)&receiver_addr,
                       sizeof(receiver_addr)) == SOCKET_ERROR) {
                fprintf(stderr, "Forward failed\n");
                goto cleanup;
            }

            if (result == 2) {
                if (sendto(outgoing, modified, received, 0,
                           (struct sockaddr *)&receiver_addr,
                           sizeof(receiver_addr)) == SOCKET_ERROR) {
                    fprintf(stderr, "Duplicate forward failed\n");
                    goto cleanup;
                }
                printf("Forwarded duplicate datagram\n");
            } else {
                printf("Forwarded datagram\n");
            }
        }
    }

    printf("Packets seen: %llu\n",
           (unsigned long long)stats.packets_seen);
    printf("Packets dropped: %llu\n",
           (unsigned long long)stats.packets_dropped);
    printf("Packets duplicated: %llu\n",
           (unsigned long long)stats.packets_duplicated);
    printf("Packets corrupted: %llu\n",
           (unsigned long long)stats.packets_corrupted);

cleanup:
    if (incoming != INVALID_SOCKET)
        closesocket(incoming);
    if (outgoing != INVALID_SOCKET)
        closesocket(outgoing);
    WSACleanup();
    return 0;
}
