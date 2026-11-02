#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <winsock2.h>

#include "../include/packet.h"
#include "../include/stop_wait.h"
#include "../include/go_back_n.h"
#include "../include/selective_repeat.h"
#include "../include/sha256.h"

typedef enum { PROTO_STOP_WAIT, PROTO_GBN, PROTO_SR } Protocol;

#define CONTROL_SEQUENCE 0xffffffffU
#define CONTROL_TIMEOUT_SECONDS 5

static double now_seconds(void);

static void usage(void)
{
    fprintf(stderr, "usage: file_transfer send|receive --protocol "
            "stop_wait|go_back_n|selective_repeat --input FILE "
            "--output FILE [--host HOST] [--port PORT] [--window N] "
            "[--loss N] [--seed N]\n");
}

static int argument(int argc, char **argv, const char *name, const char **value)
{
    int i;
    for (i = 2; i < argc - 1; ++i)
        if (strcmp(argv[i], name) == 0) { *value = argv[i + 1]; return 1; }
    return 0;
}

static int protocol_value(const char *name, Protocol *protocol)
{
    if (strcmp(name, "stop_wait") == 0) *protocol = PROTO_STOP_WAIT;
    else if (strcmp(name, "go_back_n") == 0) *protocol = PROTO_GBN;
    else if (strcmp(name, "selective_repeat") == 0) *protocol = PROTO_SR;
    else return 0;
    return 1;
}

static const char *protocol_name(Protocol p)
{
    return p == PROTO_STOP_WAIT ? "stop_wait" :
           (p == PROTO_GBN ? "go_back_n" : "selective_repeat");
}

static int send_packet(SOCKET socket, const struct sockaddr_in *peer,
                       const Packet *packet)
{
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];
    int length = serialize_packet(packet, buffer);
    if (length <= 0) return 0;
    return sendto(socket, (const char *)buffer, length, 0,
                  (const struct sockaddr *)peer, sizeof(*peer)) == length;
}

static int receive_packet(SOCKET socket, Packet *packet,
                          struct sockaddr_in *from)
{
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];
    int from_length = sizeof(*from);
    int length = recvfrom(socket, (char *)buffer, sizeof(buffer), 0,
                          (struct sockaddr *)from, &from_length);
    if (length == SOCKET_ERROR)
        return -1;
    if (length < 15 ||
        deserialize_packet(packet, buffer, (size_t)length) != 0 ||
        !packet_validate(packet))
        return 0;
    return 1;
}

static int send_control(SOCKET socket, const struct sockaddr_in *peer,
                        PacketType type, uint32_t sequence,
                        const uint8_t *data, uint16_t length)
{
    Packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.sequence_number = sequence;
    packet.payload_length = length;
    if (length) memcpy(packet.payload, data, length);
    packet.checksum = calculate_checksum(&packet);
    return send_packet(socket, peer, &packet);
}

static int send_control_repeated(SOCKET socket, const struct sockaddr_in *peer,
                                 PacketType type, uint32_t sequence,
                                 const uint8_t *data, uint16_t length)
{
    int attempt, sent = 0;
    for (attempt = 0; attempt < 3; ++attempt)
        sent |= send_control(socket, peer, type, sequence, data, length);
    return sent;
}

static int wait_for_control_ack(SOCKET socket, uint32_t number)
{
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];
    double deadline = now_seconds() + CONTROL_TIMEOUT_SECONDS;

    while (now_seconds() < deadline) {
        double remaining = deadline - now_seconds();
        fd_set read_set;
        struct timeval timeout;
        int result;
        Packet packet;
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);
        timeout.tv_sec = (long)remaining;
        timeout.tv_usec = (long)((remaining - timeout.tv_sec) * 1000000.0);
        result = select(0, &read_set, NULL, NULL, &timeout);
        if (result == SOCKET_ERROR) return 0;
        if (result == 0) break;
        result = recvfrom(socket, (char *)buffer, sizeof(buffer), 0, NULL, NULL);
        if (result == SOCKET_ERROR ||
            deserialize_packet(&packet, buffer, (size_t)result) != 0 ||
            !packet_validate(&packet))
            continue;
        if (packet.type == PACKET_ACK &&
            packet.acknowledgement_number == number)
            return 1;
    }
    return 0;
}

static int send_control_ack(SOCKET socket, const struct sockaddr_in *peer)
{
    Packet ack;
    memset(&ack, 0, sizeof(ack));
    ack.type = PACKET_ACK;
    ack.sequence_number = CONTROL_SEQUENCE;
    ack.acknowledgement_number = CONTROL_SEQUENCE;
    ack.checksum = calculate_checksum(&ack);
    return send_packet(socket, peer, &ack);
}

static int exchange_control(SOCKET socket, const struct sockaddr_in *peer,
                            PacketType type, const uint8_t *data,
                            uint16_t length)
{
    int attempt;
    for (attempt = 0; attempt < 3; ++attempt) {
        if (send_control_repeated(socket, peer, type, CONTROL_SEQUENCE,
                                   data, length) &&
            wait_for_control_ack(socket, CONTROL_SEQUENCE))
            return 1;
    }
    return 0;
}

static double now_seconds(void)
{
    return (double)GetTickCount64() / 1000.0;
}

static int sender(
    const char *input,
    const char *host,
    int port,
    Protocol protocol,
    uint32_t window_size,
    double initial_rto
)
{
    FILE *file = fopen(input, "rb");
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in peer;
    Sha256Context hash;
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE], metadata[256];
    uint8_t chunk[MAX_PAYLOAD_SIZE];
    size_t count;
    uint64_t bytes = 0;
    int result = 0;
    double started;
    GoBackNState gbn = {0};
    SelectiveRepeatState sr = {0};
    StopWaitState sw = {0};

    if (!file) { perror("input"); return 1; }
    sha256_init(&hash);
    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        sha256_update(&hash, chunk, count); bytes += count;
    }
    if (ferror(file)) { fclose(file); return 1; }
    sha256_final(&hash, digest); sha256_hex(digest, hex);
    rewind(file);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { fclose(file); return 1; }
    memset(&peer, 0, sizeof(peer)); peer.sin_family = AF_INET;
    peer.sin_port = htons((u_short)port); peer.sin_addr.s_addr = inet_addr(host);
    if (peer.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *entry = gethostbyname(host);
        if (!entry) { closesocket(sock); fclose(file); return 1; }
        memcpy(&peer.sin_addr, entry->h_addr_list[0], entry->h_length);
    }

    (void)snprintf(metadata, sizeof(metadata), "%llu %s",
                   (unsigned long long)bytes, hex);
    started = now_seconds();
    if (!exchange_control(sock, &peer, PACKET_START,
                          (const uint8_t *)metadata,
                          (uint16_t)strlen(metadata))) goto done;

    if (protocol == PROTO_STOP_WAIT) stop_wait_init(&sw, sock, &peer);
    else if (protocol == PROTO_GBN) {
        gbn_init(&gbn, sock, &peer);
        if (!gbn_set_window_size(&gbn, window_size) ||
            !gbn_set_initial_rto(&gbn, initial_rto)) goto done;
    }
    else {
        sr_init(&sr, sock, &peer);
        if (!sr_set_window_size(&sr, window_size) ||
            !sr_set_initial_rto(&sr, initial_rto)) goto done;
    }

    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        int sent = protocol == PROTO_STOP_WAIT ?
            stop_wait_send(&sw, chunk, (uint16_t)count) :
            (protocol == PROTO_GBN ? gbn_send(&gbn, chunk, (uint16_t)count) :
                                     sr_send(&sr, chunk, (uint16_t)count));
        if (!sent) {
            int flushed = protocol == PROTO_GBN ? gbn_flush(&gbn) :
                          (protocol == PROTO_SR ? sr_flush(&sr) : 0);
            if (!flushed) goto done;
            sent = protocol == PROTO_GBN ? gbn_send(&gbn, chunk, (uint16_t)count) :
                  (protocol == PROTO_SR ? sr_send(&sr, chunk, (uint16_t)count) : 0);
            if (!sent) goto done;
        }
        if (protocol == PROTO_GBN &&
            gbn.next_sequence_number - gbn.base >= gbn.window_size &&
            !gbn_flush(&gbn)) goto done;
        if (protocol == PROTO_SR &&
            sr.next_sequence_number - sr.base >= sr.window_size &&
            !sr_flush(&sr)) goto done;
    }
    if (protocol == PROTO_GBN && !gbn_flush(&gbn)) goto done;
    if (protocol == PROTO_SR && !sr_flush(&sr)) goto done;
    if (!exchange_control(sock, &peer, PACKET_END, NULL, 0)) goto done;
    result = 1;
done:
    {
        int retransmissions = protocol == PROTO_STOP_WAIT ? sw.retransmissions :
            (protocol == PROTO_GBN ? gbn.retransmissions : sr.retransmissions);
        double elapsed = now_seconds() - started;
        printf("METRIC protocol=%s status=%s bytes=%llu elapsed_seconds=%.6f "
               "goodput_bytes_per_sec=%.3f packets_sent=%d "
               "original_transmissions=%d retransmissions=%d "
               "timeout_count=%d ack_count=%d "
               "duplicate_ack_count=%d rtt_samples_accepted=%d "
               "rtt_samples_rejected=%d initial_rto=%.3f final_rto=%.3f "
               "fast_retransmissions=%d retransmission_rate=%.6f "
               "source_sha256=%s window_size=%d\n",
               protocol_name(protocol), result ? "success" : "failure",
               (unsigned long long)bytes, elapsed,
               elapsed > 0 ? bytes / elapsed : 0.0,
               protocol == PROTO_STOP_WAIT ? sw.packets_sent :
               (protocol == PROTO_GBN ? gbn.packets_sent : sr.packets_sent),
               protocol == PROTO_STOP_WAIT ?
                   (sw.packets_sent - sw.retransmissions) :
                   (protocol == PROTO_GBN ? gbn.original_transmissions :
                                           sr.original_transmissions),
               retransmissions,
               protocol == PROTO_STOP_WAIT ? sw.timeout_count :
               (protocol == PROTO_GBN ? gbn.timeout_count : sr.timeout_count),
               protocol == PROTO_STOP_WAIT ? sw.ack_count :
               (protocol == PROTO_GBN ? gbn.ack_count : sr.ack_count),
               protocol == PROTO_STOP_WAIT ? sw.duplicate_ack_count :
               (protocol == PROTO_GBN ? gbn.duplicate_ack_count_total :
                                        sr.duplicate_ack_count_total),
               protocol == PROTO_STOP_WAIT ? sw.accepted_rtt_samples :
               (protocol == PROTO_GBN ? gbn.accepted_rtt_samples :
                                         sr.accepted_rtt_samples),
               protocol == PROTO_STOP_WAIT ? sw.rejected_rtt_samples :
               (protocol == PROTO_GBN ? gbn.rejected_rtt_samples :
                                         sr.rejected_rtt_samples),
               protocol == PROTO_STOP_WAIT ? sw.initial_rto :
               (protocol == PROTO_GBN ? gbn.initial_rto : sr.initial_rto),
               protocol == PROTO_STOP_WAIT ? sw.rtt.rto :
               (protocol == PROTO_GBN ? gbn.rto : sr.rto),
               protocol == PROTO_STOP_WAIT ? 0 :
               (protocol == PROTO_GBN ? gbn.fast_retransmissions : 0),
               bytes > 0 ? (double)retransmissions /
                   (double)(protocol == PROTO_STOP_WAIT ?
                            (sw.packets_sent - sw.retransmissions) :
                            (protocol == PROTO_GBN ? gbn.original_transmissions :
                                                     sr.original_transmissions)) : 0.0,
               hex,
               protocol == PROTO_STOP_WAIT ? 1 :
               (protocol == PROTO_GBN ? gbn.window_size : sr.window_size));
    }
    fclose(file); closesocket(sock);
    return result ? 0 : 1;
}

static int receiver(const char *output, int port, Protocol protocol,
                    uint32_t window_size)
{
    FILE *file = NULL;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in address, peer;
    Packet packet;
    char metadata[256], expected_hash[SHA256_HEX_SIZE], received_hash[SHA256_HEX_SIZE];
    uint64_t expected_bytes = 0, bytes = 0;
    Sha256Context hash;
    double started = 0.0;
    int done = 0, result = 1, hash_finalized = 0;
    int packets_received = 0;
    uint32_t expected = 0;
    Packet ordered[SR_MAX_WINDOW_SIZE];
    unsigned char ordered_present[SR_MAX_WINDOW_SIZE] = {0};
    SelectiveRepeatReceiverState sr_receiver = {0};
    GoBackNReceiverState gbn_receiver = {0};
    StopWaitState sw_receiver = {0};

    expected_hash[0] = '\0';
    received_hash[0] = '\0';
    sha256_init(&hash);
    started = now_seconds();
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return 1;
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons((u_short)port);
    if (bind(sock, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) goto fail;
    if (receive_packet(sock, &packet, &peer) <= 0 ||
        packet.type != PACKET_START ||
        packet.sequence_number != CONTROL_SEQUENCE ||
        packet.payload_length >= sizeof(metadata)) goto fail;
    memcpy(metadata, packet.payload, packet.payload_length); metadata[packet.payload_length] = '\0';
    if (sscanf(metadata, "%llu %64s", (unsigned long long *)&expected_bytes,
               expected_hash) != 2) goto fail;
    if (!send_control_ack(sock, &peer)) goto fail;
    file = fopen(output, "wb"); if (!file) goto fail;
    if (protocol == PROTO_STOP_WAIT) stop_wait_init(&sw_receiver, sock, &peer);
    else if (protocol == PROTO_GBN) gbn_receiver_init(&gbn_receiver, sock, &peer);
    else {
        sr_receiver_init(&sr_receiver, sock, &peer);
        if (!sr_receiver_set_window_size(&sr_receiver, window_size)) {
            goto fail;
        }
    }

    while (!done) {
        int received_result = receive_packet(sock, &packet, &peer);
        if (received_result < 0) {
            break;
        }
        if (received_result == 0) {
            continue;
        }
        packets_received++;
        if (packet.type == PACKET_START &&
            packet.sequence_number == CONTROL_SEQUENCE) {
            send_control_ack(sock, &peer);
            continue;
        }
        if (packet.type == PACKET_END &&
            packet.sequence_number == CONTROL_SEQUENCE) {
            send_control_ack(sock, &peer);
            done = 1;
            break;
        }
        if (packet.type != PACKET_DATA) continue;
        if (protocol == PROTO_STOP_WAIT) {
            if (packet.sequence_number == expected) {
                fwrite(packet.payload, 1, packet.payload_length, file);
                sha256_update(&hash, packet.payload, packet.payload_length);
                bytes += packet.payload_length; expected++;
            }
            stop_wait_send_ack(&sw_receiver, packet.sequence_number);
        } else if (protocol == PROTO_GBN) {
            uint32_t before = gbn_receiver.expected_sequence_number;
            gbn_receiver_process(&gbn_receiver, &packet);
            if (packet.sequence_number == before) {
                fwrite(packet.payload, 1, packet.payload_length, file);
                sha256_update(&hash, packet.payload, packet.payload_length);
                bytes += packet.payload_length;
            }
        } else {
            /* Keep a separate application queue because the protocol receiver
             * advances its internal buffer while acknowledging the packet. */
            sr_receiver_process(&sr_receiver, &packet);
            if (sequence_in_window(expected, packet.sequence_number,
                                   sr_receiver.window_size)) {
                unsigned int slot =
                    packet.sequence_number % sr_receiver.window_size;
                ordered[slot] = packet;
                ordered_present[slot] = 1;
            }
            while (ordered_present[expected % sr_receiver.window_size]) {
                Packet *delivered =
                    &ordered[expected % sr_receiver.window_size];
                fwrite(delivered->payload, 1, delivered->payload_length, file);
                sha256_update(&hash, delivered->payload, delivered->payload_length);
                bytes += delivered->payload_length;
                ordered_present[expected % sr_receiver.window_size] = 0;
                expected++;
            }
        }
    }
    if (done) {
        /* Keep the endpoint alive briefly so a lost END ACK can be recovered. */
        double deadline = now_seconds() + CONTROL_TIMEOUT_SECONDS;
        while (now_seconds() < deadline) {
            fd_set read_set;
            struct timeval timeout;
            double left = deadline - now_seconds();
            int wait_result;
            FD_ZERO(&read_set);
            FD_SET(sock, &read_set);
            timeout.tv_sec = (long)left;
            timeout.tv_usec = (long)((left - timeout.tv_sec) * 1000000.0);
            wait_result = select(0, &read_set, NULL, NULL, &timeout);
            if (wait_result <= 0) break;
            if (receive_packet(sock, &packet, &peer) > 0 &&
                packet.type == PACKET_END &&
                packet.sequence_number == CONTROL_SEQUENCE)
                send_control_ack(sock, &peer);
        }
    }
    fflush(file);
    sha256_final(&hash, packet.payload);
    sha256_hex(packet.payload, received_hash);
    hash_finalized = 1;
    if (!done || bytes != expected_bytes || strcmp(expected_hash, received_hash) != 0) result = 1;
    else result = 0;
fail:
    if (!hash_finalized) {
        sha256_final(&hash, packet.payload);
        sha256_hex(packet.payload, received_hash);
    }
    {
        double elapsed = now_seconds() - started;
        printf("METRIC protocol=%s status=%s bytes=%llu elapsed_seconds=%.6f "
               "goodput_bytes_per_sec=%.3f packets_received=%d "
               "source_sha256=%s received_sha256=%s "
               "window_size=%d\n",
               protocol_name(protocol), result == 0 ? "success" : "failure",
               (unsigned long long)bytes, elapsed,
               elapsed > 0.0 ? bytes / elapsed : 0.0,
               packets_received,
               expected_hash[0] ? expected_hash : "unknown",
               received_hash[0] ? received_hash : "unknown",
               protocol == PROTO_STOP_WAIT ? 1 : window_size);
    }
    if (file) fclose(file);
    closesocket(sock);
    return result;
}

int main(int argc, char **argv)
{
    WSADATA wsa;
    const char *protocol_text, *input, *output, *host = "127.0.0.1",
               *port_text = "9000", *window_text = "4",
               *initial_rto_text = "1.0";
    Protocol protocol;
    int port, status;
    if (argc < 2 || (strcmp(argv[1], "send") != 0 && strcmp(argv[1], "receive") != 0) ||
        !argument(argc, argv, "--protocol", &protocol_text) ||
        !argument(argc, argv, "--input", &input) ||
        !argument(argc, argv, "--output", &output) ||
        !protocol_value(protocol_text, &protocol)) { usage(); return 2; }
    (void)argument(argc, argv, "--host", &host);
    (void)argument(argc, argv, "--port", &port_text);
    (void)argument(argc, argv, "--window", &window_text);
    (void)argument(argc, argv, "--initial-rto", &initial_rto_text);
    port = atoi(port_text);
    {
        long parsed_window = strtol(window_text, NULL, 10);
        double initial_rto = strtod(initial_rto_text, NULL);
        if (port < 1 || port > 65535 || parsed_window < 1 ||
            parsed_window > GBN_MAX_WINDOW_SIZE ||
            (protocol == PROTO_SR && parsed_window > SR_MAX_WINDOW_SIZE) ||
            initial_rto < GBN_MIN_RTO || initial_rto > GBN_MAX_RTO) {
            return 2;
        }
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
        status = strcmp(argv[1], "send") == 0 ?
            sender(input, host, port, protocol, (uint32_t)parsed_window,
                   initial_rto) :
            receiver(output, port, protocol, (uint32_t)parsed_window);
    }
    WSACleanup();
    return status;
}
