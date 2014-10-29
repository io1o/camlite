#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/procfs.h>
#include <linux/types.h>

typedef struct _memory_alloc_info
{
	char file[256];
	char function[256];
	int line;
	void *ptr;
	int size;

	struct _memory_alloc_info *prev;
	struct _memory_alloc_info *next;
} memory_alloc_info_t;

static memory_alloc_info_t *g_meminfo_list;


void meminfo_add(void *ptr, size_t size, const char *file, const char *function, int line)
{
	memory_alloc_info_t *alloc_info;
		
	alloc_info = fcalloc_impl(1, sizeof(memory_alloc_info_t));
	alloc_info->ptr = ptr;
	alloc_info->size = size;
	strcpy(alloc_info->file, file);
	strcpy(alloc_info->function, function);
	alloc_info->line = line;


	if (g_meminfo_list == NULL)
	{
		g_meminfo_list = alloc_info;
	}
	else
	{
		alloc_info->next = g_meminfo_list;

		g_meminfo_list->prev = alloc_info;
		g_meminfo_list = alloc_info;
	}
}

void meminfo_del(void *ptr)
{
	memory_alloc_info_t *meminfo;
	
	meminfo = g_meminfo_list;
	while (meminfo)
	{
		if (meminfo->ptr == ptr)
		{
			if (meminfo == g_meminfo_list)
			{
				g_meminfo_list = meminfo->next;
			}
			else
			{
				meminfo->next->prev = meminfo->prev;
				meminfo->prev->next = meminfo->next;
			}

#undef free
			free(meminfo);
#define free(ptr) ffree_report(ptr)

			break;
		}

		meminfo = meminfo->next;
	}
}

void meminfo_printf()
{
	memory_alloc_info_t *meminfo;
	
	
	meminfo = g_meminfo_list;

	LOGINFO("-------------------meminfo start-------------------\n");
	while (meminfo)
	{
		LOGINFO("[%s:%s:%d]%x:%d\n",
			meminfo->file,
			meminfo->function,
			meminfo->line,
			(unsigned int)meminfo->ptr,
			meminfo->size);

		meminfo = meminfo->next;
	}
	LOGINFO("-------------------meminfo end-------------------\n");
}

void *fcalloc_impl(size_t n, size_t size)
{
	void *ptr;
	
	ptr = calloc(n, size);
	if (!ptr)
	{
		LOGERROR("not enough memory\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

void *fmalloc_impl(size_t size)
{
	void *ptr;
	
	ptr = malloc(size);
	if (!ptr)
	{
		LOGERROR("not enough memory\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

void *frealloc_impl(void *ptr, size_t size)
{
	void *new_ptr;
	
	new_ptr = realloc(ptr, size);
	if (!new_ptr)
	{
		LOGERROR("not enough memory\n");
		exit(EXIT_FAILURE);
	}

	return new_ptr;
}

void *fcalloc_report(size_t n, size_t size, const char *file, const char *function, int line)
{
	void *p;
	
	p = fcalloc_impl(n, size);

	meminfo_add(p, size * n, file, function, line);

	return p;
}

void *fmalloc_report(size_t size, const char *file, const char *function, int line)
{
	void *p;
	
	p = fmalloc_impl(size);
	meminfo_add(p, size, file, function, line);
	return p;
}

void *frealloc_report(void *ptr, size_t size, const char *file, const char *function, int line)
{
	void *p;

	meminfo_del(ptr);

	p = frealloc_impl(ptr, size);

	meminfo_add(p, size, file, function, line);

	return p;
}

void ffree_report(void *ptr)
{
	meminfo_del(ptr);

#undef free
	free(ptr);
#define free(ptr) ffree_report(ptr)
}

int time_diff(struct timeval *start, struct timeval *end)
{
        uint64_t start64;
        uint64_t end64;
        int diff;

        start64 = start->tv_sec * 1000000 + start->tv_usec;
        end64 = end->tv_sec * 1000000 + end->tv_usec;

        diff = (int)((end64 - start64)/1000);

        return diff;
}

unsigned long gettickcount()  
{  
    struct timespec ts;  
  
    clock_gettime(CLOCK_MONOTONIC, &ts);  
  
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);  
}  

int read_memory_status()
{
	int result;
	FILE *fp;

	fp = fopen("/proc/self/statm", "r");
	if (!fp)
		return -1;

	if (fscanf(fp, "%d ", &result) != 1)
	{
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return result * 4;
}

unsigned long read_cpu_jiffies()
{
	FILE *fp;
	unsigned long user, sys, current, cs;

	fp = fopen("/proc/self/stat", "r");
	if (!fp)
		return 0;

	if (fscanf(fp, "%*d %*s %*s %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %lu "
		"%lu %lu %lu", &user, &sys, &current, &cs) != 4)
	{
		fclose(fp);
		return 0;
	}

	fclose(fp);

	return user + sys + current + cs;
}