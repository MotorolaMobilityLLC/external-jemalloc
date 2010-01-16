#include <sys/mman.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <errno.h>
#include <limits.h>
#ifndef SIZE_T_MAX
#  define SIZE_T_MAX	SIZE_MAX
#endif
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define	JEMALLOC_MANGLE
#include "jemalloc.h"

#ifdef JEMALLOC_LAZY_LOCK
#include <dlfcn.h>
#endif

#include "rb.h"
#if (defined(JEMALLOC_TCACHE) && defined(JEMALLOC_STATS))
#include "qr.h"
#include "ql.h"
#endif

extern void	(*JEMALLOC_P(malloc_message))(const char *p1, const char *p2,
    const char *p3, const char *p4);

/*
 * Define a custom assert() in order to reduce the chances of deadlock during
 * assertion failure.
 */
#ifdef JEMALLOC_DEBUG
#  define assert(e) do {						\
	if (!(e)) {							\
		char line_buf[UMAX2S_BUFSIZE];				\
		malloc_write4("<jemalloc>: ", __FILE__, ":",		\
		    umax2s(__LINE__, 10, line_buf));			\
		malloc_write4(": Failed assertion: ", "\"", #e,		\
		    "\"\n");						\
		abort();						\
	}								\
} while (0)
#else
#define assert(e)
#endif

/*
 * jemalloc can conceptually be broken into components (arena, tcache, trace,
 * etc.), but there are circular dependencies that cannot be broken without
 * substantial performance degradation.  In order to reduce the effect on
 * visual code flow, read the header files in multiple passes, with one of the
 * following cpp variables defined during each pass:
 *
 *   JEMALLOC_H_TYPES   : Preprocessor-defined constants and psuedo-opaque data
 *                        types.
 *   JEMALLOC_H_STRUCTS : Data structures.
 *   JEMALLOC_H_EXTERNS : Extern data declarations and function prototypes.
 *   JEMALLOC_H_INLINES : Inline functions.
 */
/******************************************************************************/
#define JEMALLOC_H_TYPES

#ifndef __DECONST
#  define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif

#ifdef JEMALLOC_DEBUG
   /* Disable inlining to make debugging easier. */
#  define JEMALLOC_INLINE
#  define inline
#else
#  define JEMALLOC_ENABLE_INLINE
#  define JEMALLOC_INLINE static inline
#endif

/* Size of stack-allocated buffer passed to strerror_r(). */
#define	STRERROR_BUF		64

/* Minimum alignment of allocations is 2^LG_QUANTUM bytes. */
#ifdef __i386__
#  define LG_QUANTUM		4
#endif
#ifdef __ia64__
#  define LG_QUANTUM		4
#endif
#ifdef __alpha__
#  define LG_QUANTUM		4
#endif
#ifdef __sparc__
#  define LG_QUANTUM		4
#endif
#ifdef __amd64__
#  define LG_QUANTUM		4
#endif
#ifdef __arm__
#  define LG_QUANTUM		3
#endif
#ifdef __mips__
#  define LG_QUANTUM		3
#endif
#ifdef __powerpc__
#  define LG_QUANTUM		4
#endif
#ifdef __s390x__
#  define LG_QUANTUM		4
#endif

#define	QUANTUM			((size_t)(1U << LG_QUANTUM))
#define	QUANTUM_MASK		(QUANTUM - 1)

/* Return the smallest quantum multiple that is >= a. */
#define	QUANTUM_CEILING(a)						\
	(((a) + QUANTUM_MASK) & ~QUANTUM_MASK)

#define	SIZEOF_PTR		(1U << LG_SIZEOF_PTR)

/* We can't use TLS in non-PIC programs, since TLS relies on loader magic. */
#if (!defined(PIC) && !defined(NO_TLS))
#  define NO_TLS
#endif

/*
 * Maximum size of L1 cache line.  This is used to avoid cache line aliasing.
 * In addition, this controls the spacing of cacheline-spaced size classes.
 */
#define	LG_CACHELINE		6
#define	CACHELINE		((size_t)(1U << LG_CACHELINE))
#define	CACHELINE_MASK		(CACHELINE - 1)

/* Return the smallest cacheline multiple that is >= s. */
#define	CACHELINE_CEILING(s)						\
	(((s) + CACHELINE_MASK) & ~CACHELINE_MASK)

/*
 * Page size.  STATIC_PAGE_SHIFT is determined by the configure script.  If
 * DYNAMIC_PAGE_SHIFT is enabled, only use the STATIC_PAGE_* macros where
 * compile-time values are required for the purposes of defining data
 * structures.
 */
#define	STATIC_PAGE_SIZE ((size_t)(1U << STATIC_PAGE_SHIFT))
#define	STATIC_PAGE_MASK ((size_t)(STATIC_PAGE_SIZE - 1))

#ifdef DYNAMIC_PAGE_SHIFT
#  define PAGE_SHIFT	lg_pagesize
#  define PAGE_SIZE	pagesize
#  define PAGE_MASK	pagesize_mask
#else
#  define PAGE_SHIFT	STATIC_PAGE_SHIFT
#  define PAGE_SIZE	STATIC_PAGE_SIZE
#  define PAGE_MASK	STATIC_PAGE_MASK
#endif

/* Return the smallest pagesize multiple that is >= s. */
#define	PAGE_CEILING(s)							\
	(((s) + PAGE_MASK) & ~PAGE_MASK)

#include "jemalloc_stats.h"
#include "jemalloc_mutex.h"
#include "jemalloc_extent.h"
#include "jemalloc_arena.h"
#include "jemalloc_base.h"
#include "jemalloc_chunk.h"
#include "jemalloc_huge.h"
#include "jemalloc_tcache.h"
#include "jemalloc_trace.h"

#undef JEMALLOC_H_TYPES
/******************************************************************************/
#define JEMALLOC_H_STRUCTS

#include "jemalloc_stats.h"
#include "jemalloc_mutex.h"
#include "jemalloc_extent.h"
#include "jemalloc_arena.h"
#include "jemalloc_base.h"
#include "jemalloc_chunk.h"
#include "jemalloc_huge.h"
#include "jemalloc_tcache.h"
#include "jemalloc_trace.h"

#undef JEMALLOC_H_STRUCTS
/******************************************************************************/
#define JEMALLOC_H_EXTERNS

extern bool	opt_abort;
#ifdef JEMALLOC_FILL
extern bool	opt_junk;
#endif
#ifdef JEMALLOC_SYSV
extern bool	opt_sysv;
#endif
#ifdef JEMALLOC_XMALLOC
extern bool	opt_xmalloc;
#endif
#ifdef JEMALLOC_FILL
extern bool	opt_zero;
#endif

#ifdef DYNAMIC_PAGE_SHIFT
extern size_t		pagesize;
extern size_t		pagesize_mask;
extern size_t		lg_pagesize;
#endif

/* Number of CPUs. */
extern unsigned		ncpus;

#ifndef NO_TLS
/*
 * Map of pthread_self() --> arenas[???], used for selecting an arena to use
 * for allocations.
 */
extern __thread arena_t	*arenas_map JEMALLOC_ATTR(tls_model("initial-exec"));
#endif
/*
 * Arenas that are used to service external requests.  Not all elements of the
 * arenas array are necessarily used; arenas are created lazily as needed.
 */
extern arena_t		**arenas;
extern unsigned		narenas;

arena_t	*arenas_extend(unsigned ind);
#ifndef NO_TLS
arena_t	*choose_arena_hard(void);
#endif

#include "jemalloc_stats.h"
#include "jemalloc_mutex.h"
#include "jemalloc_extent.h"
#include "jemalloc_arena.h"
#include "jemalloc_base.h"
#include "jemalloc_chunk.h"
#include "jemalloc_huge.h"
#include "jemalloc_tcache.h"
#include "jemalloc_trace.h"

#undef JEMALLOC_H_EXTERNS
/******************************************************************************/
#define JEMALLOC_H_INLINES

#include "jemalloc_stats.h"
#include "jemalloc_mutex.h"
#include "jemalloc_extent.h"
#include "jemalloc_base.h"
#include "jemalloc_chunk.h"
#include "jemalloc_huge.h"

#ifndef JEMALLOC_ENABLE_INLINE
void	malloc_write4(const char *p1, const char *p2, const char *p3,
    const char *p4);
arena_t	*choose_arena(void);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_C_))
/*
 * Wrapper around malloc_message() that avoids the need for
 * JEMALLOC_P(malloc_message)(...) throughout the code.
 */
JEMALLOC_INLINE void
malloc_write4(const char *p1, const char *p2, const char *p3, const char *p4)
{

	JEMALLOC_P(malloc_message)(p1, p2, p3, p4);
}

/*
 * Choose an arena based on a per-thread value (fast-path code, calls slow-path
 * code if necessary).
 */
JEMALLOC_INLINE arena_t *
choose_arena(void)
{
	arena_t *ret;

	/*
	 * We can only use TLS if this is a PIC library, since for the static
	 * library version, libc's malloc is used by TLS allocation, which
	 * introduces a bootstrapping issue.
	 */
#ifndef NO_TLS
	ret = arenas_map;
	if (ret == NULL) {
		ret = choose_arena_hard();
		assert(ret != NULL);
	}
#else
	if (isthreaded && narenas > 1) {
		unsigned long ind;

		/*
		 * Hash pthread_self() to one of the arenas.  There is a prime
		 * number of arenas, so this has a reasonable chance of
		 * working.  Even so, the hashing can be easily thwarted by
		 * inconvenient pthread_self() values.  Without specific
		 * knowledge of how pthread_self() calculates values, we can't
		 * easily do much better than this.
		 */
		ind = (unsigned long) pthread_self() % narenas;

		/*
		 * Optimistially assume that arenas[ind] has been initialized.
		 * At worst, we find out that some other thread has already
		 * done so, after acquiring the lock in preparation.  Note that
		 * this lazy locking also has the effect of lazily forcing
		 * cache coherency; without the lock acquisition, there's no
		 * guarantee that modification of arenas[ind] by another thread
		 * would be seen on this CPU for an arbitrary amount of time.
		 *
		 * In general, this approach to modifying a synchronized value
		 * isn't a good idea, but in this case we only ever modify the
		 * value once, so things work out well.
		 */
		ret = arenas[ind];
		if (ret == NULL) {
			/*
			 * Avoid races with another thread that may have already
			 * initialized arenas[ind].
			 */
			malloc_mutex_lock(&arenas_lock);
			if (arenas[ind] == NULL)
				ret = arenas_extend((unsigned)ind);
			else
				ret = arenas[ind];
			malloc_mutex_unlock(&arenas_lock);
		}
	} else
		ret = arenas[0];
#endif

	assert(ret != NULL);
	return (ret);
}
#endif

#include "jemalloc_tcache.h"
#include "jemalloc_arena.h"
#include "jemalloc_trace.h"

#ifndef JEMALLOC_ENABLE_INLINE
void	*imalloc(size_t size);
void	*icalloc(size_t size);
void	idalloc(void *ptr);
size_t	isalloc(const void *ptr);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_C_))
JEMALLOC_INLINE void *
imalloc(size_t size)
{

	assert(size != 0);

	if (size <= arena_maxclass)
		return (arena_malloc(size, false));
	else
		return (huge_malloc(size, false));
}

JEMALLOC_INLINE void *
icalloc(size_t size)
{

	if (size <= arena_maxclass)
		return (arena_malloc(size, true));
	else
		return (huge_malloc(size, true));
}

JEMALLOC_INLINE void
idalloc(void *ptr)
{
	arena_chunk_t *chunk;

	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (chunk != ptr)
		arena_dalloc(chunk->arena, chunk, ptr);
	else
		huge_dalloc(ptr);
}

JEMALLOC_INLINE size_t
isalloc(const void *ptr)
{
	size_t ret;
	arena_chunk_t *chunk;

	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (chunk != ptr) {
		/* Region. */
		assert(chunk->arena->magic == ARENA_MAGIC);

		ret = arena_salloc(ptr);
	} else
		ret = huge_salloc(ptr);

	return (ret);
}
#endif

#undef JEMALLOC_H_INLINES
/******************************************************************************/
