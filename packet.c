#include <string.h>
#include "types.h"
#include "defs.h"
#include "tty.h"
#include "packet.h"
#include "cmd_defs.h"


static unsigned char cmd_buf[PDL_MAX_PKT_SIZE] __attribute__((__aligned__(32)));

static const char *pdl_commands[MAX_CMD] = {
	[CONNECT] = "connect",
	[ERASE_FLASH] = "erase flash",
	[ERASE_PARTITION] = "erase partition",
	[ERASE_ALL] = "erase all",
	[START_DATA] = "start data",
	[MID_DATA] = "midst data",
	[END_DATA] = "end data",
	[EXEC_DATA] = "execute data",
	[READ_FLASH] = "read flash",
	[READ_PARTITION] = "read partition",
	[NORMAL_RESET] = "normal reset",
	[READ_CHIPID] = "read chipid",
	[SET_BAUDRATE] = "set baudrate",
	[FORMAT_FLASH] = "format flash",
	[READ_PARTITION_TABLE] = "read partition table",
	[READ_IMAGE_ATTR] = "read image attribute",
	[GET_VERSION] = "get version",
	[SET_FACTMODE] = "set factory mode",
	[SET_CALIBMODE] = "set calibration mode",
	[SET_PDL_DBG] = "set pdl debuglevel",
	[CHECK_PARTITION_TABLE] = "check partition table",
	[POWER_OFF] = "power off",
	[IMAGE_LIST] = "download image list",
	[GET_SECURITY] = "get security capabilities",
	[HW_TEST] = "hardware test",
	[GET_PDL_LOG] = "get pdl log",
	[DOWNLOAD_FINISH] = "download finish",
};

const static char *pdl_device_rsp[] = {
	[ACK] =	"device ack",
	[PACKET_ERROR] = "packet error",
	[INVALID_CMD] = "invalid cmd",
	[UNKNOWN_CMD] = "unknown cmd",
	[INVALID_ADDR] = "invalid address",
	[INVALID_BAUDRATE] = "invalid baudrate",
	[INVALID_PARTITION] = "invalid partition",
	[INVALID_SIZE] = "invalid size",
	[WAIT_TIMEOUT] = "wait timeout",
	[VERIFY_ERROR] = "verify error",
	[CHECKSUM_ERROR] = "checksum error",
	[OPERATION_FAILED] = "operation failed",
	[DEVICE_ERROR] = "device error",
	[NO_MEMORY] = "no memory",
	[DEVICE_INCOMPATIBLE] = "device incompatible",
	[HW_TEST_ERROR] = "hardware test error",
	[MD5_ERROR] = "md5 error",
	[ACK_AGAIN_ERASE] = "ack again erase",
	[ACK_AGAIN_FLASH] = "ack again flash",
	[MAX_RSP] = "max response",
};

const char *str_rsp(int rsp)
{
	if (rsp < ACK || rsp > MAX_RSP)
		return "unknown";
	return pdl_device_rsp[rsp];
}

const char *str_cmd(int cmd)
{
	if (cmd < MIN_CMD || cmd >= MAX_CMD || !pdl_commands[cmd])
		return "unknown";

	return pdl_commands[cmd];
}


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
