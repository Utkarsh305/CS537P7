#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "wfs.h" 
#include <sys/mman.h>



size_t round_up(size_t num, size_t multiple) {
    return num % multiple == 0 ? num : num + (multiple - (num % multiple));
}

/**
 * Main function to create filesystem.
 */
int main(int argc, char *argv[]) {
    if (argc != 7) {
        fprintf(stderr, "Usage: %s -d disk_img -i num_inodes -b num_blocks\n", argv[0]);
        return 1;
    }

    char *disk_img = NULL;
    size_t num_inodes = 0;
    size_t num_blocks = 0;

    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "-d") == 0) {
            disk_img = argv[i + 1];
        } else if (strcmp(argv[i], "-i") == 0) {
            num_inodes = round_up(atoi(argv[i + 1]), 32);
        } else if (strcmp(argv[i], "-b") == 0) {
            num_blocks = round_up(atoi(argv[i + 1]), 32);
        }
    }

    if (!disk_img || num_inodes == 0 || num_blocks == 0) {
        fprintf(stderr, "Invalid parameters\n");
        return 1;
    }

    int fd = open(disk_img, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        perror("Failed to open disk image");
        return 1;
    }

    
    int i_bitmap_size = num_inodes / 8;
    int d_bitmap_size = num_blocks / 8;

    //pointers based on block sizes and number of blocks
    off_t i_bitmap_ptr = sizeof(struct wfs_sb);
    off_t d_bitmap_ptr = i_bitmap_ptr + i_bitmap_size; // 1 bit per inode, rounded up to nearest byte
    off_t i_blocks_ptr = d_bitmap_ptr + d_bitmap_size; // 1 bit per block
    off_t d_blocks_ptr = i_blocks_ptr + num_inodes * BLOCK_SIZE;

    // superblock
    struct wfs_sb sb = {
        .num_inodes = num_inodes,
        .num_data_blocks = num_blocks,
        .i_bitmap_ptr = i_bitmap_ptr,
        .d_bitmap_ptr = d_bitmap_ptr,
        .i_blocks_ptr = i_blocks_ptr,
        .d_blocks_ptr = d_blocks_ptr
    };

    char* memStart = mmap(NULL, sizeof(struct wfs_sb) + d_blocks_ptr, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(memStart, &sb, sizeof(sb));

    // TODO: check if the max amount of memory that can be used is available on the disk image
    struct stat st;
    fstat(fd, &st);
    size_t disk_size = st.st_size;
    size_t required_size = d_blocks_ptr + num_blocks * BLOCK_SIZE;
    if (disk_size < required_size) {
        perror("Disk image is too small");
        close(fd);
        return 1;
    }

    // check here

    // Todo: set root inode
    struct wfs_inode root_inode = {
        .num = 0,
        .mode = __S_IFDIR | 0755,
        .uid = getuid(),
        .gid = getgid(),
        .size = 0,
        .nlinks = 1,
        .atim = time(NULL),
        .mtim = time(NULL),
        .ctim = time(NULL),
        .blocks = {0}
    };

    // set inode bitmap first value to 1
    *(memStart + sb.i_bitmap_ptr) |= 1;
    /*
    lseek(fd, i_bitmap_ptr, SEEK_SET);
    write(fd, "\x1", 1);
    */

    // write root inode
    memcpy(memStart + sb.i_blocks_ptr, &root_inode, sizeof(root_inode));
    /*
    lseek(fd, i_blocks_ptr, SEEK_SET);
    if (write(fd, &root_inode, sizeof(root_inode)) != sizeof(root_inode)) {
        perror("Failed to write root inode");
        close(fd);
        return 1;
    }
    */    

    close(fd);
    return 0;
}
