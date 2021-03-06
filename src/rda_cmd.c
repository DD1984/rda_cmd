#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <fcntl.h>

#include "tty.h"

#include "types.h"
#include "defs.h"
#include "dump.h"

#include "protocol.h"
#include "cmd_defs.h"
#include "mtdparts_parser.h"
#include "crc32.h"
#include "fullfw.h"
#include "file_mmap.h"
#include "pdls.h"
#include "prog_dir.h"

//#define TRUNC_READ //обрезать крайние ff в считанном файле

#define UPLOAD_CHUNK_SIZE_PDL (4 * 1024)

#define UPLOAD_CHUNK_SIZE (256 * 1024)

unsigned int calc_persent(int cur, int total)
{
	float f_total = total;
	float f_cur = cur;
	float f_per = f_cur / (f_total / 100);
	if (f_per > 100)
		f_per = 100;
	return f_per;
}

int upload_buf(buf_t *buf, char *part_name, u32 data_addr, u32 chunk_size)
{
	int ret;

	if (!buf)
		return -1;

	printf("uploading %s, size: %u bytes - %3u%% ", part_name ? part_name : "", buf->size, 0);
	fflush(stdout);

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

		printf("\b\b\b\b\b%3u%% ", calc_persent(total_send, buf->size));
		fflush(stdout);
	}

	u32 crc = htole32(crc32(0, buf->data, buf->size));

	to_dev.data = (char *)&crc;
	to_dev.size = sizeof(crc);

	ret = send_cmd_data_to_dev(END_DATA, &to_dev);
	if (ret)
		return -1;

	printf("\b\b\b\b\b100%% DONE\n");
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

int read_partition_table(char **parts)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(READ_PARTITION_TABLE, &from_dev);
	if (!ret) {
		from_dev.data[from_dev.size] = 0;
		*parts = malloc(from_dev.size + 1);
		memcpy(*parts, from_dev.data, from_dev.size + 1);
	}

	return ret;
}

// проверяет, совпадает ли таблица разделов в pdl и текущая записанная на флешке
// если совпадают возвращается 1
int check_partition_table(void)
{
	u8 buffer[PDL_MAX_DATA_SIZE];
	buf_t from_dev;

	from_dev.data = buffer;
	from_dev.size = sizeof(buffer);

	int ret = send_cmd_data_from_dev(CHECK_PARTITION_TABLE, &from_dev);
	if (!ret) {
		ret = from_dev.data[0];
		return ret;
	}

	return 0;
}

#define DOWNLOAD_CHUNK_SIZE (256 * 1024) //кратно nand page size 4kb
#define FACTORYDATA_SIZE (32 * 1024)

int read_partition(char *name, char *out_file)
{
	char *buf;
	u32 buf_size;
	size_t part_size;

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

	printf("downloading %s, size: %zu bytes - %3u%% ", name, part_size, 0);
	fflush(stdout);

	buf = malloc(buf_size);
	if (!buf) {
		printf("%s[%d] can't allocate memory\n", __func__, __LINE__);
		return -1;
	}

	size_t total_rcv = 0;
	size_t last_ff = 0;

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

#ifdef TRUNC_READ
		int i;
		for (i = (from_dev.size - 1); i >= 0; i--)
			if ((unsigned char)buf[i] != 0xff) {
				last_ff = total_rcv + i;
				break;
			}
#endif
		if (from_dev.size)
			write(fd, buf, from_dev.size);

		total_rcv += buf_size;

		printf("\b\b\b\b\b%3u%% ", calc_persent(total_rcv, part_size));
		fflush(stdout);
	}

	printf("\b\b\b\b\b100%% DONE\n");

#ifdef TRUNC_READ
	ftruncate(fd, last_ff + 1);
#endif

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

enum reboot_type {
	REBOOT_TO_NORMAL_MODE,
	REBOOT_TO_DOWNLOAD_MODE,
	REBOOT_TO_FASTBOOT_MODE,
	REBOOT_TO_RECOVERY_MODE,
	REBOOT_TO_CALIB_MODE,
	REBOOT_TO_PDL2_MODE,
};

int rda_reboot(enum reboot_type type)
{
	buf_t to_dev;

	u8 mode = type;

	to_dev.data = &mode;
	to_dev.size = 1;

	return send_cmd_data_to_dev(NORMAL_RESET, &to_dev);
}

typedef struct {
	mmap_file_t *file;
	u32 addr;
	buf_t buf;
} pdl_t;

pdl_t pdl1, pdl2;

int load_pdls(mmap_file_t *file)
{
	memset(&pdl1, 0, sizeof(pdl_t));
	memset(&pdl2, 0, sizeof(pdl_t));

	printf("pdl1 mem addr: 0x%zx\n", (size_t)&pdl1);
	printf("pdl2 mem addr: 0x%zx\n", (size_t)&pdl2);

	if (file) {
		parts_hdr_t *parts_hdr = (parts_hdr_t *)file->buf.data;
		part_info_t *part_info = fullfw_find_part(parts_hdr, "pdl1");
		if (part_info) {
			pdl1.addr = part_info->loadaddr;
			pdl1.buf.data = PARTS_DATA_BASE(parts_hdr) + part_info->offset;
			pdl1.buf.size = part_info->size;

			printf("pdl1 from fullfw\n");
		}
		part_info = fullfw_find_part(parts_hdr, "pdl2");
		if (part_info) {
			pdl2.addr = part_info->loadaddr;
			pdl2.buf.data = PARTS_DATA_BASE(parts_hdr) + part_info->offset;
			pdl2.buf.size = part_info->size;

			printf("pdl2 from fullfw\n");
		}
	}

	char path[1024];
	int cnt;

	//???? проверка, что файлы открылись

	if (pdl1.buf.data == 0) {
		pdl1.addr = PDL1_ADDR;

		cnt = 0;
		cnt += sprintf(path + cnt, "%s", get_prog_dir());
		cnt += sprintf(path + cnt, "%s", PDL1_PATH);
		pdl1.file = load_file(path);

		memcpy(&pdl1.buf, &pdl1.file->buf, sizeof(buf_t));

		printf("pdl1 is local\n");
	}

	if (pdl2.buf.data == 0) {
		pdl2.addr = PDL2_ADDR;

		cnt = 0;
		cnt += sprintf(path + cnt, "%s", get_prog_dir());
		cnt += sprintf(path + cnt, "%s", PDL2_PATH);
		pdl2.file = load_file(path);

		memcpy(&pdl2.buf, &pdl2.file->buf, sizeof(buf_t));

		printf("pdl2 is local\n");
	}

	return 0;
}

void close_pdls(void)
{
	close_file(pdl1.file);
	close_file(pdl2.file);

	memset(&pdl1, 0, sizeof(pdl_t));
	memset(&pdl2, 0, sizeof(pdl_t));
}

int exec_pdl(pdl_t *pdl)
{
	//в загрузчике не определяется какой именно pdl загружаем, важен лишь его адрес
	if (upload_buf(&pdl->buf, "pdl", pdl->addr, UPLOAD_CHUNK_SIZE_PDL)) {
		printf("upload pdl (mem addr: 0x%zx) failed\n", (size_t)pdl);
		return -1;
	}
	send_cmd_only(EXEC_DATA); //не возвращает статус

	sleep(2); //ожидание выполнения

	tty_flush();

	return send_cmd_only(CONNECT);
}

void show_help(void)
{
	printf("\tparts                               - read partition table\n");
	printf("\tver                                 - read PDL version\n");
	printf("\tread [partition name] [output file] - read partition to file\n");
	printf("\terase [partition name]              - erase partition\n");
	printf("\twrite [partition name] [input file] - write partition from file\n");
	printf("\treset                               - reboot machine\n");
	printf("\tfullfw [input file]                 - write all partitions from original fw\n");
}

typedef enum {
	DO_NOTHING = 0,
	GET_PARTS = 1,
	GET_VER,
	READ_PART,
	ERASE_PART,
	WRITE_PART,
	RESET,
	FULLFW,
} user_cmd_t;

int main(int argc, char *argv[])
{
	user_cmd_t user_cmd;
	char *buf_ptr;
	char *part_name = NULL;
	char *file_name = NULL;
	mmap_file_t *file = NULL;
	parts_hdr_t *parts_hdr;
	part_info_t *part_info;

	if (argc == 2) {
		if (!strcmp(argv[1], "parts")) {
			user_cmd = GET_PARTS;
		}
		else if (!strcmp(argv[1], "ver")) {
			user_cmd = GET_VER;
		}
		else if (!strcmp(argv[1], "reset")) {
			user_cmd = RESET;
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
		else if (!strcmp(argv[1], "fullfw")) {
			user_cmd = FULLFW;
			file_name = argv[2];
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

	if (send_cmd_only(CONNECT)) {
		close_tty();
		printf("can't connect to device\n");
		return -1;
	}

	if ((user_cmd == WRITE_PART || user_cmd == FULLFW) && file_name) {
		file = load_file(file_name);

		if (user_cmd == FULLFW) {
			parts_hdr = (parts_hdr_t *)file->buf.data;
			if (check_img(parts_hdr, file->buf.size))
				goto err;
		}
	}

	if (user_cmd == FULLFW)
		load_pdls(file);
	else
		load_pdls(NULL);

	set_tty_timeout(1); // т.к перввый вызов get_pdl_version() не возвращает результаты, т. к. в BootRom нет такой команды

	int ret = get_pdl_version(NULL);

	set_tty_timeout(TTY_WAIT_RX_TIMEOUT);

	if (ret) {
		exec_pdl(&pdl1);
		exec_pdl(&pdl2);
	}

	//TODO: нужно добавить установку дебага в exec_pdl() иначе после reload_pdl2 не работает
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
			if (read_partition(part_name, file_name))
				printf("failed\n");
		break;
		case ERASE_PART:
			printf("erase [%s] partition ", part_name);
			if (!erase_partition(part_name))
				printf("DONE\n");
			else
				printf("failed\n");
		break;
		case WRITE_PART:
			upload_buf(&(file->buf), part_name, 0, UPLOAD_CHUNK_SIZE);
		break;
		case RESET:
			rda_reboot(REBOOT_TO_NORMAL_MODE);
		break;
		case FULLFW:
			//сначала загрузчик, т.к на его разделе содержатся mtdpatrs и сохраняются туда вместе с ним
			part_info = fullfw_find_part(parts_hdr, "bootloader");
			if (part_info) {
				int need_reload_pdl2 = !check_partition_table();

				buf_t buf;
				buf.data = PARTS_DATA_BASE(parts_hdr) + part_info->offset;
				buf.size = part_info->size;

				upload_buf(&buf, part_info->part, 0, UPLOAD_CHUNK_SIZE);

				//чтобы перезагрузить mtdparts уже из текущего pdl
				//т.к. при перешивке загрузчика они записались на флеш, но не подгрузились
				if (need_reload_pdl2) {
					printf("reloading pdl2 for reread mtdparts\n");
					exec_pdl(&pdl2);
				}
			}
			//потом все остальное
			part_foreach(parts_hdr, part_info) {
				if (strncmp(part_info->part, "pdl", 3) == 0)
					continue;
				if (strcmp(part_info->part, "bootloader") == 0)
					continue;

				buf_t buf;
				buf.data = PARTS_DATA_BASE(parts_hdr) + part_info->offset;
				buf.size = part_info->size;

				upload_buf(&buf, part_info->part, 0, UPLOAD_CHUNK_SIZE);
			}

			printf("All done - reseting device ...\n");
			rda_reboot(REBOOT_TO_NORMAL_MODE);
		break;
		default:
			printf("unknown user cmd: %d\n", user_cmd);
	}

err:
	close_pdls();

	close_file(file);

	close_tty();
	return 0;
}
