#ifndef HAVE_EXECINFO_H
#  ifdef __has_include
#    if __has_include(<execinfo.h>)
#      define HAVE_EXECINFO_H 1
#    endif
#  elif defined __GLIBC__
#    define HAVE_EXECINFO_H 1
#  endif
#endif

#ifndef HAVE_EXECINFO_H

#include "debug.h"


int sc_debug_signal (int sig, struct sigaction *oldact) {
  (void) sig;
  (void) oldact;
  return 0;
}

#else

#include <execinfo.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "lib/macro.h"
#include "debug.h"


#define writes(fd, str) (void) (write(fd, str, sizeof(str) - 1) != 0)


struct ScDebugger sc_debugger = {
#ifdef _DEBUG
  .attach = true,
#endif
  .exit_status = EXIT_FAILURE,
  .fd = STDOUT_FILENO,
};


__attribute_noinline__
/**
 * @brief Print backtrace to file descriptor.
 *
 * @param fd File descriptor.
 * @param skip_top Number of topmost frames to skip (This function itself is
 *  always skipped).
 * @param skip_bottom Number of bottom frames to skip.
 * @param with_header Whether to print "Backtrace:" header.
 */
static void print_backtrace (
    int fd, int skip_top, int skip_bottom, bool with_header) {
  void *symbols[64];
  register const int symbols_size = arraysize(symbols);

  // skip itself
  skip_top++;
  if (unlikely(skip_top >= symbols_size)) {
    writes(fd, "(too many frames skipped)\n");
    return;
  }

  // get backtrace
  int size = backtrace(symbols, symbols_size);
  if (unlikely(size <= 0)) {
    writes(fd, "(no available backtrace)\n");
    return;
  }

  // write header
  if (with_header) {
    writes(fd, "Traceback:\n");
  }

  int skipped = size;
  // skip libc init
  if (likely(size < symbols_size)) {
    skipped -= skip_bottom;
  }
  // skip topmost frame
  skipped -= skip_top;
  if (likely(skipped > 0)) {
    backtrace_symbols_fd(symbols + skip_top, skipped, fd);
  }

  // detect if all frames are printed
  if (size == symbols_size) {
    writes(fd, "...and possibly more\n");
  }
}


static bool wait_debugger (int fd) {
  // attached variable
  volatile bool attached = false;

  // set non buffered
  struct termios stdin_termios;
  tcgetattr(STDIN_FILENO, &stdin_termios);
  {
    struct termios termios = stdin_termios;
    termios.c_lflag &= ~(ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &termios);
  }

  // allow Ctrl-C to work
  struct sigaction sigint_sa;
  {
    const struct sigaction default_sa = {.sa_handler = SIG_DFL};
    sigaction(SIGINT, &default_sa, &sigint_sa);
  }

  // wait
  dprintf(fd, "(%d) Start debugging? [y/N] ", getpid());
  struct pollfd pollfd = {.fd = STDIN_FILENO, .events = POLLIN};
  for (unsigned int i = 0; i < 5; i++) {
    if (poll(&pollfd, 1, 1000) > 0) {
      goto char_inputed;
    }
    writes(fd, ".");
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &stdin_termios);
  writes(fd, "\n");
  return false;

char_inputed:
  tcsetattr(STDIN_FILENO, TCSANOW, &stdin_termios);
  char input = getchar();
  if (input != '\n') {
    writes(fd, "\n");
  }
  if (input != 'Y' && input != 'y') {
    return false;
  }

  // prepare being attached
  if (!attached) {
    writes(fd, "Wait debugger to attach... Enter 'fg' to restore program...\n");
    raise(SIGSTOP);
  }

  // test if attached
  if (!attached) {
    return false;
  }
  writes(fd, "Attached!\n");

  // restore
  sigaction(SIGINT, &sigint_sa, NULL);
  return true;
}


static void handler (int sig) {
  (void) sig;

  writes(sc_debugger.fd, "\n===================\nSegmentation fault!\n\n");
  print_backtrace(sc_debugger.fd, 2, 1, true);

  if (sc_debugger.attach) {
    writes(sc_debugger.fd, "\n");
    if (wait_debugger(sc_debugger.fd)) {
      return;
    }
    writes(sc_debugger.fd, "terminated\n");
  }
  _exit(sc_debugger.exit_status);
}


int sc_debug_signal (int sig, struct sigaction *oldact) {
  const struct sigaction sa = {.sa_handler = handler};
  return sigaction(sig == 0 ? SIGSEGV : sig, &sa, oldact);
}


__attribute_constructor__
static void debug_init (void) {
  sc_debug_signal(0, NULL);
}

#endif
