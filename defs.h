#ifndef __DEFS_H__
#define __DEFS_H__

#include <stdio.h>

#include "types.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef struct {
	u8 *data;
	u32 size;
} buf_t;

#endif
