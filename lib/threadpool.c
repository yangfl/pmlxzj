#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "macro.h"
#include "log.h"
#include "threadname.h"
#include "threadpool.h"


static int ThreadPool_worker (void *arg) {
  struct ThreadPoolWorker *worker = arg;
  struct ThreadPool *pool = worker->pool;

  if (pool->name[0] != '\0') {
    threadname_append(" (%s)", pool->name);
  }

  int ret;
  while (true) {
    if (pool->state < 0) {
      ret = 0;
      break;
    }

    if_fail (mtx_lock(&pool->mutex) == thrd_success) {
      ret = ERR_SYS(mtx_lock);
      break;
    }

    thrd_start_t func = NULL;
    void *arg;
    do {
      if (pool->state == 0) {
        if_fail (cnd_wait(&pool->consumer_cond, &pool->mutex) == thrd_success) {
          ret = ERR_SYS(cnd_wait);
          break;
        }
      }

      if_fail (pool->state >= 0) {
        ret = 0;
        break;
      }
      if (pool->state == 0) {
        continue;
      }

      func = pool->func;
      arg = pool->arg;
      pool->state = 0;

      ret = 0;
    } while (false);

    mtx_unlock(&pool->mutex);
    break_if_fail (ret == 0);
    cnd_signal(&pool->producer_cond);

    if (func != NULL) {
      int res = func(arg);
      if_fail (res == 0) {
        worker->ret = res;
        worker->exc = sc_exc;
        pool->err_i = worker->id;
      }
    }
  }
  return ret;
}


int ThreadPool_run (void *ctx, thrd_start_t func, void *arg) {
  struct ThreadPool *pool = ctx;

  return_if_fail (pool->state >= 0) ERR(PL_ESTOP);
  return_if_fail (mtx_lock(&pool->mutex) == thrd_success) ERR_SYS(mtx_lock);

  int ret = 0;
  do {
    if (pool->state > 0) {
      if_fail (cnd_wait(&pool->producer_cond, &pool->mutex) == thrd_success) {
        ret = ERR_SYS(cnd_wait);
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


void ThreadPool_stop (struct ThreadPool *pool) {
  return_if_fail (pool->state >= 0);

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
}


void ThreadPool_destroy (struct ThreadPool *pool) {
  ThreadPool_stop(pool);

  free(pool->workers);
  cnd_destroy(&pool->consumer_cond);
  cnd_destroy(&pool->producer_cond);
  mtx_destroy(&pool->mutex);
}


int ThreadPool_init (
    struct ThreadPool *pool, unsigned int nproc, const char *name) {
  if (nproc == 0) {
    nproc = sysconf(_SC_NPROCESSORS_ONLN);
    return_if_fail (nproc > 0) ERR(PL_EINVAL);
  }

  int ret;

  return_if_fail (mtx_init(&pool->mutex, mtx_plain) == thrd_success)
    ERR_SYS(mtx_init);
  if_fail (cnd_init(&pool->producer_cond) == thrd_success) {
    ret = ERR_SYS(cnd_init);
    goto fail_producer_cond;
  }
  if_fail (cnd_init(&pool->consumer_cond) == thrd_success) {
    ret = ERR_SYS(cnd_init);
    goto fail_consumer_cond;
  }
  pool->workers = calloc(nproc, sizeof(pool->workers[0]));
  if_fail (pool->workers != NULL) {
    ret = ERR_SYS(calloc);
    goto fail_workers;
  }

  if (name == NULL) {
    pool->name[0] = '\0';
  } else {
    strncpy(pool->name, name, sizeof(pool->name) - 1);
    pool->name[sizeof(pool->name) - 1] = '\0';
  }
  pool->state = 0;

  unsigned int i;
  for (i = 0; i < nproc; i++) {
    pool->workers[i].pool = pool;
    pool->workers[i].id = i;
    break_if_fail (thrd_create(
      &pool->workers[i].thr, ThreadPool_worker, pool->workers + i
    ) == thrd_success);
  }
  if_fail (i > 0) {
    ret = ERR_SYS(thrd_create);
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
