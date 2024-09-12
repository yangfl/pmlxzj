#define _GNU_SOURCE

#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "macro.h"
#include "threadname.h"


int threadname_get (char *buf, size_t size) {
  return pthread_getname_np(pthread_self(), buf, size);
}


int threadname_set (const char *format, ...) {
  char name[THREADNAME_SIZE];

  va_list ap;
  va_start(ap, format);
  vsnprintf(name, sizeof(name), format, ap);
  va_end(ap);

  return pthread_setname_np(pthread_self(), name);
}


int threadname_append (const char *format, ...) {
  pthread_t thread = pthread_self();

  char name[THREADNAME_SIZE];
  return_with_nonzero (pthread_getname_np(thread, name, sizeof(name)));
  size_t len = strlen(name);

  va_list ap;
  va_start(ap, format);
  vsnprintf(name + len, sizeof(name) - len, format, ap);
  va_end(ap);

  return pthread_setname_np(thread, name);
}
