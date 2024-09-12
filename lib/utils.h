#ifndef UTILS_H
#define UTILS_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <zlib.h>

#include "include/defs.h"


__nonnull()
typedef int (*stream_fn_t) (FILE *, FILE *, size_t);
__nonnull()
typedef int (*init_fn_t) (void *, void *);


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1, 2))
static inline unsigned long pmlxzj_crc32 (const void *buf, size_t len) {
  return crc32_z(crc32_z(0, Z_NULL, 0), buf, len);
}

__nonnull((1, 2))
void *ptrarray_new (void *arrp, size_t *lenp, size_t elmsize);
__nonnull((1, 2))
int array_new (
  void *arrp, size_t *lenp, size_t elmsize, init_fn_t init_fn,
  void *userdata);

__THROW __nonnull()
int copy (FILE *dst, FILE *src, size_t len);
__THROW __nonnull()
int dump (const char *path, FILE *src, size_t len);
__THROW __nonnull()
int copy_uncompress (FILE *dst, FILE *src, size_t len);

__THROW __nonnull()
int write_lpe (FILE *dst, FILE *src);
__THROW __nonnull() __attr_access((__read_only__, 1))
int dump_lpe (const char *path, FILE *src);

__THROW __nonnull()
int write_lpe_uncompress (FILE *dst, FILE *src);


#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
