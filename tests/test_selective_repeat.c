#include <stdio.h>
#include <string.h>
#include <winsock2.h>

#include "../include/selective_repeat.h"

static int failures;

static Packet data_packet(uint32_t sequence, const char *text)
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

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

int main(void)
{
    WSADATA wsa;
    SelectiveRepeatState sender;
    SelectiveRepeatReceiverState receiver;
    struct sockaddr_in peer;
    uint8_t payload = 0x24;
    Packet packet;

    check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "Winsock startup");
    memset(&peer, 0, sizeof(peer));
    sr_init(&sender, INVALID_SOCKET, &peer);
    check(sender.base == 0 && sender.next_sequence_number == 0,
          "sender initialization");
    check(sr_send(NULL, &payload, 1) == 0, "NULL sender rejected");
    check(sr_send(&sender, NULL, 1) == 0, "NULL payload rejected");
    check(sr_send(&sender, &payload, (uint16_t)(MAX_PAYLOAD_SIZE + 1)) == 0,
          "oversized payload rejected");
    check(sr_send(&sender, &payload, 1) == 0,
          "send failure does not advance sequence");
    check(sender.next_sequence_number == 0 && sender.packets_sent == 0,
          "failed send leaves sender state unchanged");

    sender.base = 3;
    sender.next_sequence_number = 3 + SR_WINDOW_SIZE;
    check(sr_send(&sender, &payload, 1) == 0, "full window rejected");

    sr_receiver_init(&receiver, INVALID_SOCKET, &peer);
    check(sr_receiver_process(NULL, &packet) == -1, "NULL receiver rejected");

    packet = data_packet(2, "out of order");
    check(sr_receiver_process(&receiver, &packet) == -1,
          "receiver reports ACK send failure");
    check(receiver.base == 0 && receiver.present[2] &&
          receiver.packets_received == 1,
          "in-window packet is buffered without advancing base");

    packet = data_packet(0, "first");
    check(sr_receiver_process(&receiver, &packet) == -1,
          "second ACK send failure");
    check(receiver.present[0] && receiver.base == 0,
          "buffer remains until ACK path succeeds");

    packet = data_packet(0, "duplicate");
    check(sr_receiver_process(&receiver, &packet) == -1,
          "duplicate ACK send failure");
    check(receiver.duplicate_packets == 1, "duplicate packet counted");

    packet = data_packet(SR_WINDOW_SIZE, "outside");
    check(sr_receiver_process(&receiver, &packet) == -1,
          "out-of-window ACK attempt");
    check(receiver.out_of_window_packets == 1, "out-of-window packet counted");

    packet = data_packet(1, "bad");
    packet.checksum++;
    check(sr_receiver_process(&receiver, &packet) == -1,
          "corrupt packet rejected");
    check(receiver.corrupted_packets == 1, "corrupt packet counted");

    WSACleanup();
    if (failures == 0) printf("Selective Repeat unit tests passed\n");
    return failures != 0;
}
