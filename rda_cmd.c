#include <stdio.h>
#include <unistd.h>

#include "types.h"
#include "tty.h"

#include "packet.h"


int tty_fd;

int main(void)
{
	if ((tty_fd = open_tty()) < 0)
		return -1;
	
	
	close_tty(tty_fd);
	return 0;
}
