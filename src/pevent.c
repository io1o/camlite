#include "pevent.h"
#include "pevent_private.h"
#include <ctype.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h> 
#include <error.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include "util.h"

#define TEST_MAX_READ_WRITE_ONCE		size

pevent_t * pevent_t_alloc(pevent_base_t *base, int fd)
{
	pevent_t *pevent;

	pevent = fcalloc(1, sizeof(pevent_t));

	pevent->base = base;
	pevent->fd = fd;

	return pevent;
}


void pevent_t_free(pevent_t *pevent)
{
	free(pevent);
}


pevent_t * pevent_new(pevent_base_t *base,
	int fd, pevent_callback event_callback, void *ptr)
{
	pevent_t *pevent;

	pevent = pevent_t_alloc(base, fd);
	pevent->event_callback = event_callback;
	pevent->ptr = ptr;
	pevent->state = 0;

	return pevent;
}


int pevent_set(pevent_t *pevent, int state)
{
	if (pevent->state == state)
	{
		return -1;
	}

	return pevent_signal(pevent, state);
}

int pevent_signal(pevent_t *pevent, int state)
{
	int mode;
	struct epoll_event event;

	mode = pevent->state == 0
		? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

	if (state == PEVENT_WRITE)
	{
		event.data.ptr = pevent;
		event.events = EPOLLOUT | EPOLLET;

		if (epoll_ctl(pevent->base->epoll_fd, mode, pevent->fd, &event) == -1)
		{
			LOGERROR("epoll_ctl(epoll_fd:%d fd:%d)\n",
				pevent->base->epoll_fd, pevent->fd);
			return -1;
		}

		pevent->state = PEVENT_WRITE;
	}
	else if (state == PEVENT_READ)
	{
		event.data.ptr = pevent;
		event.events = EPOLLIN | EPOLLET;

		if (epoll_ctl(pevent->base->epoll_fd, mode, pevent->fd, &event) == -1)
		{
			LOGERROR("epoll_ctl (epoll_fd:%d fd:%d)\n",
				pevent->base->epoll_fd, pevent->fd);
			return -1;
		}

		pevent->state = PEVENT_READ;
	}
	else if (state == 0)
	{
		event.data.ptr = pevent;
		if (epoll_ctl(pevent->base->epoll_fd, EPOLL_CTL_DEL, pevent->fd, &event) == -1)
		{
			LOGERROR("epoll_ctl (epoll_fd:%d fd:%d)\n",
				pevent->base->epoll_fd, pevent->fd);
		}

		pevent->state = 0;
	}
	
	return 0;
}

void pevent_free(pevent_t *pevent)
{
	struct epoll_event event;

	if (pevent->state != 0)
	{
		event.data.ptr = pevent;
		if (epoll_ctl(pevent->base->epoll_fd, EPOLL_CTL_DEL, pevent->fd, &event) == -1)
		{
			LOGERROR("epoll_ctl (epoll_fd:%d fd:%d)\n",
				pevent->base->epoll_fd, pevent->fd);
		}
	}

	close(pevent->fd);
	pevent_t_free(pevent);
}

void pevent_free_no_close(pevent_t *pevent)
{
	struct epoll_event event;

	if (pevent->state != 0)
	{
		event.data.ptr = pevent;
		if (epoll_ctl(pevent->base->epoll_fd, EPOLL_CTL_DEL, pevent->fd, &event) == -1)
		{
			LOGERROR("epoll_ctl (epoll_fd:%d fd:%d)\n",
				pevent->base->epoll_fd, pevent->fd);
		}
	}

	pevent_t_free(pevent);
}

int pevent_read(pevent_t *pevent, char *buf, int size)
{
	int ret;
	
	
	ret = read(pevent->fd, buf, TEST_MAX_READ_WRITE_ONCE);

	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		return -1;
	}
	else if (ret == 0)
	{
		return -1;
	}
	else
	{
		return ret;
	}
}

int pevent_write(pevent_t *pevent, const char *buf, int size)
{
	int ret;
		
	
	ret = write(pevent->fd, buf, TEST_MAX_READ_WRITE_ONCE);

	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		return -1;
	}
	else if (ret == 0)
	{
		return -1;
	}
	else
	{
		return ret;
	}
}

int pevent_get_flag(pevent_t *pevent)
{
	return pevent->state;
}

int pevent_get_fd(pevent_t *pevent)
{
	return pevent->fd;
}

pevent_base_t * pevent_get_base(pevent_t *pevent)
{
	return pevent->base;
}