#ifdef NO_THREADS

#include "log.h"
#include "threadpool.h"


struct _ThreadPool {
  int ret;
};
static_assert(sizeof(struct _ThreadPool) <= sizeof(struct ThreadPool));
static_assert(alignof(struct _ThreadPool) <= alignof(struct ThreadPool));


int ThreadPool_get_err (
    const struct ThreadPool *_pool, const struct ScException **excp) {
  const struct _ThreadPool *pool = (const struct _ThreadPool *) _pool;

  if (pool->ret != 0 && excp != NULL) {
    *excp = &sc_exc;
  }
  return pool->ret;
}


int ThreadPool_run (void *ctx, ThreadPool_func_t func, void *arg) {
  struct _ThreadPool *pool = ctx;

  pool->ret = func(arg);
  return pool->ret;
}


int ThreadPool_stop (
    struct ThreadPool *_pool, const struct ScException **excp) {
  return ThreadPool_get_err(_pool, excp);
}


void ThreadPool_destroy (struct ThreadPool *_pool) {
  (void) _pool;
}


int ThreadPool_init (
    struct ThreadPool *_pool, unsigned int nproc, const char *name) {
  const struct _ThreadPool *pool = (const struct _ThreadPool *) _pool;

  pool->ret = 0;
  return 0;
}

#else

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "platform/c11threads.h"
#include "platform/nproc.h"
#include "macro.h"
#include "log.h"
#include "threadname.h"
#include "threadpool.h"

struct _ThreadPool;


struct ThreadPoolWorker {
  struct _ThreadPool *pool;
  thrd_t thr;
  unsigned int id;

  int ret;
  struct ScException exc;
};


struct _ThreadPool {
  struct ThreadPoolWorker *workers;
  unsigned int nproc;
  volatile int err_i;

  char name[16];

  /// -1: stopped, 0: idle, 1: busy
  volatile signed char state;
  mtx_t mutex;
  cnd_t producer_cond;
  cnd_t consumer_cond;

  ThreadPool_func_t func;
  void *arg;
};
static_assert(sizeof(struct _ThreadPool) <= sizeof(struct ThreadPool));
static_assert(alignof(struct _ThreadPool) <= alignof(struct ThreadPool));


int ThreadPool_get_err (
    const struct ThreadPool *_pool, const struct ScException **excp) {
  const struct _ThreadPool *pool = (const struct _ThreadPool *) _pool;

  return_if_fail (
    pool->err_i >= 0 && (unsigned int) pool->err_i < pool->nproc) 0;
  struct ThreadPoolWorker *worker = &pool->workers[pool->err_i];
  return_if_fail (worker->ret != 0) 0;
  if (excp != NULL) {
    *excp = &worker->exc;
  }
  return worker->ret;
}


static int ThreadPool_worker (void *arg) {
  struct ThreadPoolWorker *worker = arg;
  struct _ThreadPool *pool = worker->pool;

  if (pool->name[0] != '\0') {
    threadname_append(" (%s)", pool->name);
  }
  sc_debug("ThreadPool worker %u begin\n", worker->id);

  int ret = 0;
  while (true) {
    if (pool->state < 0) {
      break;
    }

    if_fail (mtx_lock(&pool->mutex) == thrd_success) {
      ret = ERR_STD(mtx_lock);
      break;
    }

    ThreadPool_func_t func = NULL;
    void *funcarg;
    do {
      if (pool->state == 0) {
        if_fail (cnd_wait(&pool->consumer_cond, &pool->mutex) == thrd_success) {
          ret = ERR_STD(cnd_wait);
          break;
        }
      }

      if_fail (pool->state >= 0) {
        break;
      }
      if (pool->state == 0) {
        continue;
      }

      func = pool->func;
      funcarg = pool->arg;
      pool->state = 0;
    } while (false);

    mtx_unlock(&pool->mutex);
    break_if_fail (ret == 0);
    cnd_signal(&pool->producer_cond);

    if (func != NULL) {
      int res = func(funcarg);
      if_fail (res == 0) {
        worker->ret = res;
        worker->exc = sc_exc;
        pool->err_i = worker->id;
      }
    }
  }

  sc_debug("ThreadPool worker %u stopped\n", worker->id);
  return ret;
}


int ThreadPool_run (void *ctx, ThreadPool_func_t func, void *arg) {
  struct _ThreadPool *pool = ctx;

  return_if_fail (pool->state >= 0) ERR(PL_ESTOP);
  return_if_fail (mtx_lock(&pool->mutex) == thrd_success) ERR_STD(mtx_lock);

  int ret = 0;
  do {
    if (pool->state > 0) {
      if_fail (cnd_wait(&pool->producer_cond, &pool->mutex) == thrd_success) {
        ret = ERR_STD(cnd_wait);
        break;
      }
    }

    if_fail (pool->state >= 0) {
      ret = ERR(PL_ESTOP);
      break;
    }
    if (pool->state > 0) {
      continue;
    }

    pool->func = func;
    pool->arg = arg;
    pool->state = 1;

    ret = 0;
  } while (false);

  mtx_unlock(&pool->mutex);

  if (ret == 0) {
    cnd_signal(&pool->consumer_cond);
  }
  return ret;
}


int ThreadPool_stop (
    struct ThreadPool *_pool, const struct ScException **excp) {
  struct _ThreadPool *pool = (struct _ThreadPool *) _pool;

  return_if_fail (pool->state >= 0) 0;

  mtx_lock(&pool->mutex);

  do {
    if (pool->state > 0) {
      cnd_wait(&pool->producer_cond, &pool->mutex);
    }

    if (pool->state > 0) {
      continue;
    }

    pool->state = -1;
  } while (false);

  mtx_unlock(&pool->mutex);
  cnd_broadcast(&pool->consumer_cond);

  for (unsigned int i = 0; i < pool->nproc; i++) {
    thrd_join(pool->workers[i].thr, NULL);
  }

  return ThreadPool_get_err(_pool, excp);
}


void ThreadPool_destroy (struct ThreadPool *_pool) {
  struct _ThreadPool *pool = (struct _ThreadPool *) _pool;

  const struct ScException *exc;
  int ret = ThreadPool_stop(_pool, &exc);
  if_fail (ret == 0) {
    ScException_stderr(exc, NULL);
  }

  free(pool->workers);
  cnd_destroy(&pool->consumer_cond);
  cnd_destroy(&pool->producer_cond);
  mtx_destroy(&pool->mutex);
}


int ThreadPool_init (
    struct ThreadPool *_pool, unsigned int nproc, const char *name) {
  struct _ThreadPool *pool = (struct _ThreadPool *) _pool;

  if (nproc == 0) {
    nproc = get_nproc();
    return_if_fail (nproc > 0) ERR(PL_EINVAL);
  }

  int ret;

  return_if_fail (mtx_init(&pool->mutex, mtx_plain) == thrd_success)
    ERR_STD(mtx_init);
  if_fail (cnd_init(&pool->producer_cond) == thrd_success) {
    ret = ERR_STD(cnd_init);
    goto fail_producer_cond;
  }
  if_fail (cnd_init(&pool->consumer_cond) == thrd_success) {
    ret = ERR_STD(cnd_init);
    goto fail_consumer_cond;
  }
  pool->workers = calloc(nproc, sizeof(pool->workers[0]));
  if_fail (pool->workers != NULL) {
    ret = ERR_STD(calloc);
    goto fail_workers;
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
  if (name == NULL) {
    pool->name[0] = '\0';
  } else {
    strncpy(pool->name, name, sizeof(pool->name) - 1);
    pool->name[sizeof(pool->name) - 1] = '\0';
  }
  pool->state = 0;
#pragma GCC diagnostic pop

  unsigned int i;
  for (i = 0; i < nproc; i++) {
    pool->workers[i].pool = pool;
    pool->workers[i].id = i;
    break_if_fail (thrd_create(
      &pool->workers[i].thr, ThreadPool_worker, pool->workers + i
    ) == thrd_success);
  }
  if_fail (i > 0) {
    ret = ERR_STD(thrd_create);
    goto fail;
  }

  if_fail (i >= nproc) {
    pool->workers = realloc(pool->workers, sizeof(pool->workers[0]) * i);
  }
  pool->nproc = i;
  pool->err_i = -1;
  return 0;

fail:
  free(pool->workers);
fail_workers:
  cnd_destroy(&pool->consumer_cond);
fail_consumer_cond:
  cnd_destroy(&pool->producer_cond);
fail_producer_cond:
  mtx_destroy(&pool->mutex);
  return ret;
}

#endif
