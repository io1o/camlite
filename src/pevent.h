#ifndef PEVENT_H_
#define PEVENT_H_

typedef struct _pevent pevent_t;
typedef struct _pevent_base pevent_base_t;

#define PEVENT_READ		1
#define PEVENT_WRITE	2
#define PEVENT_ERROR	3




typedef void (*pevent_callback)(pevent_t *, int, void *);


pevent_t * pevent_new(pevent_base_t *base,
	int fd, pevent_callback event_callback, void *ptr);

int pevent_set(pevent_t *pevent, int state);

int pevent_signal(pevent_t *pevent, int state);

void pevent_free(pevent_t *pevent);

void pevent_free_no_close(pevent_t *pevent);

int pevent_read(pevent_t *pevent, char *buf, int size);

int pevent_write(pevent_t *pevent, const char *buf, int size);


int pevent_get_flag(pevent_t *pevent);

pevent_base_t * pevent_get_base(pevent_t *pevent);

int pevent_get_fd(pevent_t *pevent);

void pevent_set_ptr(pevent_t *pevent, void *ptr);



#endif