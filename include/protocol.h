#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include "types.h"
#include "defs.h"
#include "packet.h"

int send_cmd(struct command_header *cmd_hdr, buf_t *to_dev, buf_t *from_dev);
int send_cmd_only(u32 cmd); // команда без данных
// команда с заполненным заголовком
int send_cmd_hdr(u32 cmd, u32 data_addr, u32 data_size);
int send_cmd_data_to_dev(u32 cmd, buf_t *to_dev);
int send_cmd_data_from_dev(u32 cmd, buf_t *from_dev);

#endif
