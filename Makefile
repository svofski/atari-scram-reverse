LDLIBS = -lncurses -lm
all: 	scram

clean:
	rm -f scram

scram:	scram.c drvcurses.c util.c
