#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include "../include/go_back_n.h"
#include "../include/selective_repeat.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static SOCKET bind_loopback(struct sockaddr_in *address)
{
    SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int length = sizeof(*address);
    if (socket_handle == INVALID_SOCKET) return INVALID_SOCKET;
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address->sin_port = htons(0);
    if (bind(socket_handle, (struct sockaddr *)address, sizeof(*address)) ==
            SOCKET_ERROR ||
        getsockname(socket_handle, (struct sockaddr *)address, &length) ==
            SOCKET_ERROR) {
        closesocket(socket_handle);
        return INVALID_SOCKET;
    }
    return socket_handle;
}

static int receive_packet(SOCKET socket_handle, Packet *packet)
{
    uint8_t buffer[15 + MAX_PAYLOAD_SIZE];
    struct timeval timeout = {2, 0};
    fd_set read_set;
    int length;

    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);
    if (select(0, &read_set, NULL, NULL, &timeout) <= 0) return 0;
    length = recvfrom(socket_handle, (char *)buffer, sizeof(buffer), 0,
                      NULL, NULL);
    return length >= 15 &&
        deserialize_packet(packet, buffer, (size_t)length) == 0 &&
        packet_validate(packet);
}

static void test_wrap_helpers(void)
{
    check(sequence_in_window(UINT32_MAX - 1U, UINT32_MAX, 4),
          "wrap window includes UINT32_MAX");
    check(sequence_in_window(UINT32_MAX - 1U, 0, 4),
          "wrap window includes zero");
    check(sequence_before(UINT32_MAX, 0), "wrap ordering");
}

static void test_gbn_karn(void)
{
    SOCKET sender_socket, receiver_socket;
    struct sockaddr_in sender_address, receiver_address;
    GoBackNState sender;
    GoBackNReceiverState receiver;
    Packet packet;
    uint8_t value = 7;

    sender_socket = bind_loopback(&sender_address);
    receiver_socket = bind_loopback(&receiver_address);
    check(sender_socket != INVALID_SOCKET && receiver_socket != INVALID_SOCKET,
          "GBN feature sockets");
    gbn_init(&sender, sender_socket, &receiver_address);
    gbn_receiver_init(&receiver, receiver_socket, &sender_address);
    check(gbn_set_initial_rto(&sender, GBN_MIN_RTO),
          "GBN initial RTO setter");
    check(gbn_send(&sender, &value, 1), "GBN Karn send");
    check(receive_packet(receiver_socket, &packet), "GBN Karn first receive");
    Sleep(650);
    check(gbn_retransmit_expired(&sender) == 1, "GBN timeout retransmit");
    check(receive_packet(receiver_socket, &packet), "GBN Karn retry receive");
    check(gbn_receiver_process(&receiver, &packet) == 1,
          "GBN duplicate retry handling");
    check(gbn_receive(&sender, NULL) == 1, "GBN delayed ACK receive");
    check(sender.accepted_rtt_samples == 0 &&
          sender.rejected_rtt_samples == 1,
          "GBN Karn rejection");
    check(sender.timeout_count == 1 && sender.rto >= GBN_MIN_RTO * 2.0,
          "GBN timeout diagnostics");
    closesocket(sender_socket);
    closesocket(receiver_socket);
}

static void test_sr_karn(void)
{
    SOCKET sender_socket, receiver_socket;
    struct sockaddr_in sender_address, receiver_address;
    SelectiveRepeatState sender;
    SelectiveRepeatReceiverState receiver;
    Packet packet;
    uint8_t value = 9;

    sender_socket = bind_loopback(&sender_address);
    receiver_socket = bind_loopback(&receiver_address);
    check(sender_socket != INVALID_SOCKET && receiver_socket != INVALID_SOCKET,
          "SR feature sockets");
    sr_init(&sender, sender_socket, &receiver_address);
    sr_receiver_init(&receiver, receiver_socket, &sender_address);
    check(sr_set_initial_rto(&sender, SR_MIN_RTO),
          "SR initial RTO setter");
    check(sr_send(&sender, &value, 1), "SR Karn send");
    check(receive_packet(receiver_socket, &packet), "SR Karn first receive");
    Sleep(650);
    check(sr_retransmit_expired(&sender) == 1, "SR timeout retransmit");
    check(receive_packet(receiver_socket, &packet), "SR Karn retry receive");
    check(sr_receiver_process(&receiver, &packet) == 0,
          "SR duplicate retry handling");
    check(sr_receive(&sender, NULL) == 1, "SR delayed ACK receive");
    check(sender.accepted_rtt_samples == 0 &&
          sender.rejected_rtt_samples == 1,
          "SR Karn rejection");
    check(sender.timeout_count == 1 && sender.rto >= SR_MIN_RTO * 2.0,
          "SR timeout diagnostics");
    closesocket(sender_socket);
    closesocket(receiver_socket);
}

int main(void)
{
    WSADATA wsa;
    check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "Winsock startup");
    test_wrap_helpers();
    test_gbn_karn();
    test_sr_karn();
    WSACleanup();
    if (failures == 0) printf("Protocol feature tests passed\n");
    return failures != 0;
}
