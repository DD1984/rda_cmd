#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

char *get_prog_dir(void)
{
	static char dir[1024];
	char buf[32];

	sprintf(buf, "/proc/%d/exe", getpid());

	ssize_t ret = readlink(buf, dir, sizeof(dir));
	if (ret < 0) {
		dir[0] = 0;
	}
	else {
		dir[ret] = 0;
		int i = strlen(dir) - 1;
		while (i >=0 && dir[i] != '/')
			i--;
		dir[i + 1] = 0;
	}
	return dir;
}
