#ifndef SC_DEBUG_H
#define SC_DEBUG_H 1

#ifdef __cplusplus
extern "C" {
#endif

// #include <signal.h>
struct sigaction;
#include <stdbool.h>

#include "lib/include/defs.h"

/**
 * @file
 * Debug utility functions.
 */

/// Signal capturer.
struct ScDebugger {
  /// whether to wait debugger attachment when signal is received
  bool attach;
  /// exit status when signal is received
  int exit_status;
  /// file descriptor for logging
  int fd;
};

extern struct ScDebugger sc_debugger;

__THROW __attr_access((__write_only__, 2))
/**
 * @brief Capture, print backtrace and (optionally) wait for sc_debugger when
 *   signal is received.
 *
 * @param sig Signal number. If 0, @c SIGSEGV is used.
 * @param[out] oldact Previously associated action. Can be @c NULL.
 * @return 0 on success, -1 if @c sigaction() error.
 */
int sc_debug_signal (int sig, struct sigaction *oldact);


#ifdef __cplusplus
}
#endif

#endif /* SC_DEBUG_H */
