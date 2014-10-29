#include "pevent_base.h"
#include "pevent.h"
#include "pevent_private.h"
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "util.h"


pevent_base_t * pevent_base_create()
{
	pevent_base_t *base;
	
	
	base = fcalloc(1, sizeof(pevent_base_t));

	base->epoll_fd = epoll_create1(0);
	if (base->epoll_fd == -1)
	{
		LOGERROR("epoll_create error:%s\n", strerror(errno));
		free(base);
		return NULL;
	}

	return base;
}

int pevent_base_loop(pevent_base_t *base, int timeout)
{
	int i;
	int nfds;
	pevent_t *pevent;
	
	nfds = epoll_wait(base->epoll_fd, base->events, MAX_EPOLL_EVENTS, timeout);
	
    for(i = 0; i < nfds && nfds > 0; i++)
	{
		pevent = (pevent_t *)base->events[i].data.ptr;

		if (pevent && pevent->event_callback)
		{
			if (base->events[i].events & EPOLLERR || base->events[i].events & EPOLLHUP)
			{
				pevent->event_callback(pevent, PEVENT_ERROR, pevent->ptr);
			}
			else if (base->events[i].events & EPOLLIN)
			{
				pevent->event_callback(pevent, PEVENT_READ, pevent->ptr);
					
			}
			else if (base->events[i].events & EPOLLOUT)
			{
				pevent->event_callback(pevent, PEVENT_WRITE, pevent->ptr);
			}
		}

    }

	return nfds;
}

void pevent_base_cleanup(pevent_base_t *base)
{
	close(base->epoll_fd);
	free(base);
}
