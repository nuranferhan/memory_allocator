##############################################################################
# Makefile - Custom Memory Allocator
#
# Hedefler:
#   make all      -> test, benchmark ve TUI ikili dosyalarını derler
#   make test     -> test programını çalıştırır
#   make bench    -> benchmark programını çalıştırır
#   make tui      -> TUI programını derler ve açar
#   make clean    -> oluşturulan dosyaları siler
#   make valgrind -> test programını valgrind altında çalıştırır
##############################################################################

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 \
          -O2 -g \
          -I. \
          -D_POSIX_C_SOURCE=200809L \
          -D_GNU_SOURCE
LDFLAGS = -lpthread

SRC_COMMON = allocator.c log.c
SRC_TEST   = test.c
SRC_BENCH  = benchmark.c
SRC_TUI    = tui.c

OBJ_COMMON = $(SRC_COMMON:.c=.o)
OBJ_TEST   = $(SRC_TEST:.c=.o)
OBJ_BENCH  = $(SRC_BENCH:.c=.o)
OBJ_TUI    = $(SRC_TUI:.c=.o)

BIN_TEST   = allocator_test
BIN_BENCH  = allocator_bench
BIN_TUI    = allocator_tui

.PHONY: all clean test bench tui valgrind

all: $(BIN_TEST) $(BIN_BENCH) $(BIN_TUI)

$(BIN_TEST): $(OBJ_COMMON) $(OBJ_TEST)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_BENCH): $(OBJ_COMMON) $(OBJ_BENCH)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_TUI): $(OBJ_COMMON) $(OBJ_TUI)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c allocator.h log.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(BIN_TEST)
	./$(BIN_TEST)

bench: $(BIN_BENCH)
	./$(BIN_BENCH)

tui: $(BIN_TUI)
	./$(BIN_TUI)

valgrind: $(BIN_TEST)
	valgrind --leak-check=full \
	         --show-leak-kinds=all \
	         --track-origins=yes \
	         --verbose \
	         ./$(BIN_TEST) 2>&1 | tee valgrind_report.txt

clean:
	rm -f $(OBJ_COMMON) $(OBJ_TEST) $(OBJ_BENCH) $(OBJ_TUI)
	rm -f $(BIN_TEST) $(BIN_BENCH) $(BIN_TUI)
	rm -f *.log valgrind_report.txt
