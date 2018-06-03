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
	printf("cmd: %s(%d)\n", str_cmd(pkt->pdl_pkt->cmd_header.cmd_type), pkt->pdl_pkt->cmd_header.cmd_type);
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

#define MAX_CNT_RETRY 3

int send_cmd(struct command_header *cmd_hdr, buf_t *to_dev, buf_t *from_dev)
{
	char rcv_buf[PDL_MAX_DATA_SIZE];
	int retry_cnt = 0;

retry:
	if (retry_cnt != 0)
		printf("retry #%d - cmd: %s(%d)\n", retry_cnt, str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type);

	if (retry_cnt++ >= MAX_CNT_RETRY) {
		printf("max retry count exceed\n");
		return -1;
	}

	u8 *data_buf = NULL;
	u32 data_size = 0;
	if (to_dev && (to_dev->size > 0)) {
		data_buf = to_dev->data;
		data_size = to_dev->size;
	}

	struct pdl_packet *pdl_pkt = make_pdl_pkt(cmd_hdr, data_buf);
	struct packet *pkt = make_pkt(HOST_PACKET_TAG, HOST_PACKET_FLOWID, pdl_pkt, data_size);

	send_pkt(pkt);

	free_pkt_mem(pkt);

	memset(rcv_buf, 0, sizeof(rcv_buf));

	int len = read_tty(rcv_buf, sizeof(struct packet_header));

	if (len < 0) {
		printf("rx pkt hdr failed\n");
		goto retry;
	}

	struct packet_header *pkt_hdr = (struct packet_header *)rcv_buf;

	if (pkt_hdr->tag != PACKET_TAG) {
		printf("rcvd pkt_hdr with error pkt tag\n");
		goto retry;
	}

	u32 pkt_size = le32toh(pkt_hdr->pkt_size);

	if (pkt_size > (PDL_MAX_DATA_SIZE - sizeof(struct packet_header))) {
		printf("rcvd pkt_hdr with error pkt_size: %u\n", pkt_size);
		goto retry;
	}

	if (pkt_size != 0)
		len = read_tty(rcv_buf + sizeof(struct packet_header), pkt_size);
	else
		len = 0;

	if (len < 0) {
		printf("rx pkt data failed\n");
		goto retry;
	}

	u32 flowid = pkt_hdr->flowid;
	u32 rsp = le32toh(*(int *)(rcv_buf + sizeof(struct packet_header)));

	// может возвращаться короткий ответ: "ae 04 00 00", что равно ACK
	if (flowid == FLOWID_ACK && rsp == ACK)
		return 0;

	if ((flowid == FLOWID_ERROR || flowid == FLOWID_ACK) && (rsp != ACK)) {
		printf("cmd: %s(%d), %s, response: %s(%d)\n",
			str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type,
			flowid == FLOWID_ERROR ? "FLOWID_ERROR" : "FLOWID_ACK",
			str_rsp(rsp), rsp);
		goto retry;
	}
	if (pkt_hdr->flowid == FLOWID_DATA) {
		if (!from_dev || (pkt_size > from_dev->size)) {
			printf("cmd: %s(%d), small size of outbuf: data len: %d, buffer_size: %d\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type, pkt_size, from_dev->size);
			goto retry;
		}

		from_dev->size = pkt_size;

		memcpy(from_dev->data, rcv_buf + sizeof(struct packet_header), from_dev->size);

		return 0;
	}
	printf("cmd: %s(%d), unknown response\n", str_cmd(cmd_hdr->cmd_type), cmd_hdr->cmd_type);
	goto retry;
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
