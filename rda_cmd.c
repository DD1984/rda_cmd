#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <endian.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "types.h"
#include "tty.h"
#include "dump.h"

#include "packet.h"
#include "cmd_defs.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

void send_pkt(struct packet *pkt)
{
	hex_dump((char *)pkt, sizeof(struct packet_header));
	hex_dump((char *)&(pkt->pdl_pkt->cmd_header), sizeof(struct command_header));
	if (pkt->pdl_pkt->data)
		hex_dump((char *)(pkt->pdl_pkt->data), pkt->pdl_pkt->cmd_header.data_size);
	hex_dump((char *)&(pkt->state), sizeof(pkt->state));

	write_tty((char *)pkt, sizeof(struct packet_header));
	write_tty((char *)&(pkt->pdl_pkt->cmd_header), sizeof(struct command_header));
	if (pkt->pdl_pkt->data)
		write_tty((char *)(pkt->pdl_pkt->data), pkt->pdl_pkt->cmd_header.data_size);
	write_tty((char *)&(pkt->state), sizeof(pkt->state));
}

struct pdl_packet *make_pdl_pkt(struct command_header *cmd_hdr, u8 *data)
{
	struct pdl_packet *pdl_pkt = malloc(sizeof(struct pdl_packet));

	pdl_pkt->cmd_header.cmd_type = htole32(cmd_hdr->cmd_type);

	pdl_pkt->cmd_header.data_addr = htole32(cmd_hdr->data_addr);

	pdl_pkt->cmd_header.data_size = htole32(cmd_hdr->data_size);

	pdl_pkt->data = data;

	return pdl_pkt;
}

struct packet *make_pkt(u8 tag, u8 flowid, struct pdl_packet *pdl_pkt, u32 data_size)
{
	struct packet *pkt = malloc(sizeof(struct packet));

	pkt->pkt_header.tag = tag;
	pkt->pkt_header.pkt_size = htole32(sizeof(struct command_header) + data_size);
	pkt->pkt_header.flowid = flowid;

	pkt->pdl_pkt = pdl_pkt;

	pkt->state = 0;

	return pkt;
}

void free_pkt_mem(struct packet *pkt)
{
	free(pkt->pdl_pkt);
	free(pkt);
}

int send_cmd(struct command_header *cmd_hdr, u8 *data, u32 data_size)
{
	printf("\ncmd: %s(%d)\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type);

	char rcv_buf[PDL_MAX_DATA_SIZE];

	struct pdl_packet *pdl_pkt = make_pdl_pkt(cmd_hdr, data);
	struct packet *pkt = make_pkt(HOST_PACKET_TAG, HOST_PACKET_FLOWID, pdl_pkt, cmd_hdr->data_size);

	send_pkt(pkt);

	free_pkt_mem(pkt);

	memset(rcv_buf, 0, sizeof(rcv_buf));

	printf("\n");

	int len = read_tty(rcv_buf, sizeof(rcv_buf));

	printf("len: %d\n", len);

	if (len <= 0)
		return -1;

	hex_dump(rcv_buf, len);

	struct packet_header *pkt_hdr = (struct packet_header *)rcv_buf;
	
	if (pkt_hdr->flowid == FLOWID_ERROR) {
		printf("flowid error\n");
		return -1; 
	}
	int rsp = le32toh(*(int *)(rcv_buf + sizeof(struct packet_header)));

	printf("response: %s(%d)\n", str_rsp(rsp), rsp);
	return 0;
}

int send_cmd_only(u32 cmd_type)
{
	struct command_header cmd_hdr;

	memset(&cmd_hdr, 0, sizeof(struct command_header));
	cmd_hdr.cmd_type = cmd_type;

	return send_cmd(&cmd_hdr, NULL, 0);
}

int allowed_commands[] = {
	//ERASE_FLASH,
	//ERASE_PARTITION,
	//ERASE_ALL,
	//START_DATA,
	//MID_DATA,
	//END_DATA,
	//EXEC_DATA,
	//READ_FLASH,
	//READ_PARTITION,
	//NORMAL_RESET,
	READ_CHIPID,
	//SET_BAUDRATE,
	//FORMAT_FLASH,
	//READ_PARTITION_TABLE,
	//READ_IMAGE_ATTR,
	GET_VERSION,
	//SET_FACTMODE,
	//SET_CALIBMODE,
	SET_PDL_DBG,
	//CHECK_PARTITION_TABLE,
	//POWER_OFF,
	//IMAGE_LIST,
	//GET_SWCFG_REG,
	//SET_SWCFG_REG,
	//GET_HWCFG_REG,
	//SET_HWCFG_REG,
	//EXIT_AND_RELOAD,
	GET_SECURITY,
	//HW_TEST,
	GET_PDL_LOG,
	//DOWNLOAD_FINISH,
};

#define PDL1_PATH "pdl1.bin"
#define PDL2_PATH "pdl2.bin"

#define PDL1_ADDR 0x00100100 //spl-uboot start addr
#define PDL2_ADDR 0x80008000 

#define CHUNK_SIZE 1024


int upload_buf(u8 *buf, u32 data_size, u32 data_addr)
{
	int ret;
	struct command_header cmd_hdr;

	cmd_hdr.cmd_type = START_DATA;
	cmd_hdr.data_addr = data_addr;
	cmd_hdr.data_size = data_size;

	ret = send_cmd(&cmd_hdr, NULL, 0);
	if (ret)
		return -1;

	u32 total_send = 0;
	while (total_send < data_size) {
		
		u32 chunk = CHUNK_SIZE;
		if ((total_send + CHUNK_SIZE) > data_size)
			chunk = data_size - total_send;

		cmd_hdr.cmd_type = MID_DATA;
		cmd_hdr.data_addr = 0;
		cmd_hdr.data_size = chunk;

		ret = send_cmd(&cmd_hdr, buf + total_send, chunk);
		if (ret)
			return -1;

		total_send += chunk;
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

	char *buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (buf == NULL) {
		close(fd);
		printf("can't mmap file: %s\n", path);
		return -1;
	}

	upload_buf(buf, stat_buf.st_size, data_addr);

	munmap(buf, stat_buf.st_size);

	close(fd);
	
	return 0;
}

int main(void)
{
	if (open_tty() != 0)
		return -1;

	if (send_cmd_only(CONNECT)) {
		close_tty();
		printf("can't connect to device\n");
		return -1;
	}
	

	//exec pdl1
	upload_file(PDL1_PATH, PDL1_ADDR);
	send_cmd_only(EXEC_DATA);

	return 0;
	sleep(3);

	send_cmd_only(CONNECT);

	//exec pdl2
	upload_file(PDL2_PATH, PDL2_ADDR);
	send_cmd_only(EXEC_DATA);


	//send_cmd_only(GET_VERSION);

	close_tty();
	return 0;
}
