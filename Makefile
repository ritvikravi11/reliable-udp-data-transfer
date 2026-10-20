CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -lws2_32

SRC = src

all: test_packet test_timer test_timer_manager sender receiver

test_packet: tests/test_packet.c $(SRC)/packet.c
	$(CC) $(CFLAGS) -o test_packet.exe tests/test_packet.c $(SRC)/packet.c

test_timer: tests/test_timer.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_timer.exe tests/test_timer.c $(SRC)/timer.c $(LDFLAGS)

test_timer_manager: tests/test_timer_manager.c $(SRC)/timer.c
	$(CC) $(CFLAGS) -o test_timer_manager.exe tests/test_timer_manager.c $(SRC)/timer.c $(LDFLAGS)

sender: $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o sender.exe $(SRC)/udp_sender.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

receiver: $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c
	$(CC) $(CFLAGS) -o receiver.exe $(SRC)/udp_receiver.c $(SRC)/packet.c $(SRC)/timer.c $(SRC)/stop_wait.c $(LDFLAGS)

clean:
	-del /Q test_packet.exe test_timer.exe test_timer_manager.exe sender.exe receiver.exe 2>NUL

.PHONY: all clean