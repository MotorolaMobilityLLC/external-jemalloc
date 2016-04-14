/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef enum {
	dss_prec_disabled  = 0,
	dss_prec_primary   = 1,
	dss_prec_secondary = 2,

	dss_prec_limit     = 3
} dss_prec_t;
#define	DSS_PREC_DEFAULT	dss_prec_secondary
#define	DSS_DEFAULT		"secondary"

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

extern const char *dss_prec_names[];

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

dss_prec_t	chunk_dss_prec_get(tsd_t *tsd);
bool	chunk_dss_prec_set(tsd_t *tsd, dss_prec_t dss_prec);
void	*chunk_alloc_dss(tsd_t *tsd, arena_t *arena, void *new_addr,
    size_t size, size_t alignment, bool *zero, bool *commit);
bool	chunk_in_dss(tsd_t *tsd, void *chunk);
bool	chunk_dss_boot(void);
void	chunk_dss_prefork(tsd_t *tsd);
void	chunk_dss_postfork_parent(tsd_t *tsd);
void	chunk_dss_postfork_child(tsd_t *tsd);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
