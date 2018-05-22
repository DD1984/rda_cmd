#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "fullfw.h"

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
}

int main(int argc, char *argv[])
{
	int ret = -1;

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

	for (i = 0; i < part_cnt; i++)
		printf("%s - %s\n", parts[i].name, parts[i].file);

	ret = 0;
err:
	free_parts();
	return ret;
}
