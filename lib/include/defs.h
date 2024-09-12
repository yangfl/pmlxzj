#ifndef PLZJ_DEFS_H
#define PLZJ_DEFS_H 1

#include <sys/cdefs.h>


#ifndef __always_inline
#  define __always_inline inline __attribute__((__always_inline__))
#endif
#ifndef __attr_access
#  define __attr_access(x) __attribute__((__access__ x))
#endif
#ifndef __attr_access_none
#  define __attr_access_none(argno) __attr_access((__none__, argno))
#endif
#ifndef __attr_format
#  define __attr_format(x) __attribute__((__format__ x))
#endif
#ifndef __attribute_artificial__
#  define __attribute_artificial__ __attribute__((__artificial__))
#endif
#ifndef __attribute_const__
#  define __attribute_const__ __attribute__((__const__))
#endif
#ifndef __attribute_constructor__
#  define __attribute_constructor__ __attribute__((__constructor__))
#endif
#ifndef __attr_dealloc
#  define __attr_dealloc(dealloc, argno) \
    __attribute__((__malloc__(dealloc, argno)))
#endif
#ifndef __attr_dealloc_fclose
#  define __attr_dealloc_fclose __attr_dealloc(fclose, 1)
#endif
#ifndef __attribute_malloc__
#  define __attribute_malloc__ __attribute__((__malloc__))
#endif
#ifndef __attribute_maybe_unused__
#  define __attribute_maybe_unused__ __attribute__((__unused__))
#endif
#ifndef __attribute_noinline__
#  define __attribute_noinline__ __attribute__((__noinline__))
#endif
#ifndef __attribute_pure__
#  define __attribute_pure__ __attribute__((__pure__))
#endif
#ifndef __attribute_warn_unused_result__
#  define __attribute_warn_unused_result__ \
    __attribute__((__warn_unused_result__))
#endif
#ifndef __attribute_weak__
#  if defined _WIN32 || defined __CYGWIN__
#    define __attribute_weak__
#  else
#    define __attribute_weak__ __attribute__((__weak__))
#  endif
#endif
#ifndef __nonnull
#  define __nonnull(params) __attribute__((__nonnull__ params))
#endif
#ifndef __packed
#  define __packed __attribute__((__packed__))
#endif
#ifndef __THROW
#  define __THROW __attribute__((__nothrow__, __leaf__))
#endif


#ifndef PLZJ_API
#  if defined _WIN32 || defined __CYGWIN__
#    ifdef PLZJ_BUILDING_DLL
#      define PLZJ_API __declspec(dllexport)
#    elif defined PLZJ_BUILDING_STATIC
#      define PLZJ_API
#    else
#      define PLZJ_API __declspec(dllimport)
#    endif
#  else
#    define PLZJ_API __attribute__((visibility("default")))
#  endif
#endif


#define plzj_max(a, b) ((a) > (b) ? (a) : (b))
#define plzj_min(a, b) ((a) < (b) ? (a) : (b))
#define plzj_clamp(value, left, right) plzj_min(plzj_max(value, left), right)


#endif /* PLZJ_DEFS_H */
