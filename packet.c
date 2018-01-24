#include <string.h>
#include "types.h"
#include "defs.h"
#include "tty.h"
#include "packet.h"


static unsigned char cmd_buf[PDL_MAX_PKT_SIZE] __attribute__((__aligned__(32)));

static int pdl_send_data(const u8 *data, u32 size, u8 flowid)
{
	struct packet_header *header = (struct packet_header *)cmd_buf;
	int hdr_sz = sizeof(struct packet_header);

	if(size > PDL_MAX_PKT_SIZE - hdr_sz) {
		pdl_error("packet size is too large.\n");
		return -1;
	}

	memset(cmd_buf, PDL_MAX_PKT_SIZE, 0);
	memcpy(&cmd_buf[hdr_sz], data, size);

	header->tag = HOST_PACKET_TAG;
	header->flowid = flowid;
	/* carefully, header->pkt_size may be unaligned */
	put_unaligned(size, &header->pkt_size); /* header->pkt_size = size; */

	write_tty(cmd_buf, hdr_sz + size);
	return 0;
}

int pdl_send_pkt(const u8 *data, u32 size)
{
	return pdl_send_data(data, size, HOST_PACKET_FLOWID);
}
