#define	JEMALLOC_STATS_C_
#include "internal/jemalloc_internal.h"

#define	CTL_GET(n, v, t) do {						\
	size_t sz = sizeof(t);						\
	xmallctl(n, v, &sz, NULL, 0);					\
} while (0)

#define	CTL_I_GET(n, v, t) do {						\
	size_t mib[6];							\
	size_t miblen = sizeof(mib) / sizeof(size_t);			\
	size_t sz = sizeof(t);						\
	xmallctlnametomib(n, mib, &miblen);				\
	mib[2] = i;							\
	xmallctlbymib(mib, miblen, v, &sz, NULL, 0);			\
} while (0)

#define	CTL_J_GET(n, v, t) do {						\
	size_t mib[6];							\
	size_t miblen = sizeof(mib) / sizeof(size_t);			\
	size_t sz = sizeof(t);						\
	xmallctlnametomib(n, mib, &miblen);				\
	mib[2] = j;							\
	xmallctlbymib(mib, miblen, v, &sz, NULL, 0);			\
} while (0)

#define	CTL_IJ_GET(n, v, t) do {					\
	size_t mib[6];							\
	size_t miblen = sizeof(mib) / sizeof(size_t);			\
	size_t sz = sizeof(t);						\
	xmallctlnametomib(n, mib, &miblen);				\
	mib[2] = i;							\
	mib[4] = j;							\
	xmallctlbymib(mib, miblen, v, &sz, NULL, 0);			\
} while (0)

/******************************************************************************/
/* Data. */

bool	opt_stats_print = false;

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

#ifdef JEMALLOC_STATS
static void	malloc_vcprintf(void (*write4)(void *, const char *,
    const char *, const char *, const char *), void *w4opaque,
    const char *format, va_list ap);
static void	stats_arena_bins_print(void (*write4)(void *, const char *,
    const char *, const char *, const char *), void *w4opaque, unsigned i);
static void	stats_arena_lruns_print(void (*write4)(void *, const char *,
    const char *, const char *, const char *), void *w4opaque, unsigned i);
static void	stats_arena_print(void (*write4)(void *, const char *,
    const char *, const char *, const char *), void *w4opaque, unsigned i);
#endif

/******************************************************************************/

/*
 * We don't want to depend on vsnprintf() for production builds, since that can
 * cause unnecessary bloat for static binaries.  umax2s() provides minimal
 * integer printing functionality, so that malloc_printf() use can be limited to
 * JEMALLOC_STATS code.
 */
char *
umax2s(uintmax_t x, unsigned base, char *s)
{
	unsigned i;

	i = UMAX2S_BUFSIZE - 1;
	s[i] = '\0';
	switch (base) {
	case 10:
		do {
			i--;
			s[i] = "0123456789"[x % 10];
			x /= 10;
		} while (x > 0);
		break;
	case 16:
		do {
			i--;
			s[i] = "0123456789abcdef"[x & 0xf];
			x >>= 4;
		} while (x > 0);
		break;
	default:
		do {
			i--;
			s[i] = "0123456789abcdefghijklmnopqrstuvwxyz"[x % base];
			x /= base;
		} while (x > 0);
	}

	return (&s[i]);
}

#ifdef JEMALLOC_STATS
static void
malloc_vcprintf(void (*write4)(void *, const char *, const char *, const char *,
    const char *), void *w4opaque, const char *format, va_list ap)
{
	char buf[4096];

	if (write4 == NULL) {
		/*
		 * The caller did not provide an alternate write4 callback
		 * function, so use the default one.  malloc_write4() is an
		 * inline function, so use malloc_message() directly here.
		 */
		write4 = JEMALLOC_P(malloc_message);
		w4opaque = NULL;
	}

	vsnprintf(buf, sizeof(buf), format, ap);
	write4(w4opaque, buf, "", "", "");
}

/*
 * Print to a callback function in such a way as to (hopefully) avoid memory
 * allocation.
 */
JEMALLOC_ATTR(format(printf, 3, 4))
void
malloc_cprintf(void (*write4)(void *, const char *, const char *, const char *,
    const char *), void *w4opaque, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	malloc_vcprintf(write4, w4opaque, format, ap);
	va_end(ap);
}

/*
 * Print to stderr in such a way as to (hopefully) avoid memory allocation.
 */
JEMALLOC_ATTR(format(printf, 1, 2))
void
malloc_printf(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	malloc_vcprintf(NULL, NULL, format, ap);
	va_end(ap);
}
#endif

#ifdef JEMALLOC_STATS
static void
stats_arena_bins_print(void (*write4)(void *, const char *, const char *,
    const char *, const char *), void *w4opaque, unsigned i)
{
	size_t pagesize;
	bool config_tcache;
	unsigned nbins, j, gap_start;

	CTL_GET("arenas.pagesize", &pagesize, size_t);

	CTL_GET("config.tcache", &config_tcache, bool);
	if (config_tcache) {
		malloc_cprintf(write4, w4opaque,
		    "bins:     bin    size regs pgs  nrequests    "
		    "nfills  nflushes   newruns    reruns maxruns curruns\n");
	} else {
		malloc_cprintf(write4, w4opaque,
		    "bins:     bin    size regs pgs  nrequests   "
		    "newruns    reruns maxruns curruns\n");
	}
	CTL_GET("arenas.nbins", &nbins, unsigned);
	for (j = 0, gap_start = UINT_MAX; j < nbins; j++) {
		uint64_t nruns;

		CTL_IJ_GET("stats.arenas.0.bins.0.nruns", &nruns, uint64_t);
		if (nruns == 0) {
			if (gap_start == UINT_MAX)
				gap_start = j;
		} else {
			unsigned ntbins_, nqbins, ncbins, nsbins;
			size_t reg_size, run_size;
			uint32_t nregs;
			uint64_t nrequests, nfills, nflushes, reruns;
			size_t highruns, curruns;

			if (gap_start != UINT_MAX) {
				if (j > gap_start + 1) {
					/* Gap of more than one size class. */
					malloc_cprintf(write4, w4opaque,
					    "[%u..%u]\n", gap_start,
					    j - 1);
				} else {
					/* Gap of one size class. */
					malloc_cprintf(write4, w4opaque,
					    "[%u]\n", gap_start);
				}
				gap_start = UINT_MAX;
			}
			CTL_GET("arenas.ntbins", &ntbins_, unsigned);
			CTL_GET("arenas.nqbins", &nqbins, unsigned);
			CTL_GET("arenas.ncbins", &ncbins, unsigned);
			CTL_GET("arenas.nsbins", &nsbins, unsigned);
			CTL_J_GET("arenas.bin.0.size", &reg_size, size_t);
			CTL_J_GET("arenas.bin.0.nregs", &nregs, uint32_t);
			CTL_J_GET("arenas.bin.0.run_size", &run_size, size_t);
			CTL_IJ_GET("stats.arenas.0.bins.0.nrequests",
			    &nrequests, uint64_t);
			if (config_tcache) {
				CTL_IJ_GET("stats.arenas.0.bins.0.nfills",
				    &nfills, uint64_t);
				CTL_IJ_GET("stats.arenas.0.bins.0.nflushes",
				    &nflushes, uint64_t);
			}
			CTL_IJ_GET("stats.arenas.0.bins.0.nreruns", &reruns,
			    uint64_t);
			CTL_IJ_GET("stats.arenas.0.bins.0.highruns", &highruns,
			    size_t);
			CTL_IJ_GET("stats.arenas.0.bins.0.curruns", &curruns,
			    size_t);
			if (config_tcache) {
				malloc_cprintf(write4, w4opaque,
				    "%13u %1s %5u %4u %3u %10"PRIu64" %9"PRIu64
				    " %9"PRIu64" %9"PRIu64""
				    " %9"PRIu64" %7zu %7zu\n",
				    j,
				    j < ntbins_ ? "T" : j < ntbins_ + nqbins ?
				    "Q" : j < ntbins_ + nqbins + ncbins ? "C" :
				    j < ntbins_ + nqbins + ncbins + nsbins ? "S"
				    : "M",
				    reg_size, nregs, run_size / pagesize,
				    nrequests, nfills, nflushes, nruns, reruns,
				    highruns, curruns);
			} else {
				malloc_cprintf(write4, w4opaque,
				    "%13u %1s %5u %4u %3u %10"PRIu64" %9"PRIu64
				    " %9"PRIu64" %7zu %7zu\n",
				    j,
				    j < ntbins_ ? "T" : j < ntbins_ + nqbins ?
				    "Q" : j < ntbins_ + nqbins + ncbins ? "C" :
				    j < ntbins_ + nqbins + ncbins + nsbins ? "S"
				    : "M",
				    reg_size, nregs, run_size / pagesize,
				    nrequests, nruns, reruns, highruns,
				    curruns);
			}
		}
	}
	if (gap_start != UINT_MAX) {
		if (j > gap_start + 1) {
			/* Gap of more than one size class. */
			malloc_cprintf(write4, w4opaque, "[%u..%u]\n",
			    gap_start, j - 1);
		} else {
			/* Gap of one size class. */
			malloc_cprintf(write4, w4opaque, "[%u]\n", gap_start);
		}
	}
}

static void
stats_arena_lruns_print(void (*write4)(void *, const char *, const char *,
    const char *, const char *), void *w4opaque, unsigned i)
{
	size_t pagesize, nlruns, j;
	ssize_t gap_start;

	CTL_GET("arenas.pagesize", &pagesize, size_t);

	malloc_cprintf(write4, w4opaque,
	    "large:   size pages nrequests   maxruns   curruns\n");
	CTL_GET("arenas.nlruns", &nlruns, size_t);
	for (j = 0, gap_start = -1; j < nlruns; j++) {
		uint64_t nrequests;
		size_t run_size, highruns, curruns;

		CTL_IJ_GET("stats.arenas.0.lruns.0.nrequests", &nrequests,
		    uint64_t);
		if (nrequests == 0) {
			if (gap_start == -1)
				gap_start = j;
		} else {
			CTL_J_GET("arenas.lrun.0.size", &run_size, size_t);
			CTL_IJ_GET("stats.arenas.0.lruns.0.highruns", &highruns,
			    size_t);
			CTL_IJ_GET("stats.arenas.0.lruns.0.curruns", &curruns,
			    size_t);
			if (gap_start != -1) {
				malloc_cprintf(write4, w4opaque, "[%zu]\n",
				    j - gap_start);
				gap_start = -1;
			}
			malloc_cprintf(write4, w4opaque,
			    "%13zu %5zu %9"PRIu64" %9zu %9zu\n",
			    run_size, run_size / pagesize, nrequests, highruns,
			    curruns);
		}
	}
	if (gap_start != -1)
		malloc_cprintf(write4, w4opaque, "[%zu]\n", j - gap_start);
}

static void
stats_arena_print(void (*write4)(void *, const char *, const char *,
    const char *, const char *), void *w4opaque, unsigned i)
{
	size_t pagesize, pactive, pdirty, mapped;
	uint64_t npurge, nmadvise, purged;
	size_t small_allocated;
	uint64_t small_nmalloc, small_ndalloc;
	size_t medium_allocated;
	uint64_t medium_nmalloc, medium_ndalloc;
	size_t large_allocated;
	uint64_t large_nmalloc, large_ndalloc;

	CTL_GET("arenas.pagesize", &pagesize, size_t);

	CTL_I_GET("stats.arenas.0.pactive", &pactive, size_t);
	CTL_I_GET("stats.arenas.0.pdirty", &pdirty, size_t);
	CTL_I_GET("stats.arenas.0.npurge", &npurge, uint64_t);
	CTL_I_GET("stats.arenas.0.nmadvise", &nmadvise, uint64_t);
	CTL_I_GET("stats.arenas.0.purged", &purged, uint64_t);
	malloc_cprintf(write4, w4opaque,
	    "dirty pages: %zu:%zu active:dirty, %"PRIu64" sweep%s,"
	    " %"PRIu64" madvise%s, %"PRIu64" purged\n",
	    pactive, pdirty, npurge, npurge == 1 ? "" : "s",
	    nmadvise, nmadvise == 1 ? "" : "s", purged);

	malloc_cprintf(write4, w4opaque,
	    "            allocated      nmalloc      ndalloc\n");
	CTL_I_GET("stats.arenas.0.small.allocated", &small_allocated, size_t);
	CTL_I_GET("stats.arenas.0.small.nmalloc", &small_nmalloc, uint64_t);
	CTL_I_GET("stats.arenas.0.small.ndalloc", &small_ndalloc, uint64_t);
	malloc_cprintf(write4, w4opaque,
	    "small:   %12zu %12"PRIu64" %12"PRIu64"\n",
	    small_allocated, small_nmalloc, small_ndalloc);
	CTL_I_GET("stats.arenas.0.medium.allocated", &medium_allocated, size_t);
	CTL_I_GET("stats.arenas.0.medium.nmalloc", &medium_nmalloc, uint64_t);
	CTL_I_GET("stats.arenas.0.medium.ndalloc", &medium_ndalloc, uint64_t);
	malloc_cprintf(write4, w4opaque,
	    "medium:  %12zu %12"PRIu64" %12"PRIu64"\n",
	    medium_allocated, medium_nmalloc, medium_ndalloc);
	CTL_I_GET("stats.arenas.0.large.allocated", &large_allocated, size_t);
	CTL_I_GET("stats.arenas.0.large.nmalloc", &large_nmalloc, uint64_t);
	CTL_I_GET("stats.arenas.0.large.ndalloc", &large_ndalloc, uint64_t);
	malloc_cprintf(write4, w4opaque,
	    "large:   %12zu %12"PRIu64" %12"PRIu64"\n",
	    large_allocated, large_nmalloc, large_ndalloc);
	malloc_cprintf(write4, w4opaque,
	    "total:   %12zu %12"PRIu64" %12"PRIu64"\n",
	    small_allocated + medium_allocated + large_allocated,
	    small_nmalloc + medium_nmalloc + large_nmalloc,
	    small_ndalloc + medium_ndalloc + large_ndalloc);
	malloc_cprintf(write4, w4opaque, "active:  %12zu\n",
	    pactive * pagesize );
	CTL_I_GET("stats.arenas.0.mapped", &mapped, size_t);
	malloc_cprintf(write4, w4opaque, "mapped:  %12zu\n", mapped);

	stats_arena_bins_print(write4, w4opaque, i);
	stats_arena_lruns_print(write4, w4opaque, i);
}
#endif

void
stats_print(void (*write4)(void *, const char *, const char *, const char *,
    const char *), void *w4opaque, const char *opts)
{
	uint64_t epoch;
	size_t u64sz;
	char s[UMAX2S_BUFSIZE];
	bool general = true;
	bool merged = true;
	bool unmerged = true;
	bool bins = true;
	bool large = true;

	/* Refresh stats, in case mallctl() was called by the application. */
	epoch = 1;
	u64sz = sizeof(uint64_t);
	xmallctl("epoch", &epoch, &u64sz, &epoch, sizeof(uint64_t));

	if (write4 == NULL) {
		/*
		 * The caller did not provide an alternate write4 callback
		 * function, so use the default one.  malloc_write4() is an
		 * inline function, so use malloc_message() directly here.
		 */
		write4 = JEMALLOC_P(malloc_message);
		w4opaque = NULL;
	}

	if (opts != NULL) {
		unsigned i;

		for (i = 0; opts[i] != '\0'; i++) {
			switch (opts[i]) {
				case 'g':
					general = false;
					break;
				case 'm':
					merged = false;
					break;
				case 'a':
					unmerged = false;
					break;
				case 'b':
					bins = false;
					break;
				case 'l':
					large = false;
					break;
				default:;
			}
		}
	}

	write4(w4opaque, "___ Begin jemalloc statistics ___\n", "", "", "");
	if (general) {
		int err;
		bool bv;
		unsigned uv;
		ssize_t ssv;
		size_t sv, bsz, ssz;

		bsz = sizeof(bool);
		ssz = sizeof(size_t);

		CTL_GET("config.debug", &bv, bool);
		write4(w4opaque, "Assertions ", bv ? "enabled" : "disabled",
		    "\n", "");

		write4(w4opaque, "Boolean JEMALLOC_OPTIONS: ", "", "", "");
		if ((err = mallctl("opt.abort", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "A" : "a", "", "", "");
		if ((err = mallctl("opt.junk", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "J" : "j", "", "", "");
		if ((err = mallctl("opt.overcommit", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "O" : "o", "", "", "");
		write4(w4opaque, "P", "", "", "");
		if ((err = mallctl("opt.tcache_sort", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "S" : "s", "", "", "");
		if ((err = mallctl("opt.trace", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "T" : "t", "", "", "");
		if ((err = mallctl("opt.sysv", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "V" : "v", "", "", "");
		if ((err = mallctl("opt.xmalloc", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "X" : "x", "", "", "");
		if ((err = mallctl("opt.zero", &bv, &bsz, NULL, 0)) == 0)
			write4(w4opaque, bv ? "Z" : "z", "", "", "");
		write4(w4opaque, "\n", "", "", "");

		write4(w4opaque, "CPUs: ", umax2s(ncpus, 10, s), "\n", "");

		CTL_GET("arenas.narenas", &uv, unsigned);
		write4(w4opaque, "Max arenas: ", umax2s(uv, 10, s), "\n", "");

		write4(w4opaque, "Pointer size: ", umax2s(sizeof(void *), 10,
		    s), "\n", "");

		CTL_GET("arenas.quantum", &sv, size_t);
		write4(w4opaque, "Quantum size: ", umax2s(sv, 10, s), "\n", "");

		CTL_GET("arenas.cacheline", &sv, size_t);
		write4(w4opaque, "Cacheline size (assumed): ", umax2s(sv, 10,
		    s), "\n", "");

		CTL_GET("arenas.subpage", &sv, size_t);
		write4(w4opaque, "Subpage spacing: ", umax2s(sv, 10, s), "\n",
		    "");

		CTL_GET("arenas.medium", &sv, size_t);
		write4(w4opaque, "Medium spacing: ", umax2s(sv, 10, s), "\n",
		    "");

		if ((err = mallctl("arenas.tspace_min", &sv, &ssz, NULL, 0)) ==
		    0) {
			write4(w4opaque, "Tiny 2^n-spaced sizes: [", umax2s(sv,
			    10, s), "..", "");

			CTL_GET("arenas.tspace_max", &sv, size_t);
			write4(w4opaque, umax2s(sv, 10, s), "]\n", "", "");
		}

		CTL_GET("arenas.qspace_min", &sv, size_t);
		write4(w4opaque, "Quantum-spaced sizes: [", umax2s(sv, 10, s),
		    "..", "");
		CTL_GET("arenas.qspace_max", &sv, size_t);
		write4(w4opaque, umax2s(sv, 10, s), "]\n", "", "");

		CTL_GET("arenas.cspace_min", &sv, size_t);
		write4(w4opaque, "Cacheline-spaced sizes: [", umax2s(sv, 10, s),
		    "..", "");
		CTL_GET("arenas.cspace_max", &sv, size_t);
		write4(w4opaque, umax2s(sv, 10, s), "]\n", "", "");

		CTL_GET("arenas.sspace_min", &sv, size_t);
		write4(w4opaque, "Subpage-spaced sizes: [", umax2s(sv, 10, s),
		    "..", "");
		CTL_GET("arenas.sspace_max", &sv, size_t);
		write4(w4opaque, umax2s(sv, 10, s), "]\n", "", "");

		CTL_GET("arenas.medium_min", &sv, size_t);
		write4(w4opaque, "Medium sizes: [", umax2s(sv, 10, s), "..",
		    "");
		CTL_GET("arenas.medium_max", &sv, size_t);
		write4(w4opaque, umax2s(sv, 10, s), "]\n", "", "");

		CTL_GET("opt.lg_dirty_mult", &ssv, ssize_t);
		if (ssv >= 0) {
			write4(w4opaque,
			    "Min active:dirty page ratio per arena: ",
			    umax2s((1U << ssv), 10, s), ":1\n", "");
		} else {
			write4(w4opaque,
			    "Min active:dirty page ratio per arena: N/A\n", "",
			    "", "");
		}
#ifdef JEMALLOC_TCACHE
		if ((err = mallctl("opt.lg_tcache_nslots", &sv, &ssz, NULL, 0))
		    == 0) {
			size_t tcache_nslots, tcache_gc_sweep;

			tcache_nslots = (1U << sv);
			write4(w4opaque, "Thread cache slots per size class: ",
			    tcache_nslots ? umax2s(tcache_nslots, 10, s) :
			    "N/A", "\n", "");

			CTL_GET("opt.lg_tcache_gc_sweep", &ssv, ssize_t);
			tcache_gc_sweep = (1U << ssv);
			write4(w4opaque, "Thread cache GC sweep interval: ",
			    tcache_nslots && ssv >= 0 ? umax2s(tcache_gc_sweep,
			    10, s) : "N/A", "\n", "");
		}
#endif
		CTL_GET("arenas.chunksize", &sv, size_t);
		write4(w4opaque, "Chunk size: ", umax2s(sv, 10, s), "", "");
		CTL_GET("opt.lg_chunk", &sv, size_t);
		write4(w4opaque, " (2^", umax2s(sv, 10, s), ")\n", "");
	}

#ifdef JEMALLOC_STATS
	{
		int err;
		size_t ssz;
		size_t allocated, active, mapped;
		size_t chunks_current, chunks_high, swap_avail;
		uint64_t chunks_total;
		size_t huge_allocated;
		uint64_t huge_nmalloc, huge_ndalloc;

		ssz = sizeof(size_t);

		CTL_GET("stats.allocated", &allocated, size_t);
		CTL_GET("stats.active", &active, size_t);
		CTL_GET("stats.mapped", &mapped, size_t);
		malloc_cprintf(write4, w4opaque,
		    "Allocated: %zu, active: %zu, mapped: %zu\n", allocated,
		    active, mapped);

		/* Print chunk stats. */
		CTL_GET("stats.chunks.total", &chunks_total, uint64_t);
		CTL_GET("stats.chunks.high", &chunks_high, size_t);
		CTL_GET("stats.chunks.current", &chunks_current, size_t);
		if ((err = mallctl("swap.avail", &swap_avail, &ssz, NULL, 0))
		    == 0) {
			size_t lg_chunk;

			malloc_cprintf(write4, w4opaque, "chunks: nchunks   "
			    "highchunks    curchunks   swap_avail\n");
			CTL_GET("opt.lg_chunk", &lg_chunk, size_t);
			malloc_cprintf(write4, w4opaque,
			    "  %13"PRIu64"%13zu%13zu%13zu\n",
			    chunks_total, chunks_high, chunks_current,
			    swap_avail << lg_chunk);
		} else {
			malloc_cprintf(write4, w4opaque, "chunks: nchunks   "
			    "highchunks    curchunks\n");
			malloc_cprintf(write4, w4opaque,
			    "  %13"PRIu64"%13zu%13zu\n",
			    chunks_total, chunks_high, chunks_current);
		}

		/* Print huge stats. */
		CTL_GET("stats.huge.nmalloc", &huge_nmalloc, uint64_t);
		CTL_GET("stats.huge.ndalloc", &huge_ndalloc, uint64_t);
		CTL_GET("stats.huge.allocated", &huge_allocated, size_t);
		malloc_cprintf(write4, w4opaque,
		    "huge: nmalloc      ndalloc    allocated\n");
		malloc_cprintf(write4, w4opaque,
		    " %12"PRIu64" %12"PRIu64" %12zu\n",
		    huge_nmalloc, huge_ndalloc, huge_allocated);

		if (merged) {
			unsigned narenas;

			CTL_GET("arenas.narenas", &narenas, unsigned);
			{
				bool initialized[narenas];
				size_t isz;
				unsigned i, ninitialized;

				isz = sizeof(initialized);
				xmallctl("arenas.initialized", initialized,
				    &isz, NULL, 0);
				for (i = ninitialized = 0; i < narenas; i++) {
					if (initialized[i])
						ninitialized++;
				}

				if (ninitialized > 1) {
					/* Print merged arena stats. */
					malloc_cprintf(write4, w4opaque,
					    "\nMerge arenas stats:\n");
					stats_arena_print(write4, w4opaque,
					    narenas);
				}
			}
		}

		if (unmerged) {
			unsigned narenas;

			/* Print stats for each arena. */

			CTL_GET("arenas.narenas", &narenas, unsigned);
			{
				bool initialized[narenas];
				size_t isz;
				unsigned i;

				isz = sizeof(initialized);
				xmallctl("arenas.initialized", initialized,
				    &isz, NULL, 0);

				for (i = 0; i < narenas; i++) {
					if (initialized[i]) {
						malloc_cprintf(write4, w4opaque,
						    "\narenas[%u]:\n", i);
						stats_arena_print(write4,
						    w4opaque, i);
					}
				}
			}
		}
	}
#endif /* #ifdef JEMALLOC_STATS */
	write4(w4opaque, "--- End jemalloc statistics ---\n", "", "", "");
}
