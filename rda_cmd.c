#include <stdio.h>
#include <unistd.h>

#include "types.h"
#include "tty.h"

#include "packet.h"

int main(void)
{
	if (open_tty() != 0)
		return -1;
	
	
	close_tty();
	return 0;
}
