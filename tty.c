#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "tty.h"

int tty_fd;

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

	tty_attr.c_cc[VMIN]  = 0;				// read doesn't block
	tty_attr.c_cc[VTIME] = 10;				// 10 x 0.1 seconds read timeout

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
	tty_fd = open(TTY_DEV, O_RDWR | O_NOCTTY | O_SYNC);

	if (tty_fd < 0) {
		printf("Can't open device: %s\n", TTY_DEV);
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
	return read(tty_fd, buf, len);
}
