TARGET = pe_exchange
TRADER = pe_trader

CC = gcc
CFLAGS   = -Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS  = -lm
BINARIES = pe_exchange pe_trader

all: $(BINARIES)

run:
	./$(TARGET) $(ARGS)

.PHONY: clean
clean:
	rm -f $(BINARIES)

