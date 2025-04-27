#ifndef BLOCKIO_H
#define BLOCKIO_H

#include <stdint.h>
#include <stddef.h>

#define BLOCK_SIZE 256

int alloc_block(const char *filename);
void read_block(const char *filename, int blocknum, char buf[BLOCK_SIZE]);
void write_block(const char *filename, int blocknum, const char buf[BLOCK_SIZE]);
void free_block(const char *filename, int blocknum);
void set_next_block(const char *file, int blocknum, int32_t next);
int32_t get_next_block(const char *file, int blocknum);

#endif // BLOCKIO_H