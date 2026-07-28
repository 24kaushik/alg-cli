CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lm

.PHONY: all clean test

all: alg-rgb

alg-rgb: alg-rgb.c ../include/version.h
	$(CC) $(CPPFLAGS) $(CFLAGS) alg-rgb.c -o $@ $(LDLIBS)

clean:
	rm -f alg-rgb

test:
	../tests/test-cli.sh
