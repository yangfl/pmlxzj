#ifndef PLZJ_DEFS_H
#define PLZJ_DEFS_H 1

#include <sys/cdefs.h>


#ifndef __attr_format
  #define __attr_format(x) __attribute__((__format__ x))
#endif

#ifndef __packed
  #define __packed __attribute__((__packed__))
#endif

#ifndef DLL_PUBLIC
  #if defined _WIN32 || defined __CYGWIN__
    #ifdef BUILDING_DLL
      #define DLL_PUBLIC __declspec(dllexport)
    #else
      #define DLL_PUBLIC __declspec(dllimport)
    #endif
  #else
    #define DLL_PUBLIC __attribute__((visibility("default")))
  #endif
#endif


#endif /* PLZJ_DEFS_H */
