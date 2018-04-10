#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "tty.h"

#include "types.h"
#include "defs.h"
#include "dump.h"

#include "protocol.h"
#include "cmd_defs.h"
#include "mtdparts_parser.h"

#define PDL1_PATH "pdl1.bin"
#define PDL2_PATH "pdl2.bin"

#define PDL1_ADDR 0x00100100 //spl-uboot start addr
#define PDL2_ADDR 0x80008000 

#define UPLOAD_CHUNK_SIZE (4 * 1024)

int upload_buf(buf_t *buf, u32 data_addr)
{
	int ret;

	if (!buf)
		return -1;

	ret = send_cmd_hdr(START_DATA, data_addr, buf->size);
	if (ret)
		return -1;

	u32 total_send = 0;
	while (total_send < buf->size) {

		buf_t chunk_buf;
		chunk_buf.data = buf->data + total_send;
		chunk_buf.size = UPLOAD_CHUNK_SIZE;
		if ((total_send + UPLOAD_CHUNK_SIZE) > buf->size)
			chunk_buf.size = buf->size - total_send;

		ret = send_cmd_data_to_dev(MID_DATA, &chunk_buf);
		if (ret)
			return -1;

		total_send += chunk_buf.size;
	}

	ret = send_cmd_only(END_DATA);
	if (ret)
		return -1;

	return 0;
}

int upload_file(char *path, u32 data_addr)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("can't open file: %s\n", path);
		return -1;
	}
	struct stat stat_buf;
	stat(path, &stat_buf);

	buf_t buf;

	buf.size = stat_buf.st_size;
	buf.data = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (buf.data == NULL) {
		close(fd);
		printf("can't mmap file: %s\n", path);

		close(fd);
		return -1;
	}

	int ret = upload_buf(&buf, data_addr);
	if (ret)
		ret = -1;

	munmap(buf.data, stat_buf.st_size);
	close(fd);

	return ret;
}

int read_partition_table(void)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(READ_PARTITION_TABLE, &from_dev);
	if (!ret) {
		from_dev.data[from_dev.size + 1] = 0;

		ret = parse_mtdparts(from_dev.data);
	}

	return ret;
}

#define DOWNLOAD_CHUNK_SIZE (256 * 1024) //кратно nand page size
#define FACTORYDATA_SIZE (32 * 1024)

int read_partition(char *name,  char *out_file)
{
	char *buf;
	u32 buf_size;
	u64 part_size;

	if (strcmp("factorydata", name) == 0) {
		buf_size = FACTORYDATA_SIZE;
		part_size = FACTORYDATA_SIZE;
	}
	else {
		buf_size = DOWNLOAD_CHUNK_SIZE;
		part_size = get_part_size(name);
		if (part_size == 0)
			return -1;
	}

	buf = malloc(buf_size);

	u64 total_rcv = 0;

	int fd = open(out_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

	while (total_rcv < part_size) {

		struct command_header cmd_hdr;

		cmd_hdr.cmd_type = READ_PARTITION;
		cmd_hdr.data_addr = total_rcv;
		cmd_hdr.data_size = buf_size;

		buf_t to_dev;

		to_dev.data = name;
		to_dev.size = strlen(name) + 1;

		buf_t from_dev;
		from_dev.data = buf;
		from_dev.size = buf_size;

		int ret = send_cmd(&cmd_hdr, &to_dev, &from_dev);
		if (ret) {
			free(buf);
			close(fd);
			unlink(out_file);
			printf("download error\n");
			return -1;
		}
		write(fd, buf, buf_size);
		total_rcv += buf_size;
	}

	free(buf);
	close(fd);

	return 0;
}

int get_pdl_version(char **ver)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(GET_VERSION, &from_dev);
	if (!ret) {
		if (ver) {
			*ver = malloc(from_dev.size);
			memcpy(*ver, from_dev.data, from_dev.size);
		}
	}

	return ret;
}

int set_pdl_dbg(u32 dbg)
{
	return send_cmd_hdr(SET_PDL_DBG, dbg, 0);
}

int get_pdl_log(void)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(GET_PDL_LOG, &from_dev);
	if (!ret) {
		//from_dev.data[from_dev.size + 1] = 0;
		//printf("[%s]\n", from_dev.data);
		hex_dump(from_dev.data, from_dev.size);
	}

	return ret;
}

int main(void)
{
	int ret = 0;
	u8 buffer[PDL_MAX_DATA_SIZE];
	struct command_header cmd_hdr;
	buf_t outbuf;

	if (open_tty() != 0)
		return -1;

	if (send_cmd_only(CONNECT)) {
		close_tty();
		printf("can't connect to device\n");
		return -1;
	}

	if (get_pdl_version(NULL)) {
		//exec pdl1
		printf("uploading pdl1\n");
		if (upload_file(PDL1_PATH, PDL1_ADDR)) {
			printf("upload pdl1 failed\n");
			return -1;
		}
		send_cmd_only(EXEC_DATA); //не возвращает статус

		sleep(2);

		send_cmd_only(CONNECT);

		//exec pdl2
		printf("uploading pdl2\n");
		if (upload_file(PDL2_PATH, PDL2_ADDR)) {
			printf("upload pdl2 failed\n");
			return -1;
		}
		send_cmd_only(EXEC_DATA); //не возвращает статус

		sleep(2);
	}

	char *ver = NULL;
	get_pdl_version(&ver);
	printf("[%s]\n", ver);
	if (ver)
		free(ver);

	set_pdl_dbg(
		PDL_DBG_PDL |
		//PDL_DBG_USB_EP0 |
		//PDL_DBG_USB_SERIAL |
		PDL_DBG_RW_CHECK |
		PDL_DBG_FACTORY_PART |
		PDL_DBG_PDL_VERBOSE |
		PDL_EXTENDED_STATUS
	);

	if (read_partition_table()) {
		close_tty();
		printf("can't read partition table\n");
		return -1;
	}


	read_partition("bootloader", "bootloader.bin");

	close_tty();
	return 0;
}
