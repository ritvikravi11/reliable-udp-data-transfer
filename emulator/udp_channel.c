#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include "channel.h"

#define DEFAULT_EMULATOR_PORT 9001
#define DEFAULT_RECEIVER_PORT 9000
#define DEFAULT_IDLE_TIMEOUT_MS 15000
#define MAX_DATAGRAM_SIZE 2048
#define MAX_PENDING_DATAGRAMS 256

typedef struct {
    SOCKET output_socket;
    struct sockaddr_in destination;
    uint8_t data[MAX_DATAGRAM_SIZE];
    int length;
    unsigned long long due_time_ms;
    int active;
} PendingDatagram;

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static unsigned long long now_ms(void)
{
    return (unsigned long long)GetTickCount64();
}

static int queue_datagram(
    PendingDatagram *queue,
    const uint8_t *data,
    int length,
    SOCKET output_socket,
    const struct sockaddr_in *destination,
    unsigned int delay_ms
)
{
    int index;

    for (index = 0; index < MAX_PENDING_DATAGRAMS; ++index) {
        if (!queue[index].active) {
            queue[index].output_socket = output_socket;
            queue[index].destination = *destination;
            memcpy(queue[index].data, data, (size_t)length);
            queue[index].length = length;
            queue[index].due_time_ms = now_ms() + delay_ms;
            queue[index].active = 1;
            return 1;
        }
    }
    return 0;
}

static int flush_due_datagrams(PendingDatagram *queue)
{
    int index;
    int delivered = 0;
    unsigned long long current = now_ms();

    for (index = 0; index < MAX_PENDING_DATAGRAMS; ++index) {
        int sent;

        if (!queue[index].active || queue[index].due_time_ms > current) {
            continue;
        }
        sent = sendto(
            queue[index].output_socket,
            (const char *)queue[index].data,
            queue[index].length,
            0,
            (const struct sockaddr *)&queue[index].destination,
            sizeof(queue[index].destination)
        );
        if (sent == SOCKET_ERROR) {
            fprintf(stderr, "Forward failed: %d\n", WSAGetLastError());
            return -1;
        }
        queue[index].active = 0;
        delivered++;
    }
    return delivered;
}

static unsigned int next_queue_timeout(const PendingDatagram *queue)
{
    unsigned long long current = now_ms();
    unsigned long long minimum = 1000;
    int index;

    for (index = 0; index < MAX_PENDING_DATAGRAMS; ++index) {
        unsigned long long remaining;

        if (!queue[index].active) {
            continue;
        }
        remaining = queue[index].due_time_ms > current
            ? queue[index].due_time_ms - current : 0;
        if (remaining < minimum) {
            minimum = remaining;
        }
    }
    return (unsigned int)minimum;
}

static int parse_port(const char *text)
{
    long value = strtol(text, NULL, 10);
    return value >= 1 && value <= 65535 ? (int)value : 0;
}

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET sender_socket = INVALID_SOCKET;
    SOCKET receiver_socket = INVALID_SOCKET;
    struct sockaddr_in sender_listen_address;
    struct sockaddr_in receiver_address;
    struct sockaddr_in sender_address;
    int sender_address_length = sizeof(sender_address);
    int listen_port = DEFAULT_EMULATOR_PORT;
    int receiver_port = DEFAULT_RECEIVER_PORT;
    int idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
    int sender_known = 0;
    unsigned long long last_activity;
    char buffer[MAX_DATAGRAM_SIZE];
    ChannelConfig config;
    ChannelStats stats;
    PendingDatagram queue[MAX_PENDING_DATAGRAMS];

    memset(&stats, 0, sizeof(stats));
    memset(queue, 0, sizeof(queue));
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (argc > 1) {
        listen_port = parse_port(argv[1]);
    }
    if (argc > 2) {
        receiver_port = parse_port(argv[2]);
    }
    if (argc > 10) {
        idle_timeout_ms = atoi(argv[10]);
    }
    if (listen_port == 0 || receiver_port == 0 || idle_timeout_ms < 0) {
        fprintf(stderr, "Invalid port or idle timeout\n");
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    sender_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    receiver_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender_socket == INVALID_SOCKET ||
        receiver_socket == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        goto cleanup;
    }

    memset(&sender_listen_address, 0, sizeof(sender_listen_address));
    sender_listen_address.sin_family = AF_INET;
    sender_listen_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sender_listen_address.sin_port = htons((u_short)listen_port);
    if (bind(sender_socket, (struct sockaddr *)&sender_listen_address,
             sizeof(sender_listen_address)) == SOCKET_ERROR) {
        fprintf(stderr, "Sender-facing bind failed on port %d: %d\n",
                listen_port, WSAGetLastError());
        goto cleanup;
    }

    memset(&receiver_address, 0, sizeof(receiver_address));
    receiver_address.sin_family = AF_INET;
    receiver_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiver_address.sin_port = htons((u_short)receiver_port);

    channel_default_config(&config);
    if (argc > 3) config.loss_rate = atof(argv[3]);
    if (argc > 4) config.duplicate_rate = atof(argv[4]);
    if (argc > 5) config.corrupt_rate = atof(argv[5]);
    if (argc > 6) config.reorder_rate = atof(argv[6]);
    if (argc > 7) {
        config.min_delay_ms = (unsigned int)atoi(argv[7]);
        config.max_delay_ms = config.min_delay_ms;
    }
    if (argc > 8) config.jitter_ms =
        (unsigned int)strtoul(argv[8], NULL, 10);
    if (argc > 9) config.seed =
        (uint32_t)strtoul(argv[9], NULL, 10);
    if (!channel_validate_config(&config)) {
        fprintf(stderr, "Invalid channel configuration\n");
        goto cleanup;
    }
    channel_init(config.seed);

    printf("Sender-facing endpoint: 127.0.0.1:%d\n", listen_port);
    printf("Receiver-facing endpoint: 127.0.0.1:%d\n", receiver_port);
    printf("Settings: loss=%.2f duplicate=%.2f corrupt=%.2f "
           "reorder=%.2f delay=%u ms jitter=%u ms seed=%u "
           "idle_timeout=%d ms\n",
           config.loss_rate, config.duplicate_rate, config.corrupt_rate,
           config.reorder_rate, config.min_delay_ms, config.jitter_ms,
           config.seed, idle_timeout_ms);
    printf("READY\n");
    fflush(stdout);
    last_activity = now_ms();

    while (!stop_requested) {
        fd_set read_set;
        struct timeval timeout;
        unsigned int wait_ms = next_queue_timeout(queue);
        unsigned long long idle_elapsed = now_ms() - last_activity;
        int result;

        if (idle_timeout_ms > 0 && idle_elapsed >=
            (unsigned long long)idle_timeout_ms) {
            break;
        }
        if (idle_timeout_ms > 0 &&
            (unsigned int)(idle_timeout_ms - idle_elapsed) < wait_ms) {
            wait_ms = (unsigned int)(idle_timeout_ms - idle_elapsed);
        }

        FD_ZERO(&read_set);
        FD_SET(sender_socket, &read_set);
        FD_SET(receiver_socket, &read_set);
        timeout.tv_sec = (long)(wait_ms / 1000U);
        timeout.tv_usec = (long)((wait_ms % 1000U) * 1000U);
        result = select(0, &read_set, NULL, NULL, &timeout);
        if (result == SOCKET_ERROR) {
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            break;
        }
        if (result > 0 && FD_ISSET(sender_socket, &read_set)) {
            int received = recvfrom(
                sender_socket, buffer, sizeof(buffer), 0,
                (struct sockaddr *)&sender_address, &sender_address_length
            );
            if (received == SOCKET_ERROR) {
                fprintf(stderr, "Sender-facing receive failed: %d\n",
                        WSAGetLastError());
                break;
            }
            sender_known = 1;
            last_activity = now_ms();
            {
                uint8_t modified[MAX_DATAGRAM_SIZE];
                unsigned int delay_ms = 0;
                int copies = channel_process(
                    &config, &stats, (const uint8_t *)buffer,
                    (size_t)received, modified, sizeof(modified), &delay_ms
                );
                int copy;
                if (copies < 0) {
                    fprintf(stderr, "Channel processing failed\n");
                    break;
                }
                for (copy = 0; copy < copies; ++copy) {
                    unsigned int copy_delay = delay_ms + (copy * 1U);
                    if (!queue_datagram(queue, modified, received,
                                        receiver_socket, &receiver_address,
                                        copy_delay)) {
                        fprintf(stderr, "Forward queue is full\n");
                        stop_requested = 1;
                        break;
                    }
                }
            }
        }
        if (result > 0 && FD_ISSET(receiver_socket, &read_set)) {
            int received = recvfrom(
                receiver_socket, buffer, sizeof(buffer), 0,
                NULL, NULL
            );
            if (received == SOCKET_ERROR) {
                fprintf(stderr, "Receiver-facing receive failed: %d\n",
                        WSAGetLastError());
                break;
            }
            last_activity = now_ms();
            if (sender_known) {
                uint8_t modified[MAX_DATAGRAM_SIZE];
                unsigned int delay_ms = 0;
                int copies = channel_process(
                    &config, &stats, (const uint8_t *)buffer,
                    (size_t)received, modified, sizeof(modified), &delay_ms
                );
                int copy;
                if (copies < 0) {
                    fprintf(stderr, "Channel processing failed\n");
                    break;
                }
                for (copy = 0; copy < copies; ++copy) {
                    if (!queue_datagram(queue, modified, received,
                                        sender_socket, &sender_address,
                                        delay_ms + (copy * 1U))) {
                        fprintf(stderr, "Forward queue is full\n");
                        stop_requested = 1;
                        break;
                    }
                }
            }
        }
        if (flush_due_datagrams(queue) < 0) {
            break;
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
    printf("Packets reordered: %llu\n",
           (unsigned long long)stats.packets_reordered);

cleanup:
    if (sender_socket != INVALID_SOCKET) closesocket(sender_socket);
    if (receiver_socket != INVALID_SOCKET) closesocket(receiver_socket);
    WSACleanup();
    return 0;
}
