#include <stdio.h>
#include <string.h>

#include "fullfw.h"

void prn_part_info(part_info_t *ptr)
{
	printf("offset: 0x%08x\n", ptr->offset);
	printf("size: 0x%08x\n", ptr->size);
	printf("unknown1: 0x%08x\n", ptr->unknown1);
	printf("loadaddr: 0x%08x\n", ptr->loadaddr);
	printf("name: %s\n", ptr->name);
	printf("part: %s\n", ptr->part);
	printf("path: %s\n", ptr->path);
	printf("unknown3: 0x%08x\n", ptr->unknown3);
	printf("unknown4: 0x%08x\n", ptr->unknown4);
}

part_info_t *fullfw_find_part(mmap_file_t *file, char *part)
{
	if (!file)
		return NULL;

	part_info_t *ptr = NULL;
	part_foreach(ptr, file) {
		if (strcmp(part, ptr->part) == 0)
			return ptr;
	}
	return NULL;
}

char *get_part_ptr(mmap_file_t *file, part_info_t *part_info)
{
	if (!file || !part_info)
		return NULL;
	uint32_t parts_cnt = *(uint32_t *)(file->buf.data);
	return file->buf.data + sizeof(uint32_t) + parts_cnt * sizeof(part_info_t) + part_info->offset;
}

