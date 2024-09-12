#ifndef PLZJ_VIDEO_H
#define PLZJ_VIDEO_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform/endian.h"

#include "defs.h"
#include "parser.h"
#include "structs.h"


struct PlzjSegment {
  size_t size;
  off_t offset;
};


struct PlzjBuffer {
  size_t size;
  void *data;
};

__attribute_artificial__ __nonnull()
static inline void PlzjBuffer_destroy (const struct PlzjBuffer *buf) {
  free(buf->data);
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
int PlzjBuffer_init_file (
  struct PlzjBuffer *buf, FILE *file, size_t size, off_t offset);

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PlzjBuffer_init_file_seg (
    struct PlzjBuffer *buf, FILE *file, const struct PlzjSegment *seg) {
  return PlzjBuffer_init_file(buf, file, seg->size, seg->offset);
}


struct PlzjPoint {
  int32_t x;
  int32_t y;
};


struct PlzjRect {
  struct PlzjPoint p1;
  struct PlzjPoint p2;
};

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline bool PlzjRect_inbox (
    const struct PlzjRect *rect, uint32_t width, uint32_t height) {
  return
    rect->p1.x >= 0 && rect->p1.y >= 0 &&
    (width >= INT32_MAX || rect->p2.x <= (int32_t) width) &&
    (height >= INT32_MAX || rect->p2.y <= (int32_t) height);
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline bool PlzjRect_valid (const struct PlzjRect *rect) {
  return rect->p1.x <= rect->p2.x && rect->p1.y <= rect->p2.y;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PlzjRect_width (const struct PlzjRect *rect) {
  return rect->p2.x - rect->p1.x;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PlzjRect_height (const struct PlzjRect *rect) {
  return rect->p2.y - rect->p1.y;
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
static inline void PlzjRect_add (
    struct PlzjRect *rect, const struct PlzjRect *a, const struct PlzjRect *b) {
  *rect = (struct PlzjRect) {
    {plzj_min(a->p1.x, b->p1.x), plzj_min(a->p1.y, b->p1.y)},
    {plzj_max(a->p2.x, b->p2.x), plzj_max(a->p2.y, b->p2.y)}
  };
}

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 2))
static inline void PlzjRect_iadd (
    struct PlzjRect *rect, const struct PlzjRect *other) {
  PlzjRect_add(rect, rect, other);
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
static inline void PlzjRect_clamp (
    struct PlzjRect *rect, const struct PlzjRect *r, const struct PlzjRect *u) {
  *rect = (struct PlzjRect) {
    {plzj_clamp(r->p1.x, u->p1.x, u->p2.x),
     plzj_clamp(r->p1.y, u->p1.y, u->p2.y)},
    {plzj_clamp(r->p2.x, u->p1.x, u->p2.x),
     plzj_clamp(r->p2.y, u->p1.y, u->p2.y)}
  };
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
static inline int PlzjRect_init_box (
    struct PlzjRect *rect, int32_t width, int32_t height) {
  *rect = (struct PlzjRect) {{0, 0}, {width, height}};
  return 0;
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
static inline int PlzjRect_init (struct PlzjRect *rect) {
  *rect = (struct PlzjRect) {{INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MIN}};
  return 0;
}


struct PlzjClick {
  unsigned int type;
  /// x: left, y: top
  struct PlzjPoint p;
};


struct PlzjCursorRes {
  struct PlzjBuffer buf;
  unsigned long tag;
  bool gray;
};


struct PlzjCursor {
  /// x: left, y: top
  struct PlzjPoint p;
  struct PlzjClick event;
  struct PlzjSegment seg;
  struct PlzjCursorRes *curres;
};

__attribute_artificial__ __nonnull()
static inline bool PlzjCursor_valid (const struct PlzjCursor *cursor) {
  return cursor->p.x >= -32 && cursor->p.y >= -32;
}

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjCursor_print (
  const struct PlzjCursor *cursor, FILE *out, off_t offset);

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PlzjCursor_init (
    struct PlzjCursor *cursor, const struct PlzjLxeCursor *h_cursor) {
  cursor->p.x = le32toh(h_cursor->left);
  cursor->p.y = le32toh(h_cursor->top);
  cursor->event = (struct PlzjClick) {0};
  cursor->seg.size = le32toh(h_cursor->size);
  cursor->seg.offset = -1;
  cursor->curres = NULL;
  return 0;
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
int PlzjCursor_init_file (struct PlzjCursor *cursor, FILE *file);


struct PlzjColor {
  union {
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t a;
    };
    uint8_t values[4];
    uint32_t color;
  };
};
static_assert(sizeof(struct PlzjColor) == 4);


struct PlzjCanvas {
  struct PlzjColor *pixels;
  uint32_t width;
  uint32_t height;
};


struct PlzjImage {
  /// p1.x: left, p1.y: top, p2.x: right, p2.y: bottom
  struct PlzjRect rect;
  struct PlzjSegment seg;
  struct PlzjBuffer buf;
};

PLZJ_API __THROW __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
bool PlzjImage_valid (
  const struct PlzjImage *image, uint32_t width, uint32_t height);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjImage_apply (const struct PlzjImage *image, struct PlzjCanvas *canvas);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjImage_print (const struct PlzjImage *image, FILE *out, off_t offset);
PLZJ_API __THROW __nonnull((1, 2)) __attr_access((__read_only__, 4))
int PlzjImage_read (
  struct PlzjImage *image, FILE *file, unsigned int type, const void *key,
  bool temp_use);

__attribute_artificial__ __nonnull()
static inline void PlzjImage_destroy (const struct PlzjImage *image) {
  PlzjBuffer_destroy(&image->buf);
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PlzjImage_init (
    struct PlzjImage *image, const struct PlzjLxeImage *h_image) {
  image->rect.p1.x = le32toh(h_image->left);
  image->rect.p1.y = le32toh(h_image->top);
  image->rect.p2.x = le32toh(h_image->right);
  image->rect.p2.y = le32toh(h_image->bottom);
  image->seg.size = le32toh(h_image->size);
  image->seg.offset = -1;
  image->buf = (struct PlzjBuffer) {0};
  return 0;
}

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
int PlzjImage_init_file (struct PlzjImage *image, FILE *file);


struct PlzjFrame {
  struct PlzjCursor cursor;
  struct PlzjImage **patches;
  size_t patches_cnt;
};

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjFrame_apply (const struct PlzjFrame *frame, struct PlzjCanvas *canvas);

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 1))
static inline void PlzjFrame_destroy (const struct PlzjFrame *frame) {
  for (size_t i = 0; i < frame->patches_cnt; i++) {
    PlzjImage_destroy(frame->patches[i]);
    free(frame->patches[i]);
  }
  free(frame->patches);
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
static inline int PlzjFrame_init (struct PlzjFrame *frame) {
  frame->cursor = (struct PlzjCursor) {.p = {INT32_MIN, INT32_MIN}};
  frame->patches = NULL;
  frame->patches_cnt = 0;
  return 0;
}


struct PlzjVideo {
  struct PlzjFrame **frames;
  size_t frames_cnt;
  struct PlzjCursorRes **curreses;
  size_t curreses_cnt;

  const struct Plzj *pl;
  uint32_t frame_ms;
};

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjVideo_write_apng (
  const struct PlzjVideo *video, FILE *out, unsigned int flags,
  unsigned int transitions_cnt, int compression_level, unsigned int nproc);
PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_save_apng (
  const struct PlzjVideo *video, const char *path, unsigned int flags,
  unsigned int transitions_cnt, int compression_level, unsigned int nproc);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_save_cursors (const struct PlzjVideo *video, const char *dir);
PLZJ_API __THROW __nonnull()
int PlzjVideo_read_cursors (struct PlzjVideo *video, FILE *in);

PLZJ_API __THROW __nonnull() __attr_access((__read_only__, 1))
void PlzjVideo_destroy (const struct PlzjVideo *video);
PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_init (
  struct PlzjVideo *video, const struct Plzj *pl, int32_t frames_limit,
  bool read_cursor);


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_VIDEO_H */
