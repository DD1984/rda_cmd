#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "mtdparts_parser.h"
#include "list.h"

#define SIZE_REMAINING		(u64)(-1)
#define OFFSET_NOT_SPECIFIED	(u64)(-1)

#define MIN_PART_SIZE		4096

#define MTD_WRITEABLE_CMD		1

//#define debug printf
#define debug {}


struct part_info {
	struct list_head link;
	char *name;			/* partition name */
	u8 auto_name;			/* set to 1 for generated name */
	u64 size;			/* total size of the partition */
	u64 offset;			/* offset within device */
	u32 mask_flags;			/* kernel MTD mask flags */
};

LIST_HEAD(parts);

/**
 * Delete all partitions from parts head list, free memory.
 *
 * @param head list of partitions to delete
 */
static void part_delall(struct list_head *head)
{
	struct list_head *entry, *n;
	struct part_info *part_tmp;

	/* clean tmp_list and free allocated memory */
	list_for_each_safe(entry, n, head) {
		part_tmp = list_entry(entry, struct part_info, link);

		list_del(entry);
		free(part_tmp);
	}
}

/**
 * Parses a string into a number.  The number stored at ptr is
 * potentially suffixed with K (for kilobytes, or 1024 bytes),
 * M (for megabytes, or 1048576 bytes), or G (for gigabytes, or
 * 1073741824).  If the number is suffixed with K, M, or G, then
 * the return value is the number multiplied by one kilobyte, one
 * megabyte, or one gigabyte, respectively.
 *
 * @param ptr where parse begins
 * @param retptr output pointer to next char after parse completes (output)
 * @return resulting unsigned int
 */
static unsigned long memsize_parse (const char *const ptr, const char **retptr)
{
	unsigned long ret = strtoul(ptr, (char **)retptr, 0);

	switch (**retptr) {
		case 'G':
		case 'g':
			ret <<= 10;
		case 'M':
		case 'm':
			ret <<= 10;
		case 'K':
		case 'k':
			ret <<= 10;
			(*retptr)++;
		default:
			break;
	}

	return ret;
}

/**
 * Parse one partition definition, allocate memory and return pointer to this
 * location in retpart.
 *
 * @param partdef pointer to the partition definition string i.e. <part-def>
 * @param ret output pointer to next char after parse completes (output)
 * @param retpart pointer to the allocated partition (output)
 * @return 0 on success, 1 otherwise
 */
static int part_parse(const char *const partdef, const char **ret, struct part_info **retpart)
{
	struct part_info *part;
	u64 size, offset;
	const char *name;
	int name_len;
	unsigned int mask_flags;
	const char *p;

	p = partdef;
	*retpart = NULL;
	*ret = NULL;

	/* fetch the partition size */
	if (*p == '-') {
		/* assign all remaining space to this partition */
		debug("'-': remaining size assigned\n");
		size = SIZE_REMAINING;
		p++;
	} else {
		size = memsize_parse(p, &p);
		if (size < MIN_PART_SIZE) {
			printf("partition size too small (%lx)\n", size);
			return 1;
		}
	}

	/* check for offset */
	offset = OFFSET_NOT_SPECIFIED;
	if (*p == '@') {
		p++;
		offset = memsize_parse(p, &p);
	}

	/* now look for the name */
	if (*p == '(') {
		name = ++p;
		if ((p = strchr(name, ')')) == NULL) {
			printf("no closing ) found in partition name\n");
			return 1;
		}
		name_len = p - name + 1;
		if ((name_len - 1) == 0) {
			printf("empty partition name\n");
			return 1;
		}
		p++;
	} else {
		/* 0x00000000@0x00000000 */
		name_len = 22;
		name = NULL;
	}

	/* test for options */
	mask_flags = 0;
	if (strncmp(p, "ro", 2) == 0) {
		mask_flags |= MTD_WRITEABLE_CMD;
		p += 2;
	}

	/* check for next partition definition */
	if (*p == ',') {
		if (size == SIZE_REMAINING) {
			*ret = NULL;
			printf("no partitions allowed after a fill-up partition\n");
			return 1;
		}
		*ret = ++p;
	} else if ((*p == ';') || (*p == '\0')) {
		*ret = p;
	} else {
		printf("unexpected character '%c' at the end of partition\n", *p);
		*ret = NULL;
		return 1;
	}

	/*  allocate memory */
	part = (struct part_info *)malloc(sizeof(struct part_info) + name_len);
	if (!part) {
		printf("out of memory\n");
		return 1;
	}
	memset(part, 0, sizeof(struct part_info) + name_len);
	part->size = size;
	part->offset = offset;
	part->mask_flags = mask_flags;
	part->name = (char *)(part + 1);

	if (name) {
		/* copy user provided name */
		strncpy(part->name, name, name_len - 1);
		part->auto_name = 0;
	} else {
		/* auto generated name in form of size@offset */
		sprintf(part->name, "0x%lx@0x%lx", size, offset);
		part->auto_name = 1;
	}

	part->name[name_len - 1] = '\0';
	INIT_LIST_HEAD(&part->link);

	debug("+ partition: name %-22s size 0x%lx offset 0x%lx mask flags %d\n",
			part->name, part->size,
			part->offset, part->mask_flags);

	*retpart = part;
	return 0;
}

/**
 * Parse device type, name and mtd-id. If syntax is ok allocate memory and
 * return pointer to the device structure.
 *
 * @param mtd_dev pointer to the device definition string i.e. <mtd-dev>
 * @param ret output pointer to next char after parse completes (output)
 * @param retdev pointer to the allocated device (output)
 * @return 0 on success, 1 otherwise
 */
int parse_mtdparts(const char *const mtdparts)
{
	struct part_info *part;

	const char *p;
	struct list_head *entry, *n;
	u16 num_parts;
	u64 offset;
	int err = 1;

	p = mtdparts;

	if (strncmp(p, "mtdparts=", 9) != 0) {
		printf("mtdparts variable doesn't start with 'mtdparts='\n");
		return err;
	}
	p += 9;

	if (!(p = strchr(p, ':'))) {
		printf("no <mtd-id> identifier\n");
		return 1;
	}

	p++;

	/* parse partitions */
	num_parts = 0;

	offset = 0;

	while (p && (*p != '\0') && (*p != ';')) {
		err = 1;
		if ((part_parse(p, &p, &part) != 0) || (!part))
			break;

		/* calculate offset when not specified */
		if (part->offset == OFFSET_NOT_SPECIFIED)
			part->offset = offset;
		else
			offset = part->offset;

		offset += part->size;

		/* partition is ok, add it to the list */
		list_add_tail(&part->link, &parts);
		num_parts++;
		err = 0;
	}
	if (err == 1) {
		part_delall(&parts);
		return 1;
	}

	if (num_parts == 0) {
		//printf("no partitions for device %s%d (%s)\n",
				//MTD_DEV_TYPE(id->type), id->num, id->mtd_id);
		printf("no partitions\n");
		return 1;
	}

	debug("\ntotal partitions: %d\n", num_parts);

	return 0;
}

void print_parts(void)
{
	struct list_head *entry;
	struct part_info *part;

	list_for_each(entry, &parts) {
		part = list_entry(entry, struct part_info, link);
		printf("partition: name %-22s size 0x%lx offset 0x%lx mask flags %d\n",
				part->name, part->size,
				part->offset, part->mask_flags);
	}
}

u64 get_part_size(char *name)
{
	struct list_head *entry;
	struct part_info *part;

	list_for_each(entry, &parts) {
		part = list_entry(entry, struct part_info, link);
		if (strcmp(part->name, name) == 0)
			return part->size;
	}

	return 0;
}

void clear_parse_result(void)
{
	part_delall(&parts);
}
