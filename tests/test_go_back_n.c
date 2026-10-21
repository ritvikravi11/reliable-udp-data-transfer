#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include "../include/go_back_n.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static Packet make_data(uint32_t sequence, const char *text)
{
    Packet packet;
    size_t length = strlen(text);

    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_DATA;
    packet.sequence_number = sequence;
    packet.payload_length = (uint16_t)length;
    memcpy(packet.payload, text, length);
    packet.checksum = calculate_checksum(&packet);
    return packet;
}

static SOCKET make_bound_socket(struct sockaddr_in *address)
{
    SOCKET socket_handle;
    int address_length = sizeof(*address);

    socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address->sin_port = htons(0);
    if (bind(socket_handle, (struct sockaddr *)address,
             sizeof(*address)) == SOCKET_ERROR ||
        getsockname(socket_handle, (struct sockaddr *)address,
                    &address_length) == SOCKET_ERROR) {
        closesocket(socket_handle);
        return INVALID_SOCKET;
    }
    return socket_handle;
}

static int receive_packet(
    SOCKET socket_handle,
    Packet *packet
)
{
    uint8_t buffer[GBN_BUFFER_SIZE];
    fd_set read_set;
    struct timeval timeout;
    int received;

    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (select(0, &read_set, NULL, NULL, &timeout) <= 0) {
        return 0;
    }
    received = recvfrom(socket_handle, (char *)buffer, sizeof(buffer), 0,
                        NULL, NULL);
    return received >= 15 &&
        deserialize_packet(packet, buffer, (size_t)received) == 0 &&
        packet_validate(packet);
}

static void process_one_data(
    SOCKET receiver_socket,
    GoBackNReceiverState *receiver,
    int expected_result,
    const char *name
)
{
    Packet packet;

    check(receive_packet(receiver_socket, &packet), "receive DATA packet");
    check(gbn_receiver_process(receiver, &packet) == expected_result, name);
}

int main(void)
{
    WSADATA wsa;
    SOCKET sender_socket = INVALID_SOCKET;
    SOCKET receiver_socket = INVALID_SOCKET;
    struct sockaddr_in sender_address;
    struct sockaddr_in receiver_address;
    GoBackNState sender;
    GoBackNReceiverState receiver;
    Packet packet;
    uint8_t payload = 0x42;
    int i;

    check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "Winsock startup");
    sender_socket = make_bound_socket(&sender_address);
    receiver_socket = make_bound_socket(&receiver_address);
    check(sender_socket != INVALID_SOCKET &&
          receiver_socket != INVALID_SOCKET, "UDP sockets created");

    gbn_init(&sender, sender_socket, &receiver_address);
    gbn_receiver_init(&receiver, receiver_socket, &sender_address);
    check(sender.window_size == GBN_WINDOW_SIZE, "default window size");
    check(gbn_set_window_size(&sender, 2), "configure window size");
    check(sender.window_size == 2, "configured window size stored");
    check(!gbn_set_window_size(&sender, GBN_MAX_WINDOW_SIZE + 1U),
          "reject oversized window");

    check(gbn_send(&sender, &payload, 1), "send first packet");
    check(gbn_send(&sender, &payload, 1), "send second packet");
    check(!gbn_send(&sender, &payload, 1), "full window rejects packet");
    printf("stage=initial-send\n"); fflush(stdout);

    process_one_data(receiver_socket, &receiver, 1,
                     "deliver first in-order packet");
    process_one_data(receiver_socket, &receiver, 1,
                     "deliver second in-order packet");
    check(gbn_receive(&sender, NULL) == 1, "advance on first ACK");
    check(gbn_receive(&sender, NULL) == 1, "advance on second ACK");
    printf("stage=acks\n"); fflush(stdout);
    check(sender.base == 2 && sender.next_sequence_number == 2,
          "cumulative ACK advances sender window");

    packet = make_data(1, "duplicate");
    check(gbn_receiver_process(&receiver, &packet) == 0,
          "duplicate packet discarded");
    check(receiver.duplicate_packets == 1 &&
          receiver.packets_delivered == 2,
          "duplicate is not delivered twice");
    check(receive_packet(sender_socket, &packet), "receive duplicate ACK");

    packet = make_data(4, "out of order");
    check(gbn_receiver_process(&receiver, &packet) == 0,
          "out-of-order packet discarded");
    check(receiver.out_of_order_packets == 1 &&
          receiver.packets_delivered == 2,
          "out-of-order packet is not delivered");
    check(receive_packet(sender_socket, &packet), "receive repeat ACK");

    packet = make_data(2, "corrupt");
    packet.checksum++;
    check(gbn_receiver_process(&receiver, &packet) == -1,
          "corrupted packet rejected");
    check(receiver.corrupted_packets == 1, "corruption counted");
    printf("stage=receiver-cases\n"); fflush(stdout);

    gbn_init(&sender, sender_socket, &receiver_address);
    check(gbn_send(&sender, &payload, 1), "send timeout candidate");
    Sleep(1100);
    check(gbn_retransmit_expired(&sender) == 1,
          "timeout retransmits outstanding packet");
    printf("stage=first-retry\n"); fflush(stdout);
    check(sender.retransmissions == 1 && sender.packets_sent == 2,
          "retransmission counters updated");
    for (i = 0; i < GBN_MAX_RETRIES - 1; ++i) {
        sender.timer.start_time -= GBN_DEFAULT_RTO + 1.0;
        check(gbn_retransmit_expired(&sender) == 1,
              "retry retransmission succeeds");
    }
    check(sender.retry_count == GBN_MAX_RETRIES,
          "retry count reaches configured limit");
    sender.timer.start_time -= GBN_DEFAULT_RTO + 1.0;
    check(gbn_retransmit_expired(&sender) == -1,
          "retry-limit failure is reported");

    if (sender_socket != INVALID_SOCKET) {
        closesocket(sender_socket);
    }
    if (receiver_socket != INVALID_SOCKET) {
        closesocket(receiver_socket);
    }
    WSACleanup();

    if (failures == 0) {
        printf("Go-Back-N tests passed\n");
    }
    return failures != 0;
}
