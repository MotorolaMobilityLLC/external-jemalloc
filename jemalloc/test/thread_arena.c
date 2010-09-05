#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#define	JEMALLOC_MANGLE
#include "jemalloc/jemalloc.h"

void *
thread_start(void *arg)
{
	unsigned main_arena_ind = *(unsigned *)arg;
	unsigned arena_ind;
	size_t size;
	int err;

	JEMALLOC_P(malloc)(1);

	size = sizeof(arena_ind);
	if ((err = JEMALLOC_P(mallctl)("thread.arena", &arena_ind, &size,
	    &main_arena_ind, sizeof(main_arena_ind)))) {
		fprintf(stderr, "%s(): Error in mallctl(): %s\n", __func__,
		    strerror(err));
		return (void *)1;
	}

	return (NULL);
}

int
main(void)
{
	int ret = 0;
	unsigned arena_ind;
	size_t size;
	int err;
	pthread_t thread;

	fprintf(stderr, "Test begin\n");

	JEMALLOC_P(malloc)(1);

	size = sizeof(arena_ind);
	if ((err = JEMALLOC_P(mallctl)("thread.arena", &arena_ind, &size, NULL,
	    0))) {
		fprintf(stderr, "%s(): Error in mallctl(): %s\n", __func__,
		    strerror(err));
		ret = 1;
		goto RETURN;
	}

	if (pthread_create(&thread, NULL, thread_start, (void *)&arena_ind)
	    != 0) {
		fprintf(stderr, "%s(): Error in pthread_create()\n", __func__);
		ret = 1;
		goto RETURN;
	}
	pthread_join(thread, (void *)&ret);

	if (pthread_create(&thread, NULL, thread_start, (void *)&arena_ind)
	    != 0) {
		fprintf(stderr, "%s(): Error in pthread_create()\n", __func__);
		ret = 1;
		goto RETURN;
	}
	pthread_join(thread, (void *)&ret);

RETURN:
	fprintf(stderr, "Test end\n");
	return (ret);
}
