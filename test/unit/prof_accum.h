#include "test/jemalloc_test.h"

#define	NTHREADS		4
#define	NALLOCS_PER_THREAD	50
#define	DUMP_INTERVAL		1
#define	BT_COUNT_CHECK_INTERVAL	5

#define	alloc_n_proto(n)						\
void	*alloc_##n(unsigned bits);
alloc_n_proto(0)
alloc_n_proto(1)

#define	alloc_n_gen(n)							\
void *									\
alloc_##n(unsigned bits)						\
{									\
									\
	if (bits == 0) {						\
		void *p = mallocx(1, 0);				\
		assert_ptr_not_null(p, "Unexpected mallocx() failure");	\
		return (p);						\
	} else {							\
		switch (bits & 0x1U) {					\
		case 0: return (alloc_0(bits >> 1));			\
		case 1: return (alloc_1(bits >> 1));			\
		default: not_reached();					\
		}							\
	}								\
}
