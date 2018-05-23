#ifndef __FULLFW_H__
#define __FULLFW_H__

#include <stdint.h>
#include <stddef.h>

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

typedef struct {
	uint32_t part_cnt;
	part_info_t parts[];
} parts_hdr_t;


#define PARTS_DATA_BASE(hdr) ((char *)hdr + offsetof(parts_hdr_t, parts) + hdr->part_cnt * sizeof(part_info_t))

int fullfw_find_part(parts_hdr_t *hdr, char *part);

void prn_part_info(part_info_t *ptr);

#endif
