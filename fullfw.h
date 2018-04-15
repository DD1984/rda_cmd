#ifndef __FULLFW_H__
#define __FULLFW_H__

#include <stdint.h>

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

#endif
