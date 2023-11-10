
CC=gcc
CFLAGS= -O0 -g
LDFLAGS=-lqatzip -Wl,-rpath,/usr/local/lib

.PHONY: default
default: all

all: compress_lz4 compress_gzip decompress_gzip decompress_lz4

compress_lz4: compress_lz4.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

compress_gzip: compress_gzip.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

decompress_gzip: decompress_gzip.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

decompress_lz4: decompress_lz4.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm compress_lz4 compress_gzip decompress_gzip decompress_lz4