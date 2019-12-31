CC = gcc
CPPFLAGS += -O3 -march=armv8-a+crc+crypto

.PHONY: all clean

all: crc crc-opt crc-fold pmull-crc-poc

crc-opt: crc.c
	$(CC) $(CPPFLAGS) -DCRC32_OPT crc.c -o $@

crc-fold: crc.c
	$(CC) $(CPPFLAGS) -DCRC32_FOLD crc.c -o $@

clean:
	rm -f crc crc-opt crc-fold crc-gentbl crc-poly pmull-crc-poc
