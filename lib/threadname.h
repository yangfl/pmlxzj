#ifndef THREADNAME_H
#define THREADNAME_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "include/defs.h"

/** @file */


// size of thread name, including terminating null
#define THREADNAME_SIZE 16

__THROW __nonnull() __attr_access((__write_only__, 1, 2))
/**
 * @brief Get current thread name.
 *
 * @param[out] buf Buffer to store thread name.
 * @param size Size of @p buf .
 * @return 0 on success, error otherwise.
 */
int threadname_get (char *buf, size_t size);
__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_format((printf, 1, 2))
/**
 * @brief Set current thread name.
 *
 * @param format Format string.
 * @param ... Format arguments.
 * @return 0 on success, error otherwise.
 */
int threadname_set (const char *format, ...);
__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_format((printf, 1, 2))
/**
 * @brief Append string to current thread name.
 *
 * @param format Format string.
 * @param ... Format arguments.
 * @return 0 on success, error otherwise.
 */
int threadname_append (const char *format, ...);


#ifdef __cplusplus
}
#endif

#endif /* THREADNAME_H */
