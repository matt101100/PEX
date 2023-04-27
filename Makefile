CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS=-lm
BINARIES=pe_exchange

all: $(BINARIES)

.PHONY: clean
clean:
	rm -f $(BINARIES)

