#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/select.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/time.h>

#include "tty.h"

int tty_fd;

#define SYSFS_TTY_DIR "/sys/class/tty/"
#define DEV_VID 0x0525
#define DEV_PID 0xa4a7
#define DEV_NAME "ttyACM"
int tty_timeout = TTY_WAIT_RX_TIMEOUT;

int find_dev(void)
{
	DIR *dir = opendir(SYSFS_TTY_DIR);
	struct dirent *dp;

	if (!dir) {
		printf("can't open "SYSFS_TTY_DIR"\n");
		return -1;
	}

	char * file_name;
	while ((dp = readdir(dir)) != NULL) {
		if (strncmp(dp->d_name, DEV_NAME, strlen(DEV_NAME)))
			continue;

		char path[1024];
		char real_path[1024];

		sprintf(path, SYSFS_TTY_DIR"%s", dp->d_name);

		if (!realpath(path, real_path))
			goto find_err;

		sprintf(path, "%s/device", real_path);
		if (!realpath(path, real_path))
			goto find_err;

		int i = strlen(real_path) - 1;
		while (i >=0 && real_path[i] != '/')
			i--;
		real_path[i + 1] = 0;

		unsigned short pid, vid;
		FILE *fd;

		sprintf(path, "%s/idVendor", real_path);
		fd = fopen(path, "r");
		if (fd < 0)
			goto find_err;
		fscanf(fd, "%hx", &vid);
		fclose(fd);

		sprintf(path, "%s/idProduct", real_path);
		fd = fopen(path, "r");
		if (fd < 0)
			goto find_err;
		fscanf(fd, "%hx", &pid);
		fclose(fd);


		if (pid == DEV_PID && vid == DEV_VID) {
			int dev_num;
			sscanf(dp->d_name, DEV_NAME"%d", &dev_num);

			return dev_num;
		}
	}

find_err:
	if (dir)
		closedir(dir);

	return -1;
}

int check_used(char *name)
{
	DIR *proc = NULL;
	struct dirent *proc_entry;

	proc = opendir("/proc");
	if (!proc) {
		printf("can't open procfs\n");
		return -1;
	}

	while((proc_entry = readdir(proc)) != NULL) {
		pid_t pid = strtoul(proc_entry->d_name, NULL, 10);
		if (pid != 0) {
			char path[1024];

			sprintf(path, "/proc/%d/fd", pid);

			DIR *proc_fd = opendir(path);
			struct dirent *proc_fd_entry;

			if (!proc_fd)
				continue;

			while((proc_fd_entry = readdir(proc_fd)) != NULL) {
				char link[1024];
				sprintf(path, "/proc/%d/fd/%s", pid, proc_fd_entry->d_name);
				int ret = readlink(path, link, sizeof(link));
				if (ret == -1 || ret >= sizeof(link))
					continue;

				link[ret] = 0;

				if (strcmp(link, name) == 0) {
					printf("%s used by pid: %d\n", name, pid);
					return -1;
				}
			}
			closedir(proc_fd);
		}
	}
	closedir(proc);

	return 0;
}

int get_tty_timeout(void)
{
	return tty_timeout;
}

int set_tty_timeout(int timeout)
{
	tty_timeout = timeout;
}

int set_tty_attr(int fd_dev, int speed)
{
	struct termios tty_attr;
	memset(&tty_attr, 0, sizeof(tty_attr));

	if (tcgetattr(fd_dev, &tty_attr))
		return -1;

	cfsetispeed(&tty_attr, speed);
	cfsetospeed(&tty_attr, speed);

	tty_attr.c_cflag = (tty_attr.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty_attr.c_iflag &= ~IGNBRK;			// disable break processing
	tty_attr.c_lflag = 0;					// no signaling chars, no echo,
											// no canonical processing
	tty_attr.c_oflag = 0;					// no remapping, no delays

	// generic read
	tty_attr.c_cc[VMIN]  = 0;
	tty_attr.c_cc[VTIME] = TTY_RX_PKT_TIMEOUT * 10;

	tty_attr.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty_attr.c_cflag |= (CLOCAL | CREAD);	// ignore modem controls,
											// enable reading
	tty_attr.c_cflag &= ~(PARENB | PARODD); // shut off parity

	tty_attr.c_cflag &= ~CSTOPB;
	tty_attr.c_cflag &= ~CRTSCTS;

	//запрещает преобразования переводов строки!!!
	tty_attr.c_oflag &= ~ONLCR;
	tty_attr.c_iflag &= ~ICRNL;

	if (tcsetattr(fd_dev, 0, &tty_attr))
		return -1;
	return 0;
}

int open_tty(void)
{
	int dev_num = find_dev();
	if (dev_num < 0) {
		printf("can't find device\n");
		return -1;
	}

	char buf[64];
	sprintf(buf, "/dev/"DEV_NAME"%d", dev_num);

	printf("using %s\n", buf);

	while (check_used(buf) != 0) {
		printf("waithing...\n");
		sleep(1);
	}

	tty_fd = open(buf, O_RDWR | O_NOCTTY | O_SYNC);

	if (tty_fd < 0) {
		printf("Can't open device: %s\n", buf);
		goto end;
	}

	if (set_tty_attr(tty_fd, BAUDRATE)) {
		printf("Can't set tty attr\n");
		goto end;
	}

	return 0;

end:
	if (tty_fd >= 0)
		close(tty_fd);
	return -1;
}

void close_tty(void)
{
	close(tty_fd);
}

int write_tty(char *buf, size_t len)
{
	return write(tty_fd, buf, len);
}

int read_tty(char *buf, size_t len)
{
	fd_set rfds;
	struct timeval tv;
	ssize_t retval;

	FD_ZERO(&rfds);
	FD_SET(tty_fd, &rfds);

	tv.tv_sec = tty_timeout;
	tv.tv_usec = 0;

	retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

	if (retval == -1) {
		int errsv = errno;
		printf("select() err: (%d)%s\n", errsv, strerror(errsv));
		return -1;
	}
	if (retval == 0) {
		printf("tty wait timeout expired (%d sec)\n", tty_timeout);
		return -1;
	}

	int total = 0;
	while (total < len) {
		struct timeval before, after;
		unsigned int _before, _after;

		gettimeofday(&before, NULL);

		retval = read(tty_fd, buf + total, len);

		gettimeofday(&after, NULL);

		if (retval < 0) {
			int errsv = errno;
			printf("read() return %zd err: %d(%s)\n", retval, errsv, strerror(errsv));
			return -1;
		}

		_after = after.tv_sec * 100 + after.tv_usec / 10000;
		_before = before.tv_sec * 100 + before.tv_usec / 10000;

		unsigned int delta = _after - _before;

		if (delta > TTY_RX_PKT_TIMEOUT * 100) {
			printf("tty pkt timeout expired (%d sec) read() ret %zd[%zd]\n", TTY_RX_PKT_TIMEOUT, retval, len);
			return -1;
		}
		total += retval;
	}

	return len;
}

void tty_flush(void)
{
	tcflush(tty_fd, TCIOFLUSH);
}
