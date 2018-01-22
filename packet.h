#ifndef __PACKET_H__
#define __PACKET_H__

struct packet_header {
	u8 tag;
	u32 pkt_size;
	u8 flowid;
}__attribute__((packed));

struct command_header {
	u32 cmd_type;
	u32 data_addr;
	u32 data_size;
};

struct pdl_packet {
	struct command_header cmd_header;
	u8 *data;
};


struct packet{
	struct packet_header pkt_header;
	struct pdl_packet *pdl_pkt;
	int state;
};

#endif
