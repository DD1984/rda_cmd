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
#include "crc32.h"

#define PDL1_PATH "pdl1.bin"
#define PDL2_PATH "pdl2.bin"

#define PDL1_ADDR 0x00100100 //spl-uboot start addr
#define PDL2_ADDR 0x80008000 

#define UPLOAD_CHUNK_SIZE_PDL1 (4 * 1024)
#define UPLOAD_CHUNK_SIZE_PDL2 (256 * 1024)

int upload_buf(buf_t *buf, char *part_name, u32 data_addr, u32 chunk_size)
{
	int ret;

	if (!buf)
		return -1;

	struct command_header cmd_hdr;
	buf_t to_dev;

	cmd_hdr.cmd_type = START_DATA;
	cmd_hdr.data_addr = data_addr;
	cmd_hdr.data_size = buf->size;

	memset(&to_dev, 0, sizeof(buf_t));

	if (part_name) {
		to_dev.data = part_name;
		to_dev.size = strlen(part_name) + 1;
	}

	ret = send_cmd(&cmd_hdr, &to_dev, NULL);

	if (ret)
		return -1;

	u32 total_send = 0;
	u32 frame_count = 0;
	while (total_send < buf->size) {

		buf_t chunk_buf;
		chunk_buf.data = buf->data + total_send;
		chunk_buf.size = chunk_size;
		if ((total_send + chunk_size) > buf->size)
			chunk_buf.size = buf->size - total_send;

		cmd_hdr.cmd_type = MID_DATA;
		cmd_hdr.data_addr = frame_count++;
		cmd_hdr.data_size = chunk_buf.size;

		ret = send_cmd(&cmd_hdr, &chunk_buf, NULL);
		if (ret)
			return -1;

		total_send += chunk_buf.size;
	}

	u32 crc = htole32(crc32(0, buf->data, buf->size));

	to_dev.data = (char *)&crc;
	to_dev.size = sizeof(crc);

	ret = send_cmd_data_to_dev(END_DATA, &to_dev);
	if (ret)
		return -1;

	return 0;
}

int upload_file(char *path, char *part_name, u32 data_addr, u32 chunk_size)
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

	int ret = upload_buf(&buf, part_name, data_addr, chunk_size);
	if (ret)
		ret = -1;

	munmap(buf.data, stat_buf.st_size);
	close(fd);

	return ret;
}

int read_partition_table(char **parts)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(READ_PARTITION_TABLE, &from_dev);
	if (!ret) {
		from_dev.data[from_dev.size + 1] = 0;
		*parts = malloc(from_dev.size + 1);
		memcpy(*parts, from_dev.data, from_dev.size + 1);
	}

	return ret;
}

#define DOWNLOAD_CHUNK_SIZE (256 * 1024) //кратно nand page size 4kb
#define FACTORYDATA_SIZE (32 * 1024)

int read_partition(char *name, char *out_file)
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
		char *parts = NULL;

		if (read_partition_table(&parts)) {
			printf("[%s] read partition table failed\n", __func__);
			return -1;
		}

		if (parse_mtdparts(parts)) {
			printf("[%s] parse partition table failed\n", __func__);
			free(parts);
			return -1;
		}

		part_size = get_part_size(name);

		free(parts);
		clear_parse_result();

		if (part_size == 0) {
			printf("[%s] get partition [%s] size failed\n", __func__, name);
			return -1;
		}
	}

	buf = malloc(buf_size);

	u64 total_rcv = 0;

	int fd = open(out_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

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
			printf("[%s] download error\n", __func__);
			return -1;
		}
		write(fd, buf, buf_size);
		total_rcv += buf_size;
	}

	free(buf);
	close(fd);

	return 0;
}

int erase_partition(char *name)
{
	buf_t to_dev;

	to_dev.data = name;
	to_dev.size = strlen(name) + 1;

	return send_cmd_data_to_dev(ERASE_PARTITION, &to_dev);
}

int write_partition(char *name, char *in_file)
{
	return upload_file(in_file, name, 0, UPLOAD_CHUNK_SIZE_PDL2);
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

void show_help(void)
{
	printf("\tget_parts                           - read partition table\n");
	printf("\tget_ver                             - read PDL version\n");
	printf("\tread [partition name] [output file] - read partition to file\n");
	printf("\terase [partition name]              - erase partition\n");
	printf("\twrite [partition name] [input file] - write partition from file\n");
}

typedef enum {
	GET_PARTS,
	GET_VER,
	READ_PART,
	ERASE_PART,
	WRITE_PART,
} user_cmd_t;

int main(int argc, char *argv[])
{
	user_cmd_t user_cmd;
	char *buf_ptr;
	char *part_name = NULL;
	char *file_name = NULL;

	if (argc == 2) {
		if (!strcmp(argv[1], "get_parts")) {
			user_cmd = GET_PARTS;
		}
		else if (!strcmp(argv[1], "get_ver")) {
			user_cmd = GET_VER;
		}
		else {
			show_help();
			return 0;
		}
	}
	else if (argc == 3) {
		part_name = argv[2];
		if (!strcmp(argv[1], "erase")) {
			user_cmd = ERASE_PART;
		}
		else {
			show_help();
			return 0;
		}
	}
	else if (argc == 4) {
		part_name = argv[2];
		file_name = argv[3];
		if (!strcmp(argv[1], "read")) {
			user_cmd = READ_PART;
		}
		else if (!strcmp(argv[1], "write")) {
			user_cmd = WRITE_PART;
		}
		else {
			show_help();
			return 0;
		}
	}
	else {
		show_help();
		return 0;
	}

	if (open_tty() != 0)
		return -1;

	int tty_opened = 1;

	if (send_cmd_only(CONNECT)) {
		close_tty();
		printf("can't connect to device\n");
		return -1;
	}

	int tty_timeout = get_tty_timeout();
	if (tty_timeout != -1)
		set_tty_timeout(1); // т.к перввый вызов get_pdl_version() может не вернуть результаов так как в BootRom нет такой команды

	int ret = get_pdl_version(NULL);

	if (tty_timeout != -1)
		set_tty_timeout(tty_timeout); //возвращаем обратно


	if (ret) {
		//exec pdl1
		printf("uploading pdl1\n");
		if (upload_file(PDL1_PATH, NULL, PDL1_ADDR, UPLOAD_CHUNK_SIZE_PDL1)) {
			printf("upload pdl1 failed\n");
			return -1;
		}
		send_cmd_only(EXEC_DATA); //не возвращает статус

		sleep(2);

		send_cmd_only(CONNECT);

		//exec pdl2
		printf("uploading pdl2\n");
		if (upload_file(PDL2_PATH, NULL, PDL2_ADDR, UPLOAD_CHUNK_SIZE_PDL1)) {
			printf("upload pdl2 failed\n");
			return -1;
		}
		send_cmd_only(EXEC_DATA); //не возвращает статус

		sleep(2);

		close_tty(); //костыль для сброса буферов, нужно разобраться, т.к. и pdl1 и pdl2 после запуска отправляют ACK и можно убрать sleep()
		tty_opened  = 0;
	}

	if (!tty_opened) {
		open_tty();
		send_cmd_only(CONNECT);
	}

	set_pdl_dbg(
		PDL_DBG_PDL |
		//PDL_DBG_USB_EP0 |
		//PDL_DBG_USB_SERIAL |
		PDL_DBG_RW_CHECK |
		PDL_DBG_FACTORY_PART |
		PDL_DBG_PDL_VERBOSE
		//| PDL_EXTENDED_STATUS // ??? дополнительный ответ при прошивке если размер файла больше 24мб
	);

	switch (user_cmd) {
		case GET_PARTS:
			buf_ptr = NULL;
			if (!read_partition_table(&buf_ptr))
				printf("[%s]\n", buf_ptr);
			else
				printf("get partition table failed\n");
			if (buf_ptr)
				free(buf_ptr);
		break;
		case GET_VER:
			buf_ptr = NULL;
			if (!get_pdl_version(&buf_ptr))
				printf("[%s]\n", buf_ptr);
			else
				printf("get version failed\n");
			if (buf_ptr)
				free(buf_ptr);
		break;
		case READ_PART:
			printf("read [%s] partition ", part_name);
			if (!read_partition(part_name, file_name))
				printf("DONE\n");
			else
				printf("failed\n", part_name);
		break;
		case ERASE_PART:
			printf("erase [%s] partition ", part_name);
			if (!erase_partition(part_name))
				printf("DONE\n");
			else
				printf("failed\n", part_name);
		break;
		case WRITE_PART:
			printf("write [%s] partition ", part_name);
			if (!write_partition(part_name, file_name))
				printf("DONE\n");
			else
				printf("failed\n", part_name);
		break;
		default:
			printf("unknown user cmd: %d\n", user_cmd);
	}

	close_tty();
	return 0;
}
