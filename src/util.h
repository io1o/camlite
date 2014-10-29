#ifndef UTIL_H_
#define UTIL_H_

#include <stdio.h>
#include <stdlib.h>

#define STATIC_STRLEN(x) (sizeof(x) - 1)

#define LOGINFO(...)	fprintf(stderr, __VA_ARGS__)
#define LOGDEBUG(...)	//fprintf(stderr, __VA_ARGS__)
#define LOGWARN(...)	fprintf(stderr, __VA_ARGS__)
#define LOGERROR(...)	fprintf(stderr, "error (%s, %s(), %d): ", __FILE__, __FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__)


//#define MEMORY_ALLOC_REPORT

#ifndef MEMORY_ALLOC_REPORT

#define fcalloc(n, size) fcalloc_impl(n, size)

#define fmalloc(size) fmalloc_impl(size)

#define frealloc(ptr, size)  frealloc_impl(ptr, size)

#else

#define fcalloc(n, size) fcalloc_report(n, size, __FILE__, __FUNCTION__, __LINE__)

#define fmalloc(size) fmalloc_report(size, __FILE__, __FUNCTION__, __LINE__)

#define frealloc(ptr, size)  frealloc_report(ptr, size, __FILE__, __FUNCTION__, __LINE__)

#define free(ptr) ffree_report(ptr)

#endif

void *fcalloc_impl(size_t n, size_t size);

void *fmalloc_impl(size_t size);

void *frealloc_impl(void *ptr, size_t size);

void *fcalloc_report(size_t n, size_t size, const char *file, const char *function, int line);

void *fmalloc_report(size_t size, const char *file, const char *function, int line);

void *frealloc_report(void *ptr, size_t size, const char *file, const char *function, int line);

void ffree_report(void *ptr);

void meminfo_printf();




unsigned long gettickcount();

int read_memory_status();

unsigned long read_cpu_jiffies();

#endif