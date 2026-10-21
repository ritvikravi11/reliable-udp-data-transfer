CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -lws2_32

SRC = src

all: test_packet test_timer test_timer_manager test_sha256 test_channel test_go_back_n test_selective_repeat sender receiver protocols file_transfer

integration_test: file_transfer
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

sender: $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o sender.exe $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

receiver: $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o receiver.exe $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

protocols: $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/packet.c $(SRC)/timer.c

file_transfer: $(SRC)/file_transfer.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/sha256.c
	$(CC) $(CFLAGS) -o file_transfer.exe $(SRC)/file_transfer.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(SRC)/go_back_n.c $(SRC)/selective_repeat.c $(SRC)/sha256.c $(LDFLAGS)

clean:
	-del /Q test_packet.exe test_timer.exe test_timer_manager.exe test_sha256.exe test_channel.exe test_go_back_n.exe test_selective_repeat.exe sender.exe receiver.exe file_transfer.exe *.o 2>NUL

.PHONY: all clean integration_test