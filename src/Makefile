
all: camlite

CFLAGS += -DLINUX -D_GNU_SOURCE -Wall -Werror

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $^

camlite: camlite.o util.o v4l2port.o pevent.o pevent_base.o http.o camhttp.o video_manager.o md5.o
	$(CC) -o $@ $^


clean:
	rm -f *.o camlite 

	