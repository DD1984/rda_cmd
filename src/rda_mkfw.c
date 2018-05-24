#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fullfw.h"
#include "file_mmap.h"
#include "pdls.h"
#include "prog_dir.h"

#define FULLFW_NAME "fullfw.img"

int write_cont(int fd, char *ptr, int size)
{
	int total_cnt = 0;
	while (total_cnt < size) {
		int write_cnt = write(fd, ptr + total_cnt, size - total_cnt);
		if (write_cnt < 0) {
			printf("%s failed\n", __func__);
			return -1;
		}
		total_cnt += write_cnt;
	}
	return 0;
}

part_info_t *add_part(parts_hdr_t **hdr)
{
	int part_cnt = 0;
	if (*hdr)
		part_cnt = (*hdr)->part_cnt;

	part_cnt++;

	parts_hdr_t *tmp_parts_hdr = realloc(*hdr, offsetof(parts_hdr_t, parts) + sizeof(part_info_t) * part_cnt);
	if (tmp_parts_hdr) {
		tmp_parts_hdr->part_cnt = part_cnt;
		memset(&(tmp_parts_hdr->parts[part_cnt - 1]), 0, sizeof(part_info_t));

		*hdr = tmp_parts_hdr;

		return &(tmp_parts_hdr->parts[part_cnt - 1]);
	}

	return NULL;
}

part_info_t *ins_part(parts_hdr_t **hdr, int num)
{
	part_info_t *new = add_part(hdr);
	if (new) {
		memmove(&((*hdr)->parts[num + 1]), &((*hdr)->parts[num]), ((*hdr)->part_cnt - 1 - num) * sizeof(part_info_t));
		memset(&((*hdr)->parts[num]), 0, sizeof(part_info_t));
		return &((*hdr)->parts[num]);
	}

	return NULL;
}

void free_parts(parts_hdr_t **hdr)
{
	if (!*hdr)
		return;

	free(*hdr);
	*hdr = NULL;
}

int create_parts_arr(int argc, char *argv[], parts_hdr_t **hdr)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *one_arg = strdup(argv[i]);

		char *p = strstr(one_arg, ":");

		if (p) {
			char *name = one_arg;
			char *file = p + 1;

			*p = 0;

			if (*name == 0 || *file == 0) {
				printf("%s - empty partition name or file name\n", argv[i]);
				free(one_arg);
				goto err;
			}

			if (access(file, F_OK) != 0) {
				printf("%s - file do not exist\n", file);
				free(one_arg);
				goto err;
			}

			part_info_t *new_part = add_part(hdr);

			if (new_part) {
				strcpy(new_part->name, name);
				strcpy(new_part->part, name);
				realpath(file, new_part->path);
			}
			else {
				printf("%s[%d] - can not allocate memory\n", __func__, __LINE__);
				free(one_arg);
				goto err;
			}
		}

		free(one_arg);
	}

	return 0;

err:
	free_parts(hdr);
	return -1;
}

int add_pdls(parts_hdr_t **hdr)
{
	part_info_t *part_info = fullfw_find_part(*hdr, "pdl1");
	int cnt;

	if (!part_info) {
		part_info = ins_part(hdr, 0);
		if (!part_info)
			return -1;
		strcpy(part_info->part, "pdl1");
		strcpy(part_info->name, "pdl1");

		cnt = 0;
		cnt += sprintf(part_info->path + cnt, "%s", get_prog_dir());
		cnt += sprintf(part_info->path + cnt, "%s", PDL1_PATH);

		printf("local pdl1 used\n");
	}
	part_info->loadaddr = PDL1_ADDR;

	part_info = fullfw_find_part(*hdr, "pdl2");
	if (!part_info) {
		part_info = ins_part(hdr, 1);
		if (!part_info)
			return -1;
		strcpy(part_info->part, "pdl2");
		strcpy(part_info->name, "pdl2");

		cnt = 0;
		cnt += sprintf(part_info->path + cnt, "%s", get_prog_dir());
		cnt += sprintf(part_info->path + cnt, "%s", PDL2_PATH);

		printf("local pdl2 used\n");
	}
	part_info->loadaddr = PDL2_ADDR;

	return 0;
}

void calc_parts(parts_hdr_t *hdr)
{
	part_info_t *part_info;
	int total_size = 0;

	part_foreach(hdr, part_info) {
		struct stat stat_buf;
		stat(part_info->path, &stat_buf);

		part_info->size = stat_buf.st_size;
		part_info->offset = total_size;
		total_size += part_info->size;
	}
}

int pack_img(parts_hdr_t *hdr)
{
	int fd = open(FULLFW_NAME, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if ( fd < 0) {
		printf("can't create new file: %s\n", FULLFW_NAME);
		return -1;
	}

	write_cont(fd, (char *)hdr, PARTS_HDR_SIZE(hdr));

	part_info_t *part_info;
	part_foreach(hdr, part_info) {
		mmap_file_t *file = load_file(part_info->path);
		write_cont(fd, file->buf.data, file->buf.size);
		close_file(file);
	}

	close(fd);
}

void usage(void)
{
	printf("\t-p part_name1:part_file1 ... part_nameN:part_fileN - pack image\n");
	printf("\t-u fullfw_file - unpack image\n");
	printf("\t-s fullfw_file - show image info\n");
}

int show_img_info(char *file_name)
{
	mmap_file_t *file = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	parts_hdr_t *hdr = (parts_hdr_t *)file->buf.data;
	part_info_t *part_info;

	printf("=======================\n");
	part_foreach(hdr, part_info) {
		prn_part_info(part_info);
		printf("=======================\n");
	}

	close_file(file);

	return 0;
}

int unpack_img(char *file_name)
{
	mmap_file_t *file = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	parts_hdr_t *hdr = (parts_hdr_t *)file->buf.data;
	part_info_t *part_info;

	part_foreach(hdr, part_info) {
		if (!access(part_info->part, F_OK))
			if (unlink(part_info->part)) {
				printf("can't delete old file: %s\n", part_info->part);
				continue;
			}

		int fd = open(part_info->part, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (fd < 0) {
			printf("can't create new file: %s\n", part_info->part);
			continue;
		}

		int total_cnt = 0;

		char *ptr = PARTS_DATA_BASE(hdr) + part_info->offset;

		write_cont(fd, ptr, part_info->size);

		close(fd);
	}

	close_file(file);

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc > 1) {
		if (strcmp(argv[1], "-p") == 0) {
			parts_hdr_t *parts_hdr = NULL;

			if (create_parts_arr(argc, argv, &parts_hdr))
				return -1;

			add_pdls(&parts_hdr);

			calc_parts(parts_hdr);

			pack_img(parts_hdr);

			free_parts(&parts_hdr);
		}
		else if (strcmp(argv[1], "-u") == 0) {
			if (argc != 3) {
				usage();
				return 0;
			}
			unpack_img(argv[2]);
		}
		else if (strcmp(argv[1], "-s") == 0) {
			if (argc != 3) {
				usage();
				return 0;
			}
			show_img_info(argv[2]);
		}
		else {
			usage();
			return 0;
		}
	}
	else {
		usage();
		return 0;
	}

	return 0;
}
