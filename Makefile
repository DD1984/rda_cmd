CC = gcc

CFLAGS = -I ./include
LDFLAGS =

SRC_DIR = src

RDA_CMD_C_SOURCES = rda_cmd.c tty.c protocol.c packet.c dump.c mtdparts_parser.c crc32.c file_mmap.c fullfw.c
RDA_MKFW_C_SOURCES = rda_mkfw.c file_mmap.c dump.c fullfw.c

RDA_CMD_OBJECTS_DIR = rda_cmd_obj
RDA_MKFW_OBJECTS_DIR = rda_mkfw_obj

RDA_CMD_OBJECTS = $(patsubst %.c, $(RDA_CMD_OBJECTS_DIR)/%.o, $(RDA_CMD_C_SOURCES))
RDA_MKFW_OBJECTS = $(patsubst %.c, $(RDA_MKFW_OBJECTS_DIR)/%.o, $(RDA_MKFW_C_SOURCES))


$(RDA_CMD_OBJECTS_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(RDA_MKFW_OBJECTS_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

all: rda_cmd rda_mkfw

rda_cmd: rda_cmd_mkdir $(RDA_CMD_OBJECTS)
	$(CC) $(RDA_CMD_OBJECTS) $(LDFLAGS) -o rda_cmd

rda_cmd_mkdir: 
	-mkdir -p $(RDA_CMD_OBJECTS_DIR)

rda_mkfw: rda_mkfw_mkdir $(RDA_MKFW_OBJECTS)
	$(CC) $(RDA_MKFW_OBJECTS) $(LDFLAGS) -o rda_mkfw

rda_mkfw_mkdir:
	-mkdir -p $(RDA_MKFW_OBJECTS_DIR)

clean:
	-rm -rf $(RDA_CMD_OBJECTS) rda_cmd
	-rm -rf $(RDA_CMD_OBJECTS_DIR)
	-rm -rf $(RDA_MKFW_OBJECTS) rda_mkfw
	-rm -rf $(RDA_MKFW_OBJECTS_DIR)
