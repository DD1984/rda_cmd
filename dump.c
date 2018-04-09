#include <stdio.h>

void dump_line(void *addr, int len, int line_len)
{
	if (len <= 0)
		return;
	char *ptr = (char *)addr;
	while (ptr - (char *)addr < len) {
		printf("%02x ", (unsigned char)*ptr);
		ptr++;
	}
	
	int i;
	for (i = 0; i < line_len - len; i++)
		printf("   ");

	ptr = (char *)addr;
	while (ptr - (char *)addr < len) {
		printf("%c", ((unsigned char)*ptr < 0x20 || (unsigned char)*ptr > 0x7e) ? '.' : (unsigned char)*ptr);
		ptr++;
	}

	printf("\n");
}

void _hex_dump(void *addr, int len, int line_len)
{
	char *ptr = (char *)addr;
	while (ptr - (char *)addr < len) {
		//printf(" %04zx  ", (size_t)ptr - (size_t)addr);
		dump_line(ptr, ((char *)addr + len - ptr > line_len) ? line_len : (char *)addr + len - ptr, line_len);
		ptr += line_len;
	}
}
