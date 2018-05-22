#ifndef __FULLFW_H__
#define __FULLFW_H__

#include <stdint.h>

#include "file_mmap.h"

typedef struct {
	uint32_t offset;
	uint32_t size;
	uint32_t unknown1;
	uint32_t loadaddr;
	char name[128];
	char part[128];
	char path[1024];
	uint32_t unknown3;
	uint32_t unknown4;
} part_info_t;

#define part_foreach(ptr, file) \
	for (ptr = (part_info_t *)(file->buf.data + sizeof(uint32_t)); \
		ptr < (part_info_t *)(file->buf.data + sizeof(uint32_t) + *(uint32_t *)(file->buf.data) * sizeof(part_info_t)); \
		ptr++)

part_info_t *fullfw_find_part(mmap_file_t *file, char *part);
char *get_part_ptr(mmap_file_t *file, part_info_t *part_info);

#endif
