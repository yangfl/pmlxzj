#ifndef THREADPOOL_H
#define THREADPOOL_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <threads.h>

#include "include/defs.h"


struct ThreadPoolWorker {
  struct ThreadPool *pool;
  thrd_t thr;
  unsigned int id;
  int ret;
  struct ScException exc;
};


struct ThreadPool {
  struct ThreadPoolWorker *workers;
  unsigned int nproc;
  volatile int err_i;

  char name[16];

  /// -1: stopped, 0: idle, 1: busy
  volatile signed char state;
  mtx_t mutex;
  cnd_t producer_cond;
  cnd_t consumer_cond;
  thrd_start_t func;
  void *arg;
};

__THROW __nonnull((1, 2))
int ThreadPool_run (void *ctx, thrd_start_t func, void *arg);
__THROW __nonnull()
void ThreadPool_stop (struct ThreadPool *pool);
__THROW __nonnull()
void ThreadPool_destroy (struct ThreadPool *pool);
__THROW __nonnull((1)) __attr_access((__read_only__, 3))
int ThreadPool_init (
  struct ThreadPool *pool, unsigned int nproc, const char *name);


#ifdef __cplusplus
}
#endif

#endif /* THREADPOOL_H */
