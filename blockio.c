#include <string.h>
#include <fcntl.h>
#include "io_helper.h"
#include "blockio.h"


#define BLOCK_SIZE 256


// will allocate a new block at the end of the file
int alloc_block(const char *filename) {
    int fd = open_or_die(filename, O_RDWR | O_CREAT, 0666); // open file for RW
    // find end of file
    off_t off = lseek_or_die(fd, 0, SEEK_END);
    int blocknum = off / BLOCK_SIZE;
    // write a zero block
    char buf[BLOCK_SIZE] = {0}; // set the block to zero bytes
    write_or_die(fd, buf, BLOCK_SIZE);
    close_or_die(fd);
    return blocknum; //return block index(0 based)
}

// reads block into buffer(must be a least block_size)
void read_block(const char *filename, int blocknum, char buf[BLOCK_SIZE]) {
    int fd = open_or_die(filename, O_RDONLY, 0); 
    lseek_or_die(fd, (off_t)blocknum * BLOCK_SIZE, SEEK_SET); // seek byte offset to find appropriate block for reading
    read_or_die(fd, buf, BLOCK_SIZE);  // read 256 bytes into buffer
    close_or_die(fd);
}

// write buffer into block
void write_block(const char *filename, int blocknum, const char buf[BLOCK_SIZE]) {
    int fd = open_or_die(filename, O_RDWR, 0);
    lseek_or_die(fd, (off_t)blocknum * BLOCK_SIZE, SEEK_SET); //same as read
    write_or_die(fd, buf, BLOCK_SIZE); //same as read
    close_or_die(fd);
}

// free a block by zeroing it out
void free_block(const char *filename, int blocknum) {
    int fd = open_or_die(filename, O_RDWR, 0);
    lseek_or_die(fd, (off_t)blocknum * BLOCK_SIZE, SEEK_SET); 
    char buf[BLOCK_SIZE] = {0}; // set the block to zero bytes
    write_or_die(fd, buf, BLOCK_SIZE);
    close_or_die(fd);
}


#include <stdint.h>

// write the next‐block index into the last 4 bytes of block
void set_next_block(const char *file, int blocknum, int32_t next) {
    int fd = open_or_die(file, O_RDWR, 0);
    // seek to byte offset blocknum*256 + 252
    lseek_or_die(fd, (off_t)blocknum * BLOCK_SIZE + BLOCK_SIZE - sizeof(int32_t), SEEK_SET);
    write_or_die(fd, &next, sizeof(next));
    close_or_die(fd);
}

// read the next‐block index from the last 4 bytes of block
int32_t get_next_block(const char *file, int blocknum) {
    int32_t next;
    int fd = open_or_die(file, O_RDONLY, 0);
    lseek_or_die(fd, (off_t)blocknum * BLOCK_SIZE + BLOCK_SIZE - sizeof(next), SEEK_SET);
    read_or_die(fd, &next, sizeof(next));
    close_or_die(fd);
    return next;
}
