CFLAGS = -Wall -Werror
LDLIBS = -lncursesw -lm
all: 	scram

clean:
	rm -f scram

scram:	scram.c drvcurses.c util.c
