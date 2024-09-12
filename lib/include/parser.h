#ifndef PMLXZJ_PARSER_H
#define PMLXZJ_PARSER_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "defs.h"
#include "alg.h"
#include "serialized.h"


struct Pmlxzj {
  FILE *file;
  off_t file_size;

  /// = 0 unencrypted, > 0 key set, < 0 key unknown
  signed char encrypted;
  /// encryption key (only valid if encrypted > 0)
  unsigned char key[20];

  bool audio_new_format;
  /// offset to video data (including header)
  off_t video_offset;
  /// offset to audio data (including header)
  off_t audio_offset;

  /// offset to keyframes txt
  off_t keyframes_offset;
  uint32_t keyframes_size;

  /// offset to clicks txt
  off_t clicks_offset;
  uint32_t clicks_size;

  alignas(4) struct PmlxzjLxeVideo video;
  alignas(4) struct PmlxzjLxePlayer player;
  alignas(4) struct PmlxzjLxeFooter footer;
};

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2, 3))
size_t Pmlxzj_get_title (const struct Pmlxzj *pl, char *dst, size_t size);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2, 3))
size_t Pmlxzj_get_infotext (const struct Pmlxzj *pl, char *dst, size_t size);
DLL_PUBLIC __THROW __wur __attribute_pure__ __nonnull()
__attr_access((__read_only__, 1))
bool Pmlxzj_is_registered (const struct Pmlxzj *pl);

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 2))
int Pmlxzj_set_password (struct Pmlxzj *pl, const char *password, bool force);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 2))
int Pmlxzj_set_password_iconv (
  struct Pmlxzj *pl, const char *password, bool force);

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Pmlxzj_extract_audio (const struct Pmlxzj *pl, const char *dir);

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Pmlxzj_extract_video_or_cursor (
  const struct Pmlxzj *pl, const char *dir, int32_t frames_limit,
  unsigned int flags, unsigned int frames_k, unsigned int nproc,
  bool extract_video, bool extract_cursor);

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
static inline int Pmlxzj_extract_video (
    const struct Pmlxzj *pl, const char *dir, int32_t frames_limit,
    unsigned int flags, unsigned int frames_k, unsigned int nproc) {
  return Pmlxzj_extract_video_or_cursor(
    pl, dir, frames_limit, flags, frames_k, nproc, true, false);
}

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
static inline int Pmlxzj_extract_cursor (
    const struct Pmlxzj *pl, const char *dir, int32_t frames_limit) {
  return Pmlxzj_extract_video_or_cursor(
    pl, dir, frames_limit, 0, 0, 0, false, true);
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Pmlxzj_extract_txts (const struct Pmlxzj *pl, const char *dir);

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int Pmlxzj_print_info (const struct Pmlxzj *pl, FILE *out);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int Pmlxzj_print_video (
  const struct Pmlxzj *pl, FILE *out, int32_t frames_limit);
DLL_PUBLIC __THROW __nonnull((1)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Pmlxzj_set_playlock (const struct Pmlxzj *pl, const char *key);
DLL_PUBLIC __THROW __nonnull((1)) __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Pmlxzj_set_playlock_iconv (const struct Pmlxzj *pl, const char *password);

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
void Pmlxzj_destroy (struct Pmlxzj *pl);
DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
int Pmlxzj_init (struct Pmlxzj *pl, FILE *f);
DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
int Pmlxzj_init_file (struct Pmlxzj *pl, const char *path, const char *mode);


#ifdef __cplusplus
}
#endif

#endif /* PMLXZJ_PARSER_H */
