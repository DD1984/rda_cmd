CC = gcc

SRC_DIR = src
C_SOURCES = rda_cmd.c tty.c protocol.c packet.c dump.c mtdparts_parser.c crc32.c file_mmap.c fullfw.c

RDA_CMD_OBJECTS_DIR = rda_cmd_obj
RDA_CMD_OBJECTS = $(patsubst %.c, $(RDA_CMD_OBJECTS_DIR)/%.o, $(C_SOURCES))

CFLAGS = -I ./include
LDFLAGS =

$(RDA_CMD_OBJECTS_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

all: rda_cmd

rda_cmd: rda_cmd_mkdir $(RDA_CMD_OBJECTS)
	$(CC) $(RDA_CMD_OBJECTS) $(LDFLAGS) -o rda_cmd

rda_cmd_mkdir:
	-mkdir -p $(RDA_CMD_OBJECTS_DIR)

clean:
	-rm -rf $(RDA_CMD_OBJECTS) rda_cmd
	-rm -rf $(RDA_CMD_OBJECTS_DIR)
