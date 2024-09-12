#define _GNU_SOURCE

#include "nproc.h"

#ifdef _WIN32

#include <windows.h>


int get_nproc (void) {
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}

#elif defined MACOS

#include <sys/param.h>
#include <sys/sysctl.h>


int get_nproc (void) {
  int nm[2] = {CTL_HW, HW_AVAILCPU};
  uint32_t count;
  size_t len = sizeof(count);

  sysctl(nm, 2, &count, &len, NULL, 0);
  if (count >= 1) {
    return count;
  }

  nm[1] = HW_NCPU;
  sysctl(nm, 2, &count, &len, NULL, 0);
  if (count >= 1) {
    return count;
  }

  return 1;
}

#elif defined __linux__

#include <sched.h>


int get_nproc (void) {
  cpu_set_t cpuset;
  sched_getaffinity(0, sizeof(cpuset), &cpuset);
  return CPU_COUNT(&cpuset);
}

#else

#include <unistd.h>


int get_nproc (void) {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif
