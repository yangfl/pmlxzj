#ifndef THREADPOOL_H
#define THREADPOOL_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/defs.h"

struct ScException;


struct ThreadPool {
  union {
    char __size[192];
    long long __align;
  };
};

typedef int (*ThreadPool_func_t) (void *arg);

__attribute_warn_unused_result__ __THROW __nonnull((1))
__attr_access((__read_only__, 1)) __attr_access((__write_only__, 2))
int ThreadPool_get_err (
  const struct ThreadPool *pool, const struct ScException **excp);

__THROW __nonnull((1, 2))
int ThreadPool_run (void *ctx, ThreadPool_func_t func, void *arg);
__THROW __nonnull((1)) __attr_access((__write_only__, 2))
int ThreadPool_stop (struct ThreadPool *pool, const struct ScException **excp);
__THROW __nonnull()
void ThreadPool_destroy (struct ThreadPool *pool);
__THROW __nonnull((1)) __attr_access((__read_only__, 3))
int ThreadPool_init (
  struct ThreadPool *pool, unsigned int nproc, const char *name);


#ifdef __cplusplus
}
#endif

#endif /* THREADPOOL_H */
