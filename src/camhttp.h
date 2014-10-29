#ifndef CAMHTTP_H_
#define CAMHTTP_H_


#include <sys/time.h>


typedef struct _pevent_base pevent_base_t;
typedef struct _v4l2port v4l2port_t;

void camhttp_on_video_read(const char *buf,
	int size, struct timeval *timestamp, v4l2port_t *video);

int camhttp_start(pevent_base_t *base,
	unsigned short port,
	const char *username, const char *password);

void camhttp_stop();

#endif