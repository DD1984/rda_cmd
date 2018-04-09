#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <string.h>
#include <unistd.h>

#include "protocol.h"
#include "tty.h"
#include "cmd_defs.h"

#include "dump.h"

void send_pkt(struct packet *pkt)
{
#if 0
	hex_dump((char *)pkt, sizeof(struct packet_header));
	hex_dump((char *)&(pkt->pdl_pkt->cmd_header), sizeof(struct command_header));
	if (pkt->pdl_pkt->data)
		hex_dump((char *)(pkt->pdl_pkt->data), le32toh(pkt->pkt_header.pkt_size) - sizeof(struct command_header));
	hex_dump((char *)&(pkt->state), sizeof(pkt->state));
#endif

	write_tty((char *)pkt, sizeof(struct packet_header));
	write_tty((char *)&(pkt->pdl_pkt->cmd_header), sizeof(struct command_header));
	if (pkt->pdl_pkt->data)
		write_tty((char *)(pkt->pdl_pkt->data), le32toh(pkt->pkt_header.pkt_size) - sizeof(struct command_header));
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

int send_cmd(struct command_header *cmd_hdr, buf_t *to_dev, buf_t *from_dev)
{
	char rcv_buf[PDL_MAX_DATA_SIZE];

	u8 *data_buf = NULL;
	u32 data_size = 0;
	if (to_dev) {
		data_buf = to_dev->data;
		data_size = to_dev->size;
	}

	struct pdl_packet *pdl_pkt = make_pdl_pkt(cmd_hdr, data_buf);
	struct packet *pkt = make_pkt(HOST_PACKET_TAG, HOST_PACKET_FLOWID, pdl_pkt, data_size);

	send_pkt(pkt);

	free_pkt_mem(pkt);

	memset(rcv_buf, 0, sizeof(rcv_buf));

	usleep(10000);

	int len = read_tty(rcv_buf, sizeof(rcv_buf));

	if (len <= 0) {
		printf("cmd: %s(%d), data rcvd error, len: %d\n",  str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type, len);
		return -1;
	}

	struct packet_header *pkt_hdr = (struct packet_header *)rcv_buf;
	
	if (pkt_hdr->flowid == FLOWID_ERROR) {
		printf("cmd: %s(%d), flowid error\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type);
		return -1; 
	}
	if (pkt_hdr->flowid == FLOWID_ACK) {
		int rsp = le32toh(*(int *)(rcv_buf + sizeof(struct packet_header)));
		if (rsp != ACK) {
				printf("cmd: %s(%d), response error: %s(%d)\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type, str_rsp(rsp), rsp);
				return -1;
		}
		return 0;
	}
	if (pkt_hdr->flowid == FLOWID_DATA) {
		if (!from_dev || (le32toh(pkt_hdr->pkt_size) > from_dev->size)) {
			printf("cmd: %s(%d), small size of outbuf: data len: %d, buffer_size: %d\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type, le32toh(pkt_hdr->pkt_size), from_dev->size);
			return -1;
		}

		from_dev->size = le32toh(pkt_hdr->pkt_size);
		memcpy(from_dev->data, rcv_buf + sizeof(struct packet_header), from_dev->size);

		return 0;
	}
	printf("cmd: %s(%d), unknown response\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type);
	return -1;
}

// команда без данных
int send_cmd_only(u32 cmd)
{
	struct command_header cmd_hdr;

	memset(&cmd_hdr, 0, sizeof(struct command_header));
	cmd_hdr.cmd_type = cmd;

	return send_cmd(&cmd_hdr, NULL, NULL);
}

// команда с заполненным заголовком
int send_cmd_hdr(u32 cmd, u32 data_addr, u32 data_size)
{
	struct command_header cmd_hdr;

	cmd_hdr.cmd_type = cmd;
	cmd_hdr.data_addr = data_addr;
	cmd_hdr.data_size = data_size;

	return send_cmd(&cmd_hdr, NULL, NULL);
}

int send_cmd_data_to_dev(u32 cmd, buf_t *to_dev)
{
	struct command_header cmd_hdr;

	cmd_hdr.cmd_type = cmd;
	cmd_hdr.data_addr = 0;
	cmd_hdr.data_size = to_dev->size;

	return send_cmd(&cmd_hdr, to_dev, NULL);
}

int send_cmd_data_from_dev(u32 cmd, buf_t *from_dev)
{
	struct command_header cmd_hdr;

	memset(&cmd_hdr, 0, sizeof(struct command_header));
	cmd_hdr.cmd_type = cmd;

	int ret = send_cmd(&cmd_hdr, NULL, from_dev);
}
