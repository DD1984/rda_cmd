#ifndef __MTDPARTS_PARSER_H__
#define __MTDPARTS_PARSER_H__

#include "types.h"

int parse_mtdparts(const char *const mtdparts);
void print_parts(void);
u64 get_part_size(char *name);
void clear_parse_result(void);

#endif
