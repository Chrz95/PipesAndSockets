FLAGS = -Wall -O2 -o
SOURCE = remoteClient.c remoteServer.c

all: remoteClient remoteServer

remoteClient: remoteClient.c
	gcc $(FLAGS) remoteClient remoteClient.c

remoteServer: remoteServer.c
	gcc $(FLAGS) remoteServer remoteServer.c -lm

clean:
	rm -f remoteClient remoteServer
	find  . -name "*output*" -type f -delete 

count:
	wc $(SOURCE)
