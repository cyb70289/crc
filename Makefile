CC = gcc
CPPFLAGS += -O3 -march=native

.PHONY: all clean

all: crc crc-opt

crc-opt: crc.c
	$(CC) $(CPPFLAGS) -DCRC32_OPT crc.c -o $@

clean:
	rm -f crc crc-opt crc-gentbl crc-poly
