BINS = wfs mkfs mnt disk.img
CC = gcc
TFLAGS = -std=gnu18 -g
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`
.PHONY: all
all: $(BINS)
wfs: wfs.c
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs:
	$(CC) $(CFLAGS) -o mkfs mkfs.c
test:
	$(CC) $(TFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
.PHONY: clean
clean:
	rm -rf $(BINS)
