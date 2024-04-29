#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdbool.h>

int disk_fd;
struct wfs_sb *sb;
int *inode_bitmap;
int *data_bitmap;

void set_inode_bitmap(int inode_num, bool value) {
    int index = inode_num / 32;
    int bit = inode_num % 32;
    if(value) {
        inode_bitmap[index] |= 1 << bit;
    } else {
        inode_bitmap[index] &= ~(1 << bit);
    }
}

bool get_inode_bitmap(int inode_num) {
    int index = inode_num / 32;
    int bit = inode_num % 32;
    return inode_bitmap[index] & (1 << bit);
}

void set_data_bitmap(int data_num, bool value) {
    int index = data_num / 32;
    int bit = data_num % 32;
    if(value) {
        data_bitmap[index] |= 1 << bit;
    } else {
        data_bitmap[index] &= ~(1 << bit);
    }
}

bool get_data_bitmap(int data_num) {
    int index = data_num / 32;
    int bit = data_num % 32;
    return data_bitmap[index] & (1 << bit);
}

off_t get_inode_ptr(int inode_num) {
    return sb->i_blocks_ptr + inode_num * BLOCK_SIZE;
}

off_t get_data_ptr(int data_num) {
    return sb->d_blocks_ptr + data_num * BLOCK_SIZE;
}

int create_new_block() {
    for(int i = 0; i < sb->num_data_blocks; i++) {
        if(get_data_bitmap(i) == 0) {
            set_data_bitmap(i, true);
            return i;
        }
    }
    return -1;
}

int getBlockData(off_t offset, char* blockData) {
    if(offset < sb->d_blocks_ptr) return 1;
    lseek(disk_fd, offset, SEEK_SET);
    read(disk_fd, blockData, BLOCK_SIZE);
    return 0;
}

int writeBlockData(off_t offset, char* blockData) {
    if(offset < sb->d_blocks_ptr) return 1;
    lseek(disk_fd, offset, SEEK_SET);
    write(disk_fd, blockData, BLOCK_SIZE);
    return 0;
}

void get_inode(int inode_num, struct wfs_inode* inode) {
    lseek(disk_fd, get_inode_ptr(inode_num), SEEK_SET);
    read(disk_fd, inode, sizeof(struct wfs_inode));
}

void update_inode(struct wfs_inode* inode) {
    lseek(disk_fd, get_inode_ptr(inode->num), SEEK_SET);
    write(disk_fd, inode, BLOCK_SIZE);
}

// 1 = fail, 0 = success
int get_dentry_from_block(char* blockData, char *name, struct wfs_dentry *result_dentry) {
    struct wfs_dentry *dentries = (struct wfs_dentry*) blockData;
    for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if(strcmp(dentries[i].name, name) == 0) {
            *result_dentry = dentries[i];
            return 0;
        }
    }
    return 1;
}

// 1 = fail, 0 = success
int get_dentry(off_t *block_offsets, char *name, struct wfs_dentry *result_dentry) {
    char blockData[BLOCK_SIZE];
    
    // Search direct blocks
    for(int j = 0; j <= D_BLOCK; j++) {
        if(getBlockData(block_offsets[j], blockData) == 1) {
            return 1;
        }
        if(get_dentry_from_block(blockData, name, result_dentry) == 0) {
            return 0;
        }
    }
    
    // recursively check indirect blocks
    char indirectBlock[BLOCK_SIZE];
    if(getBlockData(block_offsets[IND_BLOCK - 1], indirectBlock) == 1) {
        return 1;
    }

    off_t *offsets = (off_t *)indirectBlock;
    for(int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
        if(getBlockData(offsets[i], blockData) == 1) {
            return 1;
        }
        if(get_dentry_from_block(blockData, name, result_dentry) == 0) {
            return 0;
        }
        
    }
    return 1;
}

int add_dentry_to_block(char* block, struct wfs_dentry *dentry) {
    struct wfs_dentry *dentries = (struct wfs_dentry*) block;
    for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if(dentries[i].num == 0) {
            dentries[i] = *dentry;
            return 0;
        }
    }
    return 1;
}


int add_directory(struct wfs_inode* inode, const char* name, mode_t mode, struct wfs_inode* result_inode) {
    // search for a free inode
    int free_inode = -1;
    for(int i = 0; i < sb->num_inodes; i++) {
        if(get_inode_bitmap(i) == 0) {
            free_inode = i;
            break;
        }
    }

    if(free_inode == -1) {
        return 1;
    }

    // set inode bitmap
    set_inode_bitmap(free_inode, true);

    // set inode
    struct wfs_inode new_inode;
    new_inode.num = free_inode;
    new_inode.mode = mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.atim = time(NULL);
    new_inode.mtim = time(NULL);
    new_inode.ctim = time(NULL);
    new_inode.blocks[0] = 0; // no data in the inode yet
    update_inode(&new_inode);
    *result_inode = new_inode;


    // update parent inode

    struct wfs_dentry dentry;
    strcpy(dentry.name, name);
    dentry.num = free_inode;
    // check direct blocks
    char blockData[BLOCK_SIZE];
    off_t *block_offsets = inode->blocks;
    for(int j = 0; j <= D_BLOCK; j++) {
        if(getBlockData(block_offsets[j], blockData) == 1) {
            int id = create_new_block();
            if(id == -1) {
                return 1;
            }
            block_offsets[j] = get_data_ptr(id);
        }
        if(add_dentry_to_block(blockData, &dentry) == 0) {
            writeBlockData(block_offsets[j], blockData);
            return 0;
        }
    }

    // check indirect blocks
    char indirectBlock[BLOCK_SIZE];
    if(getBlockData(block_offsets[IND_BLOCK - 1], indirectBlock) == 1) {
        return 1;
    }

    off_t *offsets = (off_t *)indirectBlock;
    for(int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
        if(getBlockData(offsets[i], blockData) == 1) {
            int id = create_new_block();
            if(id == -1) {
                return 1;
            }
            offsets[i] = get_data_ptr(id);
        }
        if(add_dentry_to_block(blockData, &dentry) == 0) {
            writeBlockData(offsets[i], blockData);
            return 0;
        }
    }

    // no space in the indirect blocks
    return 1;
}


char** str_split(char* str, const char delim, int* length) {
    int index = 0;
    int l = 0;
    while(str[index] != '\0') {
        if(str[index] == delim) {
            l++;
        }
        index++;
    }

    char** result = malloc(sizeof(char*) * l);
    int i = 0;
    char* token = strtok(str, &delim);
    while(token != NULL) {
        result[i] = token;
        token = strtok(NULL, &delim);
        i++;
    }

    if(str[0] != delim) {
        l++;
    }
    *length = i;
    return result;
}

// 1 = fail, 0 = success
int step_into(char* name, struct wfs_inode* current_inode, struct wfs_inode* result_inode) {
    if(S_ISREG(current_inode->mode)) return 1;

    struct wfs_dentry dentry;
    if(get_dentry(current_inode->blocks, name, &dentry) == 1) return 1;

    get_inode(dentry.num, result_inode);
    return 0;
}

// walks as far as possible in the path. 1 = fail, 0 = success
int walk_path(char **path, int length, struct wfs_inode* inode) {
    if(inode == NULL) {
        return 1;
    }
    get_inode(0, inode);
    if(length == 0) {
        printf("Walked to root\n");
        return 0;
    }
    

    for(int i = 0; i < length; i++) {
        if(step_into(path[i], inode, inode) == 1) return 1;
    }

    return 0; // Return 0 on success
}


static int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("getattr called on %s\n", path);

    int result = 0; // Return 0 on success
    
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);
    
    if(walk_path(path_split, path_len, &inode) == 1) {
        result = -ENOENT;
        printf("Failed to walk path\n");
        goto cleanup;
    }

    stbuf->st_dev = 0;
    stbuf->st_ino = inode.num;
    stbuf->st_mode = inode.mode;
    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
    stbuf->st_size = inode.size;
    stbuf->st_blksize = BLOCK_SIZE;
    stbuf->st_blocks = inode.size / BLOCK_SIZE;
    stbuf->st_atime = inode.atim;
    stbuf->st_mtime = inode.mtim;
    stbuf->st_ctime = inode.ctim;

    cleanup:
    free(dupPath);
    free(path_split);


	return result;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("mknod called\n");
    return 0; // Return 0 on success
}

static int wfs_mkdir(const char *path, mode_t mode) {
    printf("mkdir called\n");

    int result = 0; // Return 0 on success
    
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);

    for(int i = 0; i < path_len; i++) {
        printf("path_split[%d]: %s\n", i, path_split[i]);
    }
    
    if(walk_path(path_split, path_len - 1, &inode) == 1) {
        result = -ENOENT;
        printf("Failed to walk path\n");
        goto cleanup;
    }
    char* name = path_split[path_len-1];

    struct wfs_dentry dentry;
    if(get_dentry(inode.blocks, name, &dentry) == 0) {
        result = -EEXIST;
        printf("Directory already exists\n");
        goto cleanup;
    }

    struct wfs_inode new_inode;
    if(add_directory(&inode, path, mode, &new_inode) == 1) {
        result = -1; // TODO: fix error code number;
        printf("Failed to add directory\n");
        goto cleanup;
    }

    printf("Added directory\n");
    printf("inode num: %d\n", new_inode.num);
    printf("inode mode: %d\n", new_inode.mode);
    printf("inode uid: %d\n", new_inode.uid);
    printf("inode gid: %d\n", new_inode.gid);
    printf("inode size: %ld\n", new_inode.size);
    printf("inode atim: %ld\n", new_inode.atim);
    printf("inode mtim: %ld\n", new_inode.mtim);
    printf("inode ctim: %ld\n", new_inode.ctim);
    printf("inode blocks: %ld\n", new_inode.blocks[0]);

    cleanup:
    free(dupPath);
    free(path_split);


	return result;
}

static int wfs_unlink(const char *path) {
    printf("unlink called\n");
    return 0; // Return 0 on success
}

static int wfs_rmdir(const char *path) {
    printf("rmdir called\n");
    return 0; // Return 0 on success
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read called\n");
    return 0; // Return 0 on success
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called\n");
    return 0; // Return 0 on success
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called\n");

    int result = 0;
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);
    
    if(walk_path(path_split, path_len, &inode) == 1) {
        result = -ENOENT;
        printf("Failed to walk path\n");
        goto cleanup;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);


    char blockData[BLOCK_SIZE];
    for(int j = 0; j <= D_BLOCK; j++) {
        if(getBlockData(inode.blocks[j], blockData) == 1) {
            goto cleanup;
        }
        struct wfs_dentry *dentries = (struct wfs_dentry*) blockData;
        for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
            if(dentries[i].num != 0) {
                filler(buf, dentries[i].name, NULL, 0);
            }
        }
    }
    

    cleanup:
    free(dupPath);
    free(path_split);
    return result; // Return 0 on success
}


static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir
};


void initialize_superblock_bitmaps() {
    // read the superblock
    lseek(disk_fd, 0, SEEK_SET);
    sb = malloc(sizeof(struct wfs_sb));
    read(disk_fd, sb, sizeof(struct wfs_sb));

    // mmap the bitmaps
    size_t inode_bitmap_size = sb->num_inodes / 8;
    size_t data_bitmap_size = sb->num_data_blocks / 8;

    char* mmaps = mmap(NULL, inode_bitmap_size + data_bitmap_size + sizeof(struct wfs_sb), PROT_READ | PROT_WRITE, MAP_SHARED, disk_fd, 0);
    if(mmaps == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    sb = (struct wfs_sb*) mmaps;
    inode_bitmap = (int*) (mmaps + sizeof(struct wfs_sb));
    data_bitmap = (int*) (mmaps + sizeof(struct wfs_sb) + inode_bitmap_size);

    // print inode bitmap
    for(int i = 0; i < sb->num_inodes; i++) {
        printf("%d", get_inode_bitmap(i));
    }
}

// example:
/*
./create_disk.sh
./mkfs -d disk.img -i 32 -b 200
mkdir mnt
./wfs disk.img -f -s mnt
*/
int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main

    // open disk path
    char* disk_path = argv[1];
    printf("disk path: %s\n", disk_path);
    disk_fd = open(disk_path, O_RDWR, 0666);
    
    // remove disk path from argv
    for(int i = 1; i < argc - 1; i++) {
        argv[i] = argv[i + 1];
    }
    argc--;


    initialize_superblock_bitmaps();

    // start fuse
    return fuse_main(argc, argv, &ops, NULL);
}

