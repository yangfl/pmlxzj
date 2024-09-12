#ifndef PLZJ_PLATFORM_ENDIAN_H
#define PLZJ_PLATFORM_ENDIAN_H 1


/* import sys defs */

#if defined _WIN16 || defined _WIN32

#  include <stdlib.h>
#  include <sys/param.h>

#  if BYTE_ORDER == LITTLE_ENDIAN

#    define htobe16(x) _byteswap_ushort(x)
#    define htole16(x) ((uint16_t) (x))
#    define be16toh(x) _byteswap_ushort(x)
#    define le16toh(x) ((uint16_t) (x))

#    define htobe32(x) _byteswap_ulong(x)
#    define htole32(x) ((uint32_t) (x))
#    define be32toh(x) _byteswap_ulong(x)
#    define le32toh(x) ((uint32_t) (x))

#    define htobe64(x) _byteswap_uint64(x)
#    define htole64(x) ((uint64_t) (x))
#    define be64toh(x) _byteswap_uint64(x)
#    define le64toh(x) ((uint64_t) (x))

#  elif BYTE_ORDER == BIG_ENDIAN

     /* that would be xbox 360 */
#    define htobe16(x) ((uint16_t) (x))
#    define htole16(x) _byteswap_ushort(x)
#    define be16toh(x) ((uint16_t) (x))
#    define le16toh(x) _byteswap_ushort(x)

#    define htobe32(x) ((uint32_t) (x))
#    define htole32(x) _byteswap_ulong(x)
#    define be32toh(x) ((uint32_t) (x))
#    define le32toh(x) _byteswap_ulong(x)

#    define htobe64(x) ((uint64_t) (x))
#    define htole64(x) _byteswap_uint64(x)
#    define be64toh(x) ((uint64_t) (x))
#    define le64toh(x) _byteswap_uint64(x)

#  else

#    error byte order not supported

#  endif

#elif defined __APPLE__

#  include <libkern/OSByteOrder.h>

#  define htobe16(x) OSSwapHostToBigInt16(x)
#  define htole16(x) OSSwapHostToLittleInt16(x)
#  define be16toh(x) OSSwapBigToHostInt16(x)
#  define le16toh(x) OSSwapLittleToHostInt16(x)

#  define htobe32(x) OSSwapHostToBigInt32(x)
#  define htole32(x) OSSwapHostToLittleInt32(x)
#  define be32toh(x) OSSwapBigToHostInt32(x)
#  define le32toh(x) OSSwapLittleToHostInt32(x)

#  define htobe64(x) OSSwapHostToBigInt64(x)
#  define htole64(x) OSSwapHostToLittleInt64(x)
#  define be64toh(x) OSSwapBigToHostInt64(x)
#  define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined __sun

#  include <sys/byteorder.h>

#  define htobe16(x) BE_16(x)
#  define htole16(x) LE_16(x)
#  define be16toh(x) BE_16(x)
#  define le16toh(x) LE_16(x)

#  define htobe32(x) BE_32(x)
#  define htole32(x) LE_32(x)
#  define be32toh(x) BE_32(x)
#  define le32toh(x) LE_32(x)

#  define htobe64(x) BE_64(x)
#  define htole64(x) LE_64(x)
#  define be64toh(x) BE_64(x)
#  define le64toh(x) LE_64(x)

#else

#  ifndef HAVE_ENDIAN_H
#    ifdef __has_include
#      if __has_include(<endian.h>)
#        define HAVE_ENDIAN_H 1
#      endif
#    elif defined __linux__ || defined __CYGWIN__ || defined __HAIKU__
#      define HAVE_ENDIAN_H 1
#    endif
#  endif

#  ifndef HAVE_SYS_ENDIAN_H
#    ifdef __has_include
#      if __has_include(<sys/endian.h>)
#        define HAVE_SYS_ENDIAN_H 1
#      endif
#    elif defined __OpenBSD__ || defined __NetBSD__ || defined __FreeBSD__ || \
          defined __DragonFly__
#      define HAVE_SYS_ENDIAN_H 1
#    endif
#  endif

#  if defined HAVE_ENDIAN_H || defined HAVE_SYS_ENDIAN_H
#    ifndef _BSD_SOURCE
#      define _BSD_SOURCE
#    endif
#    ifndef __USE_BSD
#      define __USE_BSD
#    endif
#    ifndef _DEFAULT_SOURCE
#      define _DEFAULT_SOURCE
#    endif

#    ifdef HAVE_ENDIAN_H
#      include <endian.h>
#    endif
#    ifdef HAVE_SYS_ENDIAN_H
#      include <sys/endian.h>
#    endif
#  endif

#endif


/* fallbacks */

#ifndef LITTLE_ENDIAN
#  if defined __LITTLE_ENDIAN__
#    define LITTLE_ENDIAN __LITTLE_ENDIAN__
#  elif defined __LITTLE_ENDIAN
#    define LITTLE_ENDIAN __LITTLE_ENDIAN
#  else
#    define LITTLE_ENDIAN 1234
#  endif
#endif
#ifndef BIG_ENDIAN
#  if defined __BIG_ENDIAN__
#    define BIG_ENDIAN __BIG_ENDIAN__
#  elif defined __BIG_ENDIAN
#    define BIG_ENDIAN __BIG_ENDIAN
#  else
#    define BIG_ENDIAN 4321
#  endif
#endif
#ifndef PDP_ENDIAN
#  if defined __PDP_ENDIAN__
#    define PDP_ENDIAN __PDP_ENDIAN__
#  elif defined __PDP_ENDIAN
#    define PDP_ENDIAN __PDP_ENDIAN
#  else
#    define PDP_ENDIAN 3412
#  endif
#endif
#ifndef BYTE_ORDER
#  if defined __BYTE_ORDER__
#    define BYTE_ORDER __BYTE_ORDER__
#  elif defined __BYTE_ORDER
#    define BYTE_ORDER __BYTE_ORDER
#  else
#    error byte order unknown
#  endif
#endif

#if !defined be16toh || !defined le16toh || \
    !defined be32toh || !defined le32toh || \
    !defined be64toh || !defined le64toh
#  include <stdint.h>

#  ifndef HAVE_BYTESWAP_H
#    ifdef __has_include
#      if __has_include(<byteswap.h>)
#        define HAVE_BYTESWAP_H
#      endif
#    endif
#  endif

#  ifdef HAVE_BYTESWAP_H
#    include <byteswap.h>
#  endif

#  ifndef __GNUC_PREREQ
#    if defined __GNUC__ && defined __GNUC_MINOR__
#      define __GNUC_PREREQ(maj, min) \
          ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#    else
#       define __GNUC_PREREQ(maj, min) 0
#    endif
#  endif
#endif

#if (!defined be16toh || !defined le16toh) && !defined bswap_16
#  if __GNUC_PREREQ(4, 8)
#    define bswap_16 __builtin_bswap16
#  elif defined __has_builtin
#    if __has_builtin(__builtin_bswap16)
#      define bswap_16 __builtin_bswap16
#    endif
#  endif
#  ifndef bswap_16

static inline uint16_t __bswap_16 (uint16_t x) {
  return (uint16_t) (((x >> 8) & 0xff) | ((x & 0xff) << 8));
}

#    define bswap_16(x) __bswap_16(x)
#  endif
#endif
#ifndef be16toh
#  if defined betoh16
#    define be16toh(x) betoh16(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define be16toh(x) bswap_16(x)
#  elif BYTE_ORDER == BIG_ENDIAN
#    define be16toh(x) ((uint64_t) (x))
#  else
#    error byte order not supported
#  endif
#endif
#ifndef le16toh
#  if defined letoh16
#    define le16toh(x) letoh16(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define le16toh(x) ((uint64_t) (x))
#  elif BYTE_ORDER == BIG_ENDIAN
#    define le16toh(x) bswap_16(x)
#  else
#    error byte order not supported
#  endif
#endif
#ifndef htobe16
#  define htobe16(x) be16toh(x)
#endif
#ifndef htole16
#  define htole16(x) le16toh(x)
#endif


#if (!defined be32toh || !defined le32toh) && !defined bswap_32
#  if __GNUC_PREREQ(4, 3)
#    define bswap_32 __builtin_bswap32
#  elif defined __has_builtin
#    if __has_builtin(__builtin_bswap32)
#      define bswap_32 __builtin_bswap32
#    endif
#  endif
#  ifndef bswap_32

static inline uint32_t __bswap_32 (uint32_t x) {
  return (uint32_t) ( \
    ((x & 0xff000000u) >> 24) | \
    ((x & 0x00ff0000u) >> 8) | \
    ((x & 0x0000ff00u) << 8) | \
    ((x & 0x000000ffu) << 24));
}

#    define bswap_32(x) __bswap_32(x)
#  endif
#endif
#ifndef be32toh
#  if defined betoh32
#    define be32toh(x) betoh32(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define be32toh(x) bswap_32(x)
#  elif BYTE_ORDER == BIG_ENDIAN
#    define be32toh(x) ((uint64_t) (x))
#  else
#    error byte order not supported
#  endif
#endif
#ifndef le32toh
#  if defined letoh32
#    define le32toh(x) letoh32(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define le32toh(x) ((uint64_t) (x))
#  elif BYTE_ORDER == BIG_ENDIAN
#    define le32toh(x) bswap_32(x)
#  else
#    error byte order not supported
#  endif
#endif
#ifndef htobe32
#  define htobe32(x) be32toh(x)
#endif
#ifndef htole32
#  define htole32(x) le32toh(x)
#endif


#if (!defined be64toh || !defined le64toh) && !defined bswap_64
#  if __GNUC_PREREQ(4, 3)
#    define bswap_64 __builtin_bswap64
#  elif defined __has_builtin
#    if __has_builtin(__builtin_bswap64)
#      define bswap_64 __builtin_bswap64
#    endif
#  endif
#  ifndef bswap_64

static inline uint64_t __bswap_64 (uint64_t x) {
  return (uint64_t) ( \
    ((x & 0xff00000000000000ull) >> 56) | \
    ((x & 0x00ff000000000000ull) >> 40) | \
    ((x & 0x0000ff0000000000ull) >> 24) | \
    ((x & 0x000000ff00000000ull) >> 8) | \
    ((x & 0x00000000ff000000ull) << 8) | \
    ((x & 0x0000000000ff0000ull) << 24) | \
    ((x & 0x000000000000ff00ull) << 40) | \
    ((x & 0x00000000000000ffull) << 56));
}

#    define bswap_64(x) __bswap_64(x)
#  endif
#endif
#ifndef be64toh
#  if defined betoh64
#    define be64toh(x) betoh64(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define be64toh(x) bswap_64(x)
#  elif BYTE_ORDER == BIG_ENDIAN
#    define be64toh(x) ((uint64_t) (x))
#  else
#    error byte order not supported
#  endif
#endif
#ifndef le64toh
#  if defined letoh64
#    define le64toh(x) letoh64(x)
#  elif BYTE_ORDER == LITTLE_ENDIAN
#    define le64toh(x) ((uint64_t) (x))
#  elif BYTE_ORDER == BIG_ENDIAN
#    define le64toh(x) bswap_64(x)
#  else
#    error byte order not supported
#  endif
#endif
#ifndef htobe64
#  define htobe64(x) be64toh(x)
#endif
#ifndef htole64
#  define htole64(x) le64toh(x)
#endif


#endif /* PLZJ_PLATFORM_ENDIAN_H */
