CC = gcc
CPPFLAGS += -O3 -march=native

.PHONY: all clean

all: crc

clean:
	rm -f crc crc-gentbl
