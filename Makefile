all:
	$(CC) obmc-ikvm.c rfbnpcm750.c rfbusbhid.c -o obmc-ikvm -lvncserver -lpthread

.PHONY: clean
clean:
	rm -f obmc-ikvm
