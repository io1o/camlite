#ifndef PEVENT_PRIVATE_H_
#define PEVENT_PRIVATE_H_

#define MAX_EPOLL_EVENTS	32

#include <sys/epoll.h>

struct _pevent_base
{
	int epoll_fd;
	struct epoll_event events[MAX_EPOLL_EVENTS];
};

struct _pevent
{
	int fd;
	int state;
	int pending;
	struct _pevent_base *base;
	void *ptr;
	pevent_callback event_callback;
};




#endif