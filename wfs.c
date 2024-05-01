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

#define NUM_D_BLOCKS (D_BLOCK + 1)
#define ID_INDIRECT(D) (D - IND_BLOCK)
#define BIT_INDEX(num) (num % 32)

void set_inode_bitmap(int inode_num, bool value) {
    int index = inode_num / 32;
    int bit = BIT_INDEX(inode_num);
    if(value) {
        inode_bitmap[index] |= 1 << bit;
    } else {
        inode_bitmap[index] &= ~(1 << bit);
    }
}

bool get_inode_bitmap(int inode_num) {
    int index = inode_num / 32;
    int bit = BIT_INDEX(inode_num);
    return (inode_bitmap[index] >> bit) & 1;
}


void printInodeBitmap() {
    printf("inode bitmap:\n");
    for(int i = 0; i < sb->num_inodes; i++) {
        printf("%d", get_inode_bitmap(i));
    }
    printf("\n");
}

void set_data_bitmap(int data_num, bool value) {
    int index = data_num / 32;
    int bit = BIT_INDEX(data_num);
    if(value) {
        data_bitmap[index] |= 1 << bit;
    } else {
        data_bitmap[index] &= ~(1 << bit);
    }
}

bool get_data_bitmap(int data_num) {
    int index = data_num / 32;
    int bit = BIT_INDEX(data_num);
    return (data_bitmap[index] >> bit) & 1;
}

void printDataBitmap() {
    printf("data bitmap:\n");
    for(int i = 0; i < sb->num_data_blocks; i++) {
        printf("%d", get_data_bitmap(i));
    }
    printf("\n");
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
    write(disk_fd, inode, sizeof(struct wfs_inode));
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
    
    return 1;
}

void printDentriesInBlock(char* block) {
    struct wfs_dentry *dentries = (struct wfs_dentry*) block;
    for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        printf("dentry num: %d\n", dentries[i].num);
        printf("dentry name: %s\n", dentries[i].name);
    }
}

int add_dentry_to_block(char* block, struct wfs_dentry *dentry) {
    struct wfs_dentry *dentries = (struct wfs_dentry*) block;
    for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if(dentries[i].num == 0) {
            dentries[i] = *dentry;
            printf("Added dentry to block: %s\n", dentry->name);
            return i;
        }
    }
    printf("No space in block\n");
    return -1;
}


int add_inode(struct wfs_inode* parent_inode, const char* name, mode_t mode, struct wfs_inode* result_inode) {
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
    struct wfs_inode new_inode = {0};
    new_inode.num = free_inode;
    new_inode.mode = mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 1;
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
    char blockData[BLOCK_SIZE] = {0};
    off_t *block_offsets = parent_inode->blocks;
    for(int j = 0; j <= D_BLOCK; j++) {
        if(block_offsets[j] == 0) {
            int id = create_new_block();
            if(id == -1) {
                printf("Failed to create new block\n");
                return 1;
            }

            block_offsets[j] = get_data_ptr(id);
            memset(blockData, 0, BLOCK_SIZE);
        }
        else getBlockData(block_offsets[j], blockData);

        

        int index = add_dentry_to_block(blockData, &dentry);
        if(index != -1) {
            writeBlockData(block_offsets[j], blockData);
            // printDentriesInBlock(blockData);
            parent_inode->size += sizeof(struct wfs_dentry);
            update_inode(parent_inode);
            return 0;
        }
    } 

    // no space left, do not use indirect block for directories
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
    if(S_ISREG(current_inode->mode)) {
        printf("Not a directory\n");
        return 1;
    }

    struct wfs_dentry dentry;
    if(get_dentry(current_inode->blocks, name, &dentry) == 1) {
        printf("Could not find %s\n", name);
        return 1;
    }

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
        return 0;
    }
    

    for(int i = 0; i < length; i++) {
        if(step_into(path[i], inode, inode) == 1) return 1;
    }

    return 0; // Return 0 on success
}

int create_inode(char** path, int path_len, int mode, struct wfs_inode* result_inode) {
    struct wfs_inode inode;
    if(walk_path(path, path_len - 1, &inode) == 1) return -ENOENT;
    
    char* name = path[path_len-1];

    struct wfs_dentry dentry;
    if(get_dentry(inode.blocks, name, &dentry) == 0) {
        printf("Inode already exists\n");
        return -EEXIST;
    }

    struct wfs_inode new_inode;
    if(add_inode(&inode, name, mode, &new_inode) == 1) {
        printf("Failed to add inode\n");
        return -1; // TODO: fix error code number; Error likely due to no space in the inode
    }
    *result_inode = new_inode;
    return 0;
}


static int wfs_getattr(const char *path, struct stat *stbuf) {
    // printf("getattr called on %s\n", path);

    int result = 0; // Return 0 on success
    
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);
    
    
    if(walk_path(path_split, path_len, &inode) == 1) {
        result = -ENOENT;
        // printf("Failed to walk path\n");
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
    
    int n_blocks = 0;
    for(int i = 0; i < NUM_D_BLOCKS; i++) {
        if(inode.blocks[i] == 0) break;
        n_blocks++;
    }
    if(inode.blocks[IND_BLOCK] != 0) {
        n_blocks++;

        off_t ind_block_ptrs[BLOCK_SIZE / sizeof(off_t)];
        getBlockData(inode.blocks[IND_BLOCK], (char*) ind_block_ptrs);
        for(int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
            if(ind_block_ptrs[i] == 0) break;
            n_blocks++;
        }
    }
    stbuf->st_blocks = n_blocks;

    cleanup:
    free(dupPath);
    free(path_split);


	return result;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("mknod called\n");
   
    mode |= __S_IFREG;
    int result = 0;
    
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);

    if(create_inode(path_split, path_len, mode, &inode) != 0) {
        result = -1; // TODO: fix error code number;
        printf("Failed to create directory\n");
        goto cleanup;
    }

    cleanup:
    free(dupPath);
    free(path_split);


	return result;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    printf("mkdir called\n");

    mode |= __S_IFDIR;
    int result = 0; // Return 0 on success
    
    struct wfs_inode inode;
    int path_len;
    char* dupPath = strdup(path);
    char** path_split = str_split(strdup(dupPath), '/', &path_len);

    if(create_inode(path_split, path_len, mode, &inode) != 0) {
        result = -1; // TODO: fix error code number;
        printf("Failed to create directory\n");
        goto cleanup;
    }

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

int readDataFromBlock(char* buffer, size_t bufferSize, off_t offset, off_t directBlocks[N_BLOCKS], off_t* indBlockPtrs) {
    int remainingInBlock = BLOCK_SIZE - (offset % BLOCK_SIZE);
    int toRead = remainingInBlock;
    if(toRead > bufferSize) {
        toRead = bufferSize;
    }

    int blockNumber = offset / BLOCK_SIZE;
    int blockOffset = offset % BLOCK_SIZE;
    
    // get the pointer of the block to read from
    off_t blockPtr;
    if(blockNumber <= D_BLOCK) {
        blockPtr = directBlocks[blockNumber];
    } else {
        blockPtr = indBlockPtrs[ID_INDIRECT(blockNumber)];
    }
    if(blockPtr == 0) {
        return -1;
    }

    lseek(disk_fd, blockPtr + blockOffset, SEEK_SET);
    return read(disk_fd, buffer, toRead);
}

int read_from_file(char* buffer, size_t size, off_t offset, struct wfs_inode* inode) {
    printf("A\n");
    if(offset < 0) {
        return 0;
    }

    char ind_block_ptrs[BLOCK_SIZE] = {0};
    // check if we will need to use indirect blocks
    if(offset + size > (NUM_D_BLOCKS) * BLOCK_SIZE) { // 7 direct blocks * block_size is the max address for direct blocks
        printf("Using indirect block\n");
        if(inode->blocks[IND_BLOCK] == 0) {
            return -1;
        }else {
            getBlockData(inode->blocks[IND_BLOCK], ind_block_ptrs);
        }
    }
    printf("B\n");

    int totalRead = 0;
    int read = 0;
    while(totalRead < size) {
        printf("Reading from block\n");
        read = readDataFromBlock(buffer, size, totalRead + offset, inode->blocks, (off_t *)ind_block_ptrs);
        if(read == -1 || read == 0) {
            return totalRead;
        }
        totalRead += read;
    }

    printf("C\n");

    return totalRead;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read called\n");

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

    result = read_from_file(buf, size, offset, &inode);

    cleanup:
    free(dupPath);
    free(path_split);

    return result; // Return 0 on success
}


// buffer: the buffer to write to the blocks
// bufferSize: the size of the buffer
// offset: the offset into the blocks in bytes to start writing
// directBlocks: pointers to the direct blocks, size of NUM_BLOCKS (last block is indirect block)
// indirectBlock: pointer to the indirect block (last block in directBlocks array). Max size is BLOCK_SIZE / sizeof(off_t)
int writeDataToBlock(const char* buffer, size_t bufferSize, off_t offset, off_t directBlocks[N_BLOCKS], off_t* indBlockPtrs, struct wfs_inode* inode) {
    int remainingInBlock = BLOCK_SIZE - (offset % BLOCK_SIZE);
    int toWrite = remainingInBlock;
    if(toWrite > bufferSize) {
        toWrite = bufferSize;
    }

    int blockNumber = offset / BLOCK_SIZE;
    int blockOffset = offset % BLOCK_SIZE;
    
    // get the pointer of the block to write to
    off_t blockPtr;
    if(blockNumber <= D_BLOCK) {
        blockPtr = directBlocks[blockNumber];
    } else {
        blockPtr = indBlockPtrs[ID_INDIRECT(blockNumber)];
    }

    if(blockPtr == 0) {
        int id = create_new_block();
        if(id == -1) {
            printf("Failed to create block");
            return -1;
        }
        blockPtr = get_data_ptr(id);
        if(blockNumber <= D_BLOCK) {
            directBlocks[blockNumber] = blockPtr;
            update_inode(inode);
        }
        else {
            indBlockPtrs[ID_INDIRECT(blockNumber)] = blockPtr;

        }
    }

    lseek(disk_fd, blockPtr + blockOffset, SEEK_SET);
    return write(disk_fd, buffer, toWrite);
}


int write_to_file(const char* buffer,  off_t offset, size_t size, struct wfs_inode* inode) {
    if(offset < 0) {
        return 0;
    }

    char ind_block_ptrs[BLOCK_SIZE] = {0};
    // check if we will need to use indirect blocks
    if(offset + size > (NUM_D_BLOCKS) * BLOCK_SIZE) { // 7 direct blocks * block_size is the max address for direct blocks
        printf("Using indirect block\n");
        if(inode->blocks[IND_BLOCK] == 0) {
            printf("Creating new indirect block\n");
            int id = create_new_block();
            if(id == -1) {
                return -1;
            }
            inode->blocks[IND_BLOCK] = get_data_ptr(id);
        }else {
            getBlockData(inode->blocks[IND_BLOCK], ind_block_ptrs);
        }
    }

    int totalWritten = 0;
    int written = 0;
    while(totalWritten < size) {
        written = writeDataToBlock(buffer, size, totalWritten + offset, inode->blocks, (off_t *)ind_block_ptrs, inode);
        if(written == -1) {
            printf("Problem Writing to block");
            inode->size = offset + size;
            inode->mtim = time(NULL);
            update_inode(inode);
            return totalWritten;
        }
        totalWritten += written;
    }

    // update inode
    inode->size = offset + size;
    inode->mtim = time(NULL);
    update_inode(inode);

    return totalWritten;

}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called\n");

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

    result = write_to_file(buf, offset, size, &inode);

    cleanup:
    free(dupPath);
    free(path_split);

    return result; // Return 0 on success
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called, path: %s\n", path);

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
        memset(blockData, 0, BLOCK_SIZE);
        printf("block offset: %ld\n", inode.blocks[j]);
        if(getBlockData(inode.blocks[j], blockData) == 1) {
            goto cleanup;
        }
        struct wfs_dentry *dentries = (struct wfs_dentry*) blockData;
        for(int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
            if(dentries[i].num != 0) {
                printf("dentry name: %s\n", dentries[i].name);
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
    inode_bitmap = (int*) (mmaps + sb->i_bitmap_ptr);
    data_bitmap = (int*) (mmaps + sb->d_bitmap_ptr);
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

