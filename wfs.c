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

int disk_fd;
struct wfs_sb *sb;

static void walk_path(const char *path, struct stat *stbuf, struct wfs_inode* inode);

static int wfs_getattr(const char *path, struct stat *stbuf) {
    if(stbuf == NULL) {
        return 0;
    }
    printf("getattr called\n");
    printf("path: %s\n", path);

    struct wfs_inode inode;
    walk_path(path, stbuf, &inode);
    printf("inode num: %d\n", inode.num);
    printf("inode mode: %d\n", inode.mode);
    printf("Is directory: %d\n", S_ISDIR(inode.mode));

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


		
	return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("mknod called\n");
    return 0; // Return 0 on success
}

static int wfs_mkdir(const char *path, mode_t mode) {
    printf("mkdir called\n");
    return 0; // Return 0 on success
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
    return 0; // Return 0 on success
}


static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};


void getBlockData(off_t blockNum, char* blockData) {
    lseek(disk_fd, sb->d_blocks_ptr + blockNum * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, blockData, BLOCK_SIZE);
}

void get_inode(int inode_num, struct wfs_inode* inode) {
    // get inode from mmap
    lseek(disk_fd, sb->i_blocks_ptr + inode_num * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, inode, sizeof(struct wfs_inode));
}

void free_inode(struct wfs_inode* inode) {
    free(inode);
}

void update_inode(struct wfs_inode* inode) {
    // write inode to mmap
    lseek(disk_fd, sb->i_blocks_ptr + inode->num * BLOCK_SIZE, SEEK_SET);
    write(disk_fd, inode, BLOCK_SIZE);
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
    *length = l;
    return result;

}

static void walk_path(const char *path, struct stat *stbuf, struct wfs_inode* inode) {
    if(inode == NULL) {
        return;
    }
    printf("walk_path called\n");
    if((path[0]=='/' && path[1]=='\0') || path[0]=='\0') {
        get_inode(0, inode);
        return;
    }
    
    
    printf("Got root inode\n");
    char *path_copy = strdup(path);
    printf("Copied path\n");
    int path_len;
    char** path_parts = str_split(path_copy, '/', &path_len);
    printf("Length of path: %d\n", path_len);

    for(int i = 0; i < path_len; i++) {
        printf("%s\n", path_parts[i]);
    }
    if(stbuf == NULL) {
        goto cleanup;
    }

    get_inode(0, inode);
    for(int i = 0; i < path_len; i++) {
        if(S_ISREG(inode->mode) && i != path_len - 1) {
            printf("%s not a directory!\n", path_parts[i]);
            goto cleanup;
        }

        off_t *blocks = inode->blocks;
        char blockData[BLOCK_SIZE];
        for(int j = 0; j < N_BLOCKS; j++) {
            if(blocks[j] == 0) {
                continue;
            }
            getBlockData(blocks[j], blockData);
            struct wfs_dentry* dentry = (struct wfs_dentry*) blockData;
            for(int k = 0; k < BLOCK_SIZE; k += sizeof(struct wfs_dentry)) {
                if(strcmp(dentry[k].name, path_parts[i]) == 0) {
                    get_inode(dentry[k].num, inode);
                    goto found;
                }
            }
        }
        printf("Path not found\n");
        // TODO: something when not found
        goto cleanup;
        found:
        continue;
    }

    


    cleanup:
    free(path_copy);
    free(path_parts);
    return; // Return 0 on success
}


void initialize_superblock() {
    // setup superblock mmap
    void* sb_mmap = mmap(NULL, sizeof(struct wfs_sb), PROT_READ | PROT_WRITE, MAP_SHARED, disk_fd, 0);
    if(sb_mmap == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    sb = (struct wfs_sb*) sb_mmap;
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

    walk_path("/", NULL, NULL); // prevents warnings, TODO: REMOVE

    // open disk path
    char* disk_path = argv[1];
    printf("disk path: %s\n", disk_path);
    disk_fd = open(disk_path, O_RDWR, 0666);
    
    // remove disk path from argv
    for(int i = 1; i < argc - 1; i++) {
        argv[i] = argv[i + 1];
    }
    argc--;


    initialize_superblock();

    // start fuse
    return fuse_main(argc, argv, &ops, NULL);
}

