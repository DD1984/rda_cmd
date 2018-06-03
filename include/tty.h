#ifndef __TTY_H__
#define __TTY_H__

#define BAUDRATE B921600

#define TTY_WAIT_RX_TIMEOUT 60 // сек
#define TTY_RX_PKT_TIMEOUT 1 // сек

int open_tty(void);
void close_tty(void);
int read_tty(char *buf, size_t len);
int write_tty(char *buf, size_t len);
void tty_flush(void);

int get_tty_timeout(void);
int set_tty_timeout(int timeout);

#endif
