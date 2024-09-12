#ifndef PLZJ_ALG_H
#define PLZJ_ALG_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "defs.h"


PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3, 4))
/**
 * @brief Convert plzj character encoding to local encoding, insuring null
 * termination.
 *
 * @note Be sure to call <tt>setlocale(LC_ALL, "")</tt> before using this
 * function.
 *
 * @param[out] dst Destination buffer.
 * @param dstsize Size of @p dst , including space for terminating null.
 * @param src Source string.
 * @param srclen Length of @p src , not including terminating null (ideally
 *   @c strlen(src) ).
 *
 * @return Length of converted string, not including terminating null (ideally
 *   @c strlen(dst) ). A null character is always appended at the end of
 *   @p dst .
 */
size_t plzj_iconv_dec (
  char *dst, size_t dstsize, const char *src, size_t srclen);
PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3, 4))
/**
 * @brief Convert local character encoding to plzj encoding, insuring null
 * termination.
 *
 * @note Be sure to call <tt>setlocale(LC_ALL, "")</tt> before using this
 * f  unction.
 *
 * @param[out] dst Destination buffer.
 * @param dstsize Size of @p dst , including space for terminating null.
 * @param src Source string.
 * @param srclen Length of @p src , not including terminating null (ideally
 *   @c strlen(src) ).
 *
 * @return Length of converted string, not including terminating null (ideally
 *   @c strlen(dst) ). A null character is always appended at the end of
 *   @p dst .
 */
size_t plzj_iconv_enc (
  char *dst, size_t dstsize, const char *src, size_t srclen);
PLZJ_API __THROW __attribute_malloc__ __nonnull()
__attr_access((__read_only__, 1, 2))
char *plzj_iconv_enc_new (const char *src, size_t srclen);


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
/**
 * @brief Calculate password checksum.
 *
 * See also https://www.52pojie.cn/thread-583714-1-1.html .
 *
 * @param password Input password, null-terminated.
 * @return Password checksum.
 */
static inline unsigned long plzj_password_cksum(const void *password) {
  const unsigned char *password_ = password;
  unsigned long cksum = 2005;
  for (size_t i = 0; password_[i] != '\0'; i++) {
    cksum += password_[i] * (i + i / 5 + 1);
  }
  return cksum;
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
/**
 * @brief Convert password to plzj encryption key.
 *
 * @param[out] key Output encryption key. Must be at least 20 bytes long.
 * @param password Input password. Must be at least 21 bytes long. However, the
 *   first byte is ignored.
 */
static inline void plzj_key_enc (void *key, const void *password) {
  for (unsigned int i = 0; i < 20; i++) {
    ((unsigned char *) key)[i] = ((const unsigned char *) password)[20 - i];
  }
}


__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
/**
 * @brief Encode / decode plzj info text.
 *
 * Ref: @c lxefileplay::infotextjm()
 *
 * @param[out] dst Output buffer. Must be at least 40 bytes long.
 * @param src Input buffer. Must be at least 40 bytes long.
 */
static inline void plzj_infotext_encdec (void *dst, const void *src) {
  for (unsigned int i = 0; i < 40; i++) {
    ((unsigned char *) dst)[i] =
      ((const unsigned char *) src)[i] ^ (100 - 4 * i);
  }
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1, 2))
__attr_access((__read_only__, 3))
size_t plzj_infotext_dec_iconv (char *dst, size_t dstsize, const char *src);
PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2, 3))
size_t plzj_infotext_enc_iconv (char *dst, const char *src, size_t srclen);


__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline long plzj_regcode_calc (const char *regcode1) {
  long code1 = -100;
  for (size_t i = 0; regcode1[i] != '\0'; i++) {
    code1 += regcode1[i];
  }
  code1 /= 1.5432;
  code1 += 1234;
  code1 *= 3121.1415926;
  return code1;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline long plzj_regcode_dec (const char *regcode2) {
  long code2 = 0;
  for (size_t i = 0; regcode2[i] != '\0'; i++) {
    char s = regcode2[i] - 20 - 10 * (i % 2) + i / 3;
    if (s < '0' || s > '9') {
      return -1;
    }
    code2 *= 10;
    code2 += s - '0';
  }
  code2 /= 124;
  return code2;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1)) __attr_access((__read_only__, 2))
static inline bool plzj_regcode_check (
    const char *regcode1, const char *regcode2) {
  return plzj_regcode_calc(regcode1) == plzj_regcode_dec(regcode2);
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
/**
 * @brief Get string of code 2.
 *
 * @note Do not use @c plzj_regcode_enc() to check against @p regcode2 . Check
 *   @c plzj_regcode_dec(regcode2) against @p code2 instead.
 *
 * @param[out] regcode2 Output buffer. Must be able to hold an integer (at least
 *   11 bytes long).
 * @param code2 Code 2. Ideally @c plzj_regcode_calc(regcode1) .
 * @return @c strlen(regcode2) .
 */
unsigned int plzj_regcode_enc (char *regcode2, long code2);


__attribute_artificial__ __nonnull((1)) __attr_access((__read_write__, 1, 2))
__attr_access((__read_only__, 3))
static inline bool plzj_image_encdec (void *buf, size_t len, const void *key) {
  if (key == NULL || len <= 10240) {
    return false;
  }

  unsigned char *head = (unsigned char *) buf + 4;
  unsigned char *encrypted = (unsigned char *) buf + len / 2;
  for (unsigned int i = 0; i < 20; i++) {
    encrypted[i] ^= head[i] ^ ((const unsigned char *) key)[i];
  }

  return true;
}


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_ALG_H */
