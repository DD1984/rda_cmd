TARGET = rda_cmd
CC = gcc
C_SOURCES =  rda_cmd.c tty.c protocol.c packet.c dump.c mtdparts_parser.c crc32.c file_mmap.c

OBJECTS = $(C_SOURCES:.c=.o)
vpath %.c $(sort $(dir $(C_SOURCES)))

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

all: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

clean:
	-rm -fR .dep $(OBJECTS) $(TARGET)
