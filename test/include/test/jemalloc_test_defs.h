/* test/include/test/jemalloc_test_defs.h.  Generated from jemalloc_test_defs.h.in by configure.  */
#include "jemalloc/internal/jemalloc_internal_defs.h"
#include "jemalloc/internal/jemalloc_internal_decls.h"

/*
 * For use by SFMT.  configure.ac doesn't actually define HAVE_SSE2 because its
 * dependencies are notoriously unportable in practice.
 */
#if defined(__x86_64__)
#define HAVE_SSE2 
#endif
/* #undef HAVE_ALTIVEC */
