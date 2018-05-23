#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fullfw.h"
#include "file_mmap.h"

typedef struct
{
	char *name;
	char *file;
} part_t;

int part_cnt = 0;
part_t *parts = NULL;

void free_parts(void)
{
	int i;

	if (!parts)
		return;

	for (i = 0; i < part_cnt; i++) {
		if (parts[i].name)
			free(parts[i].name);
		if( parts[i].file)
			free(parts[i].file);
	}
	free(parts);
	parts = NULL;
	part_cnt = 0;
}

int create_parts_arr(int argc, char *argv[])
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

			part_cnt++;

			part_t *tmp_parts = realloc(parts, sizeof(part_t) * part_cnt);
			if (tmp_parts) {
				parts = tmp_parts;
				parts[part_cnt -1].name = malloc(strlen(name) + 1);
				if (parts[part_cnt -1].name)
					strcpy(parts[part_cnt - 1].name, name);

				parts[part_cnt -1].file = malloc(strlen(file) + 1);
				if (parts[part_cnt -1].file)
					strcpy(parts[part_cnt - 1].file, file);
			}

			if (!tmp_parts || !parts[part_cnt - 1].name || !parts[part_cnt - 1].file) {
				printf("%[%d] - can not allocate memory\n");
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


void usage(void)
{
	printf("\t-p part_name1:part_file1 ... part_nameN:part_fileN - pack image\n");
	printf("\t-u fullfw_file - unpack image\n");
	printf("\t-s fullfw_file - show image info\n");
}

int show_img_info(char *file_name)
{
	mmap_file_t *file = NULL;
	part_info_t *part_info = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	printf("=======================\n");
	part_foreach(part_info, file) {
		prn_part_info(part_info);
		printf("=======================\n");
	}

	close_file(file);

	return 0;
}

int unpack_img(char *file_name)
{
	mmap_file_t *file = NULL;
	part_info_t *part_info = NULL;

	file = load_file(file_name);
	if (!file)
		return -1;

	part_foreach(part_info, file) {
		if (!access(part_info->part, F_OK))
			if (unlink(part_info->part)) {
				printf("can't delete old file: %s\n", part_info->part);
				continue;
			}

		int fd = open(part_info->part, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if ( fd < 0) {
			printf("can't create new file: %s\n", part_info->part);
			continue;
		}

		int total_cnt = 0;
		char *ptr = get_part_ptr(file, part_info);

		while (total_cnt < part_info->size) {
			int write_cnt = write(fd, ptr + total_cnt, part_info->size - total_cnt);
			if (write_cnt < 0) {
				printf("write to file: %s failed\n", part_info->part);
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
			for (i = 0; i < part_cnt; i++)
				printf("%s - %s\n", parts[i].name, parts[i].file);

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
