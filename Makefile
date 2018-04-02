
SRC =  rda_cmd.c tty.c packet.c dump.c


all:
	gcc $(SRC) -o rda_cmd

clean:
	rm *.o rda_cmd
