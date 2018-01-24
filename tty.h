#ifndef __TTY_H__
#define __TTY_H__

#define TTY_DEV "/dev/ttyUSB0"
#define BAUDRATE B921600

int open_tty(void);
void close_tty(void);
int write_tty(char *buf, size_t len);

#endif
