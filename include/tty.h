#ifndef __TTY_H__
#define __TTY_H__

#define TTY_DEV "/dev/ttyACM0"
#define BAUDRATE B921600

#define DEFAULT_TTY_READ_TIMEOUT 60

int open_tty(void);
void close_tty(void);
int read_tty(char *buf, size_t len);
int write_tty(char *buf, size_t len);
void tty_flush(void);

int get_tty_timeout(void);
int set_tty_timeout(int timeout);

#endif
