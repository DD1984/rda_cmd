#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <ncurses.h>

#define TTY_DEV "/dev/ttyUSB0"
#define BAUDRATE B38400

#define START_STR "Booting..."
#define END_STR "D-link init done"

#define FULL_LOG "pwr_full.log"
#define INFO_LOG "pwr_info.log"
#define CUR_LOG "pwr_cur.log"
#define CNT_LOG "pwr_cnt.log"

#define MAX_STR 1024
#define DEV_WAN_ADDR "192.168.3.1" //for ping
#define PING_CNT 6

//#define STOP_ON_FAIL

unsigned int power_cycle_cnt = 0;
unsigned int tasks_reset_cnt = 0;
unsigned int warn_cnt = 0;

int fd_dev = -1;
int cur_power_state = 0; //выключено
int first_start = 1;

#ifdef NC
WINDOW *log_win = NULL;
WINDOW *stat_win = NULL;
WINDOW *info_win = NULL;
#endif

void get_time_str(char *buf)
{
	time_t t;
	t = time(NULL);
	sprintf(buf, "%s", ctime(&t));
	if (buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = 0;
}

#define APPEND_FILE(name, args...) {\
	char time_buf[64];\
	get_time_str(time_buf);\
	FILE *file_fd = fopen(name, "a");\
	if (file_fd) {\
		fprintf(file_fd, "[%s] ", time_buf);\
		fprintf(file_fd, args);\
		fclose(file_fd);\
	}\
}

#ifdef NC
#define INFOP(x...) {\
	char tmp_buf[64];\
	get_time_str(tmp_buf);\
	wprintw(info_win, "[%s] ", tmp_buf);\
	wprintw(info_win, x);\
	wrefresh(info_win);\
	APPEND_FILE(INFO_LOG, x);\
}
#define LOGP(x...) {wprintw(log_win, x); wrefresh(log_win);}
#define STATP() {wclear(stat_win); wprintw(stat_win, "--- power_cycle_cnt: %d; tasks_reset_cnt: %d; warn_cnt: %d ---", power_cycle_cnt, tasks_reset_cnt, warn_cnt); wrefresh(stat_win);}
#else
#define INFOP(x...) {APPEND_FILE(INFO_LOG, x);}
#define LOGP(x...) {}
#define STATP() {printf("--- power_cycle_cnt: %d; tasks_reset_cnt: %d; warn_cnt: %d ---\n", power_cycle_cnt, tasks_reset_cnt, warn_cnt);\
 unlink(CNT_LOG); APPEND_FILE(CNT_LOG, "--- power_cycle_cnt: %d; tasks_reset_cnt: %d; warn_cnt: %d ---\n", power_cycle_cnt, tasks_reset_cnt, warn_cnt);}
#endif

#define PL2303_GPIO_SET     _IOW('v', 0xac, int) //ioctl to write a GPIO register
#define PL2303_GPIO_GET     _IOR('v', 0xac, int) //ioctl to read a GPIO register
#define L_U8_GPIO_DIR(gpio)     ((unsigned char)(1 << ((gpio) + 4))) //macro function to set direction bit of a gpio pin
#define L_U8_GPIO_VALUE(gpio)   ((unsigned char)(1 << ((gpio) + 6))) //macro function to set the bit of a gpio pin

int pl2303_gpio_out(int fd_dev, int gpio, int level)
{
	int arg;

	/* read current configuration */
	if (ioctl(fd_dev, PL2303_GPIO_GET, &arg))
		return -1;

	/* construct new register value */
	arg |= L_U8_GPIO_DIR(gpio);
	if(level)
		arg |= L_U8_GPIO_VALUE(gpio);
	else
		arg &= ~L_U8_GPIO_VALUE(gpio);

	/* write new value */
	if (ioctl(fd_dev, PL2303_GPIO_SET, &arg))
		return -1;

	return 0;
}

int check_gpio_ioctl(int fd_dev)
{
	int test;
	if (ioctl(fd_dev, PL2303_GPIO_GET, &test))
		return -1;
	return 0;
}

void power_ctrl(int on)
{
	if (on) {
		pl2303_gpio_out(fd_dev, 0, 1);
		INFOP("Power On\n");
	}
	else {
		pl2303_gpio_out(fd_dev, 0, 0);
		INFOP("Power Off\n");
	}
	sleep(1);

	if (!cur_power_state && on) {
		power_cycle_cnt++;
		STATP();
	}
	cur_power_state = on;
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

	tty_attr.c_cc[VMIN]  = 0;				// read doesn't block
	tty_attr.c_cc[VTIME] = 1;				// 0.1 seconds read timeout

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


char tty_str[MAX_STR] = {0};

int tty_get_str(int fd_dev)
{
	static char tty_buf[MAX_STR] = {0};
	static unsigned int tty_buf_ind = 0;

	char c;

	c = 0;
	int ret = read(fd_dev, &c, 1);
	if (ret == 0)
		return -1;

	if (ret < 0) {
		INFOP("[%d] %s\n", errno, strerror(errno));
		exit(-1);
		return -1;
	}

	if (c == 0)
		return -1;

	tty_buf[tty_buf_ind] = c;
	tty_buf_ind++;

	if ((c == '\n')  || (tty_buf_ind == MAX_STR - 1)) {

		if (tty_buf_ind == MAX_STR - 1) {
			INFOP("STRING FROM TTY MORE THEN %s\n", MAX_STR);
#ifndef NC
			printf("STRING FROM TTY MORE THEN %s\n", MAX_STR);
#endif

		}
		tty_buf[tty_buf_ind] = 0;

		char *ptr = tty_str;
		int i;
		for (i = 0; i <= tty_buf_ind; i++) {
			if (tty_buf[i] != '\r') {
				*ptr = tty_buf[i];
				ptr++;
			}
		}

		tty_buf_ind = 0;

		return 0;
	}

	return -1;
}

typedef enum {INPROGRESS, COMPLETE, ERROR} check_func_t;

void task_wait_start_str_start(void)
{
	power_ctrl(1);
	unlink(CUR_LOG);
	system("touch "CUR_LOG);
}

check_func_t task_wait_start_str_check(void)
{
	if (tty_str[0]) {
		//стартовая строка
		if (strstr(tty_str, START_STR))
			return COMPLETE;
	}
	return INPROGRESS;

}

check_func_t task_wait_end_str_check(void)
{
	if (tty_str[0]) {
		//конечная строка
		if (strstr(tty_str, END_STR))
			return COMPLETE;
	}
	return INPROGRESS;
}

void task_cat_sfp_i2c_start(void)
{
	char c = '\n';
	write(fd_dev, &c, 1);

	char cat_sfp_i2c[] = "cat /proc/sfp_i2c\n";
	write(fd_dev, cat_sfp_i2c, sizeof(cat_sfp_i2c));
}

void task_cat_port_status_start(void)
{
	char c = '\n';
	write(fd_dev, &c, 1);

	char cat_port_status[] = "cat /proc/rtl865x/port_status\n";
	write(fd_dev, cat_port_status, sizeof(cat_port_status));
}

int ping_status = 1;

FILE *ping_fd = NULL;

void task_ping_start(void)
{
	char cmd[64];
	sprintf(cmd, "ping "DEV_WAN_ADDR" -c %d", PING_CNT);
	ping_fd = popen(cmd, "r");
}

check_func_t task_ping_check(void)
{
	char buf[128];

	unsigned int tx_pkts, rx_pkts, percent;

	check_func_t ret = COMPLETE;

	if (ping_fd) {
		char *ptr = fgets(buf, sizeof(buf), ping_fd);
		if (ptr != NULL) {
			APPEND_FILE(FULL_LOG, "%s", buf);
			APPEND_FILE(CUR_LOG, "%s", buf);
			INFOP("%s", buf);

			ret = INPROGRESS;

			if (sscanf(buf, "%u packets transmitted, %u received, %u%% packet loss", &tx_pkts, &rx_pkts, &percent) == 3)
				if (rx_pkts != 0)
					ret = COMPLETE;
				else
					ret = ERROR;
		}
	}
	if (ret != INPROGRESS) {
		if (ping_fd) {
			pclose(ping_fd);
			ping_fd = NULL;
		}
	}
	return ret;
}

void task_ping_end(void)
{
	power_ctrl(0);
}

typedef struct {
	char name[32];
	void (*start_action)(void);
	check_func_t (*check_action)(void);
	void (*end_action)(void);
	unsigned int timeout;
	char always_check; //проверять каждый проход
	char critical; //reset after timeout, должна закончится не по таймауту и в свое время
	unsigned int flags;
	#define FTIMEOUT 0x0001
} task_t;


char *warn_strings[] = {
"break by timeout",
"fiber AN error",
//"SFP init done!!!"
};

void tasks_reset(int save);

void warn_string_check(void)
{
	int i;
	if (tty_str[0]) {
		for (i = 0; i < sizeof(warn_strings) / sizeof(warn_strings[0]); i++) {
			if (strstr(tty_str, warn_strings[i])) {
				warn_cnt++;
				STATP();
				INFOP("Warn String found: \"%s\"\n", warn_strings[i]);
			}
		}
#if 0
		//костылик
		if (strstr(tty_str,  "fiber link status change")) {
			sleep(1);
			tasks_reset(0);
		}
#endif
	}
}

task_t tasks[] = {
	{"wait_start_str", task_wait_start_str_start, task_wait_start_str_check, NULL, 3, 1, 1, 0},
	{"wait_end_str", NULL, task_wait_end_str_check, NULL, 35, 1, 1, 0},
	{"pause", NULL, NULL, NULL, 5, 0, 0, 0},
	{"cat_sfp_i2c", task_cat_sfp_i2c_start, NULL, NULL, 1, 0, 0, 0},
	{"cat_port_status", task_cat_port_status_start, NULL, NULL, 1, 0, 0, 0},
	{"ping", task_ping_start, task_ping_check, task_ping_end, PING_CNT*3, 0, 1, 0},
};

task_t *cur_task = &tasks[0];

struct itimerval timer;

void cur_task_start_action_exec(void)
{
	INFOP("\"%s\" - start%s - timeout:%d\n", cur_task->name, cur_task->start_action ? "" : "(NULL)", cur_task->timeout);
	if (cur_task->start_action)
		cur_task->start_action();

	//старт таймера
	if (cur_task->timeout) {
		timer.it_value.tv_sec = cur_task->timeout;
		timer.it_value.tv_usec = 0;
		timer.it_interval.tv_sec = 0;
		timer.it_interval.tv_usec = 0;

		if (setitimer(ITIMER_REAL, &timer, 0)) {
			INFOP("%s[%d] Set timer ERROR!!!\n", __func__, __LINE__);
		}
	}
}

void cur_task_end_action_exec(void)
{
	//стоп таймера
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &timer, 0)) {
		INFOP("%s[%d] Set timer ERROR!!!\n", __func__, __LINE__);
	}

	INFOP("\"%s\" - end%s\n", cur_task->name, cur_task->end_action ? "" : "(NULL)");
	if (cur_task->end_action)
		cur_task->end_action();
}

typedef void (*sighandler_t)(int);

void task_timeout(int theint)
{
	cur_task->flags |= FTIMEOUT;
}

void tasks_reset(int save)
{
	INFOP("RESET tasks sequence, save:%d\n", save);

#ifdef STOP_ON_FAIL
	APPEND_FILE(CNT_LOG, "STOP on \"%s\"\n", cur_task->name);
	APPEND_FILE(CUR_LOG, "STOP on \"%s\"\n", cur_task->name);
	INFOP("STOP on \"%s\"\n", cur_task->name);
#endif

	if (save) {
		tasks_reset_cnt++;
		STATP();

		char time_buf[64];
		char cmd_buf[128];
		get_time_str(time_buf);
		sprintf(cmd_buf, "cp "CUR_LOG" \"%s.log\"", time_buf);
		INFOP("%s\n", cmd_buf);
		system(cmd_buf);
	}

#ifdef STOP_ON_FAIL
	while (1);
#endif

	cur_task = &tasks[0];
	first_start = 1;

	power_ctrl(0);

}

void switch_task()
{
	if (first_start) {
		first_start = 0;
		cur_task_start_action_exec();
		return;
	}

	int next_task = 0;
	if (cur_task->flags & FTIMEOUT) {
		INFOP("\"%s\" - timeout timer_expered\n", cur_task->name);
		cur_task->flags &= ~FTIMEOUT;

		if (cur_task->critical) {
			tasks_reset(1);
			return;
		}
		else {
			next_task = 1;
		}
	}

	task_t *tmp_task_p = &tasks[0];
	do {
		check_func_t ret = INPROGRESS;
		if (tmp_task_p == cur_task || tmp_task_p->always_check)
			if (tmp_task_p->check_action)
				ret = tmp_task_p->check_action();

		if (ret == COMPLETE) {
			if (tmp_task_p != cur_task) {

				INFOP("task sequence violation task: \"%s\" cur_task: \"%s\"\n", tmp_task_p->name, cur_task->name);

				if (tmp_task_p->critical) {
					tasks_reset(1);
					return;
				}

				if (cur_task->critical) {
					tasks_reset(1);
					return;
				}
			}
			else {
				next_task = 1;
			}
		}

		if (ret == ERROR && tmp_task_p == cur_task) {
			INFOP("current(\"%s\") task check ERROR\n", cur_task->name);
			if (cur_task->critical) {
				tasks_reset(1);
				return;
			}
			else {
				next_task = 1;
			}
		}

		tmp_task_p++;
	} while (tmp_task_p <= cur_task);

	if (next_task) {
		INFOP("\"%s\" - complite\n", cur_task->name);

		cur_task_end_action_exec();

		if (&tasks[sizeof(tasks) / sizeof(tasks[0]) - 1] == cur_task) {
			cur_task = &tasks[0];
			INFOP("***************\n");
		}
		else {
			cur_task++;
		}

		cur_task_start_action_exec();
	}
}



int main(int argc, char *argv[])
{
	int ret = 0;

	fd_dev = open(TTY_DEV, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd_dev < 0) {
		printf("Can't open device: %s\n", TTY_DEV);
		ret = -1;
		goto end;
	}

	if (ret = check_gpio_ioctl(fd_dev)) {
		printf("Can't execute special pl2303 gpio ioctl\n");
		goto end;
	}

	if (set_tty_attr(fd_dev, BAUDRATE)) {
		printf("Can't set tty attr\n");
		goto end;
	}

#ifdef NC
	//ncurces
	initscr();
	log_win = newwin(getmaxy(stdscr) - 20, getmaxx(stdscr), 0, 0);
	stat_win = newwin(1, getmaxx(stdscr), getmaxy(stdscr) - 20, 0);
	info_win = newwin(18, getmaxx(stdscr), getmaxy(stdscr) - 18, 0);
	scrollok(log_win, TRUE);
	//idlok(log_win, TRUE);
	scrollok(info_win, TRUE);

	signal (SIGWINCH, NULL);
	//
#endif

	signal(SIGALRM, (sighandler_t)task_timeout);


	unlink(FULL_LOG);
	unlink(INFO_LOG);
	unlink(CUR_LOG);

	power_ctrl(cur_power_state);

	while (1) {

		tty_get_str(fd_dev);

		switch_task();

		warn_string_check();

		if (tty_str[0]) {
			LOGP("%s", tty_str);

			APPEND_FILE(FULL_LOG, "%s", tty_str);
			APPEND_FILE(CUR_LOG, "%s", tty_str);

		}

		tty_str[0] = 0; //строку все обработали - уничтожаем
	}

end:
	if (fd_dev >= 0)
		close(fd_dev);
	return ret;
}
