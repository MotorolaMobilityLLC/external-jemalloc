#include "test/jemalloc_test.h"

bool
mtx_init(mtx_t *mtx)
{

#ifdef _WIN32
	if (!InitializeCriticalSectionAndSpinCount(&mtx->lock, _CRT_SPINCOUNT))
		return (true);
#elif (defined(JEMALLOC_OSSPIN))
	mtx->lock = 0;
#else
	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0)
		return (true);
	pthread_mutexattr_settype(&attr, MALLOC_MUTEX_TYPE);
	if (pthread_mutex_init(&mtx->lock, &attr) != 0) {
		pthread_mutexattr_destroy(&attr);
		return (true);
	}
	pthread_mutexattr_destroy(&attr);
#endif
	return (false);
}

void
mtx_fini(mtx_t *mtx)
{

#ifdef _WIN32
#elif (defined(JEMALLOC_OSSPIN))
#else
	pthread_mutex_destroy(&mtx->lock);
#endif
}

void
mtx_lock(mtx_t *mtx)
{

#ifdef _WIN32
	EnterCriticalSection(&mtx->lock);
#elif (defined(JEMALLOC_OSSPIN))
	OSSpinLockLock(&mtx->lock);
#else
	pthread_mutex_lock(&mtx->lock);
#endif
}

void
mtx_unlock(mtx_t *mtx)
{

#ifdef _WIN32
	LeaveCriticalSection(&mtx->lock);
#elif (defined(JEMALLOC_OSSPIN))
	OSSpinLockUnlock(&mtx->lock);
#else
	pthread_mutex_unlock(&mtx->lock);
#endif
}
