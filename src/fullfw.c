#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

part_info_t *fullfw_find_part(parts_hdr_t *hdr, char *part)
{
	part_info_t *part_info;

	part_foreach(hdr, part_info) {
		if (strcmp(part, part_info->part) == 0)
			return part_info;
	}
	return NULL;
}

#define MAX_PART_NUM 15

int check_img(parts_hdr_t *hdr, int max_size)
{
	int i, j;
	part_info_t *part_info;

	if (hdr->part_cnt > MAX_PART_NUM) {
		printf("invalid image: partition count - %d, but must be less than %d\n", hdr->part_cnt, MAX_PART_NUM);
		return -1;
	}

	i = 0;

	part_foreach(hdr, part_info) {

		j = 0;
		while ((j < sizeof(part_info->part) && (part_info->part[j] != 0))) {

			if (!isprint(part_info->part[j]))
				i = sizeof(part_info->part);
			else
				j++;
		}

		if ((*(part_info->part) == 0) || j >= sizeof(part_info->part)) {
			printf("invalid image: name of partition %d - unprintable\n", i);
			return -1;
		}

		if (part_info->offset + part_info->size > max_size) {
			printf("invalid image: partition %d - outside the file\n", i);
			return -1;
		}

		i++;
	}

	return 0;
}
