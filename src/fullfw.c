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

int fullfw_find_part(parts_hdr_t *hdr, char *part)
{
	int i;

	for (i = 0; i < hdr->part_cnt; i++) {
		if (strcmp(part, hdr->parts[i].part) == 0)
			return i;
	}
	return -1;
}
