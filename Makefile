CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -lws2_32

SRC = src

all: test_packet test_timer test_timer_manager test_sha256 test_channel test_go_back_n test_selective_repeat test_protocol_features test_udp_sender sender receiver protocols file_transfer emulator

integration_test: file_transfer emulator
	python scripts/run_integration_tests.py

test_packet: tests/test_packet.c $(SRC)/packet.c
	$(CC) $(CFLAGS) -o test_packet.exe tests/test_packet.c $(SRC)/packet.c

test_timer: tests/test_timer.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_timer.exe tests/test_timer.c $(SRC)/timer.c $(LDFLAGS)

test_timer_manager: tests/test_timer_manager.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_timer_manager.exe tests/test_timer_manager.c $(SRC)/timer.c $(LDFLAGS)

test_sha256: tests/test_sha256.c $(SRC)/sha256.c
	$(CC) $(CFLAGS) -o test_sha256.exe tests/test_sha256.c $(SRC)/sha256.c

test_channel: tests/test_channel.c emulator/channel.c emulator/channel.h
	$(CC) $(CFLAGS) -Iemulator -o test_channel.exe tests/test_channel.c emulator/channel.c

test_go_back_n: tests/test_go_back_n.c $(SRC)/go_back_n.c $(SRC)/packet.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_go_back_n.exe tests/test_go_back_n.c $(SRC)/go_back_n.c $(SRC)/packet.c $(SRC)/timer.c $(LDFLAGS)

test_selective_repeat: tests/test_selective_repeat.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_selective_repeat.exe tests/test_selective_repeat.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c $(LDFLAGS)

test_protocol_features: tests/test_protocol_features.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_protocol_features.exe tests/test_protocol_features.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c $(LDFLAGS)

test_udp_sender: tests/test_udp_sender.c $(SRC)/packet.c
	$(CC) $(CFLAGS) -o test_udp_sender.exe tests/test_udp_sender.c $(SRC)/packet.c $(LDFLAGS)

sender: $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o sender.exe $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

receiver: $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o receiver.exe $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

protocols: $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c

file_transfer: $(SRC)/file_transfer.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/sha256.c
	$(CC) $(CFLAGS) -o file_transfer.exe $(SRC)/file_transfer.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/sha256.c $(LDFLAGS)

emulator: emulator/udp_channel.c emulator/channel.c emulator/channel.h $(SRC)/packet.c
	$(CC) $(CFLAGS) -Iemulator -o emulator/udp_channel.exe emulator/udp_channel.c emulator/channel.c $(SRC)/packet.c $(LDFLAGS)

clean:
	$(RM) test_packet.exe test_timer.exe test_timer_manager.exe test_sha256.exe test_channel.exe test_go_back_n.exe test_selective_repeat.exe test_protocol_features.exe test_udp_sender.exe sender.exe receiver.exe file_transfer.exe emulator/udp_channel.exe *.o

.PHONY: all clean integration_test emulator