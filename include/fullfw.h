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

#define PART_CNT(file) (*(uint32_t *)file->buf.data)
#define PARTS_INFO_BASE(file) ((part_info_t *)(file->buf.data + sizeof(uint32_t)))
#define PARTS_DATA_BASE(file) (file->buf.data + 4 + PART_CNT(file) * sizeof(part_info_t))

part_info_t *fullfw_find_part(mmap_file_t *file, char *part);

void prn_part_info(part_info_t *ptr);

#endif
