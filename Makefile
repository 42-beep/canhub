CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -O0

all: can_receiver canreplay test_canlog

can_receiver: src/can_receiver.c src/canlog.c src/canlog.h
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

canreplay: src/canreplay.c src/canlog.c src/canlog.h
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_canlog: src/test_canlog.c src/canlog.c src/canlog.h
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test: test_canlog
	@./test_canlog

clean:
	rm -f can_receiver canreplay test_canlog test_layout *.log

.PHONY: all test clean
