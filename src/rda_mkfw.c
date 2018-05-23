#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fullfw.h"
#include "file_mmap.h"

#define FULLFW_NAME "fullfw.img"

parts_hdr_t *parts_hdr = NULL;

void free_parts(void)
{
	int i;

	if (!parts_hdr)
		return;

	free(parts_hdr);
	parts_hdr = NULL;
}

int create_parts_arr(int argc, char *argv[])
{
	int part_cnt = 0;
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

			part_cnt++;

			parts_hdr_t *tmp_parts_hdr = realloc(parts_hdr, offsetof(parts_hdr_t, parts) + sizeof(part_info_t) * part_cnt);
			if (tmp_parts_hdr) {
				parts_hdr = tmp_parts_hdr;

				parts_hdr->part_cnt = part_cnt;
				memset(&parts_hdr->parts[part_cnt - 1], 0, sizeof(part_info_t));

				strcpy(parts_hdr->parts[part_cnt - 1].name, name);
				strcpy(parts_hdr->parts[part_cnt - 1].part, name);
				realpath(file, parts_hdr->parts[part_cnt - 1].path);

				struct stat stat_buf;
				stat(file, &stat_buf);

				parts_hdr->parts[part_cnt - 1].size = stat_buf.st_size;
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
	free_parts();
	return -1;
}

int pack_img(void)
{
	int fd = open(FULLFW_NAME, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if ( fd < 0) {
		printf("can't create new file: %s\n", FULLFW_NAME);
		return -1;
	}
}

void usage(void)
{
	printf("\t-p part_name1:part_file1 ... part_nameN:part_fileN - pack image\n");
	printf("\t-u fullfw_file - unpack image\n");
	printf("\t-s fullfw_file - show image info\n");
}

int show_img_info(char *file_name)
{
	int i;
	mmap_file_t *file = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	parts_hdr_t *hdr = (parts_hdr_t *)file->buf.data;

	printf("=======================\n");
	for (i = 0; i < hdr->part_cnt; i++) {
		prn_part_info(&hdr->parts[i]);
		printf("=======================\n");
	}

	close_file(file);

	return 0;
}

int unpack_img(char *file_name)
{
	int i;
	mmap_file_t *file = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	parts_hdr_t *hdr = (parts_hdr_t *)file->buf.data;

	for (i = 0; i < hdr->part_cnt; i++) {
		if (!access(hdr->parts[i].part, F_OK))
			if (unlink(hdr->parts[i].part)) {
				printf("can't delete old file: %s\n", hdr->parts[i].part);
				continue;
			}

		int fd = open(hdr->parts[i].part, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (fd < 0) {
			printf("can't create new file: %s\n",hdr->parts[i].part);
			continue;
		}

		int total_cnt = 0;

		char *ptr = PARTS_DATA_BASE(hdr) + hdr->parts[i].offset;

		while (total_cnt < hdr->parts[i].size) {
			int write_cnt = write(fd, ptr + total_cnt, hdr->parts[i].size - total_cnt);
			if (write_cnt < 0) {
				printf("write to file: %s failed\n", hdr->parts[i].part);
				break;
			}
			total_cnt += write_cnt;
		}
		close(fd);
	}

	close_file(file);

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc > 1) {
		if (strcmp(argv[1], "-p") == 0) {
			if (create_parts_arr(argc, argv))
				return -1;

			int i;
			for (i = 0; i < parts_hdr->part_cnt; i++)
				printf("%s - %s\n", parts_hdr->parts[i].part, parts_hdr->parts[i].path);

			free_parts();
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
