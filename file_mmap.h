#ifndef __FILE_MMAP_H__
#define __FILE_MMAP_H__

#include "defs.h"

typedef struct {
	int fd;
	buf_t buf;
} mmap_file_t;

mmap_file_t *load_file(char *path);
void close_file(mmap_file_t *file);

#endif
