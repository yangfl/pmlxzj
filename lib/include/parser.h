#ifndef PLZJ_PARSER_H
#define PLZJ_PARSER_H 1

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
#include "structs.h"


struct Plzj {
  FILE *file;

  /// offset to begin of the section
  off_t begin_offset;
  /// offset to end of the section
  off_t end_offset;

  /// = 0 unencrypted, > 0 key set, < 0 key required but not set
  signed char key_set;
  /// encryption key (only valid if `Plzj::key_set > 0`)
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

  alignas(4) struct PlzjLxeVideo video;
  alignas(4) struct PlzjLxePlayer player;
  alignas(4) struct PlzjLxeFooter footer;
  alignas(4) struct PlzjLxeSection section;
};

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2, 3))
size_t Plzj_get_caption (const struct Plzj *pl, char *dst, size_t size);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2, 3))
size_t Plzj_get_title (const struct Plzj *pl, char *dst, size_t size);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2, 3))
size_t Plzj_get_infotext (const struct Plzj *pl, char *dst, size_t size);
PLZJ_API __THROW __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
bool Plzj_is_registered (const struct Plzj *pl);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 2))
int Plzj_set_password (struct Plzj *pl, const char *password, bool force);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 2))
int Plzj_set_password_iconv (struct Plzj *pl, const char *password, bool force);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Plzj_extract_audio (const struct Plzj *pl, const char *dir);

struct PlzjVideoExtractOptions {
  int32_t frames_limit;
  unsigned int transitions_cnt;
  int compression_level;
  unsigned int nproc;
};

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Plzj_extract_video_or_cursor (
  const struct Plzj *pl, const char *dir, int32_t frames_limit,
  unsigned int flags, unsigned int transitions_cnt, int compression_level,
  unsigned int nproc, bool extract_video, bool extract_cursor);

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
static inline int Plzj_extract_video (
    const struct Plzj *pl, const char *dir, unsigned int flags,
  unsigned int transitions_cnt, int compression_level, unsigned int nproc) {
  return Plzj_extract_video_or_cursor(
    pl, dir, -1, flags, transitions_cnt, compression_level, nproc, true, false);
}

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
static inline int Plzj_extract_cursor (
    const struct Plzj *pl, const char *dir) {
  return Plzj_extract_video_or_cursor(pl, dir, -1, 0, 0, 0, 0, false, true);
}

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int Plzj_extract_txts (const struct Plzj *pl, const char *dir);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int Plzj_print_info (const struct Plzj *pl, FILE *out, bool in_section);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int Plzj_print_video (
  const struct Plzj *pl, FILE *out, int32_t frames_limit);

PLZJ_API __THROW __nonnull((1)) __attr_access((__read_only__, 2))
int Plzj_set_playlock (struct Plzj *pl, const char *password);
PLZJ_API __THROW __nonnull((1)) __attr_access((__read_only__, 2))
int Plzj_set_playlock_iconv (struct Plzj *pl, const char *password);

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
static inline void Plzj_destroy (const struct Plzj *pl) {
  (void) pl;
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 3))
int Plzj_init (
  struct Plzj *pl, FILE *file, const struct PlzjLxeSection *section);


struct PlzjFile {
  FILE *file;
  off_t file_size;

  uint32_t sections_cnt;
  struct Plzj *sections;

  bool extended;
  alignas(4) struct PlzjLxeFooterSectioned extfooter;
};

__attribute_warn_unused_result__ __nonnull() __attr_access((__read_only__, 1))
static inline bool PlzjFile_valid (const struct PlzjFile *pf) {
  return
    pf->sections_cnt >= 1 && pf->sections != NULL &&
    (pf->extended || pf->sections_cnt <= 1);
}

PLZJ_API __THROW __nonnull((1)) __attr_access((__read_only__, 2))
int PlzjFile_set_playlock (struct PlzjFile *pf, const char *password);
PLZJ_API __THROW __nonnull((1)) __attr_access((__read_only__, 2))
int PlzjFile_set_playlock_iconv (struct PlzjFile *pf, const char *password);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjFile_set_password (
  const struct PlzjFile *pf, const char *password, bool force);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjFile_set_password_iconv (
  const struct PlzjFile *pf, const char *password, bool force);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
void PlzjFile_destroy (const struct PlzjFile *pf);
PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
int PlzjFile_init (struct PlzjFile *pf, FILE *f);
PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
int PlzjFile_init_file (
  struct PlzjFile *pf, const char *path, const char *mode);


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_PARSER_H */
