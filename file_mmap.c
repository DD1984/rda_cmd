#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "file_mmap.h"

mmap_file_t *load_file(char *path)
{
	mmap_file_t *file = malloc(sizeof(mmap_file_t));

	if (!file)
		return NULL;

	file->fd = open(path, O_RDONLY);
	if (file->fd < 0) {
		printf("can't open file: %s\n", path);
		free(file->buf.data);
		return NULL;
	}
	struct stat stat_buf;
	stat(path, &stat_buf);

	file->buf.size = stat_buf.st_size;
	file->buf.data = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, file->fd, 0);
	if (file->buf.data == NULL) {
		printf("can't mmap file: %s\n", path);
		free(file->buf.data);
		close(file->fd);
		return NULL;
	}
	return file;
}

void close_file(mmap_file_t *file)
{
	if (!file)
		return;

	munmap(file->buf.data, file->buf.size);
	close(file->fd);
	free(file);
}
