#ifndef PEVENT_BASE_H_
#define PEVENT_BASE_H_



typedef struct _pevent_base pevent_base_t;

pevent_base_t * pevent_base_create();

int pevent_base_loop(pevent_base_t *base, int timeout);

void pevent_base_cleanup(pevent_base_t *base);



#endif