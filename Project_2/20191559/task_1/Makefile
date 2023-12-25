CC = gcc
CFLAGS=-O2 -Wall
LDLIBS = -lpthread

all: multiclient stockclient stockserver

multiclient: multiclient.c csapp.c
stockclient: stockclient.c csapp.c
stockserver: stockserver.c echo.c csapp.c

clean:
	rm -rf *~ multiclient stockclient stockserver *.o
