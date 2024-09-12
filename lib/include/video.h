#ifndef PLZJ_VIDEO_H
#define PLZJ_VIDEO_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define _BSD_SOURCE

#include <endian.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
static inline void PlzjBuffer_destroy (struct PlzjBuffer *buf) {
  free(buf->data);
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
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

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 2))
static inline void PlzjRect_extend (
    struct PlzjRect *rect, const struct PlzjRect *rect2) {
  if (rect2->p1.x < rect->p1.x) {
    rect->p1.x = rect2->p1.x;
  }
  if (rect2->p1.y < rect->p1.y) {
    rect->p1.y = rect2->p1.y;
  }
  if (rect2->p2.x > rect->p2.x) {
    rect->p2.x = rect2->p2.x;
  }
  if (rect2->p2.y > rect->p2.y) {
    rect->p2.y = rect2->p2.y;
  }
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


struct PlzjCursor {
  /// x: left, y: top
  struct PlzjPoint p;
  struct PlzjClick event;
  struct PlzjSegment seg;
  struct PlzjTaggedBuffer *tbuf;
};

__attribute_artificial__ __nonnull()
static inline bool PlzjCursor_valid (const struct PlzjCursor *cursor) {
  return cursor->p.x >= -32 && cursor->p.y >= -32;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjCursor_print (
  const struct PlzjCursor *cursor, FILE *out, off_t offset);

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PlzjCursor_init (
    struct PlzjCursor *cursor, const struct PlzjLxeCursor *h_cursor) {
  cursor->p.x = le32toh(h_cursor->left);
  cursor->p.y = le32toh(h_cursor->top);
  cursor->event = (struct PlzjClick) {};
  cursor->seg.size = le32toh(h_cursor->size);
  cursor->seg.offset = -1;
  cursor->tbuf = NULL;
  return 0;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
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


struct PlzjImage {
  /// p1.x: left, p1.y: top, p2.x: right, p2.y: bottom
  struct PlzjRect rect;
  struct PlzjSegment seg;
  struct PlzjBuffer buf;
};

DLL_PUBLIC __THROW __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
bool PlzjImage_valid (
  const struct PlzjImage *image, uint32_t width, uint32_t height);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PlzjImage_apply (
  const struct PlzjImage *image, struct PlzjColor *canvas,
  uint32_t width, uint32_t height);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjImage_print (const struct PlzjImage *image, FILE *out, off_t offset);
DLL_PUBLIC __THROW __nonnull((1, 2)) __attr_access((__read_only__, 4))
int PlzjImage_read (
  struct PlzjImage *image, FILE *file, unsigned int type, const void *key);

__attribute_artificial__ __nonnull()
static inline void PlzjImage_destroy (struct PlzjImage *image) {
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
  image->buf = (struct PlzjBuffer) {};
  return 0;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
int PlzjImage_init_file (struct PlzjImage *image, FILE *file);


struct PlzjFrame {
  struct PlzjCursor cursor;
  struct PlzjImage **patches;
  size_t patches_cnt;
};

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PlzjFrame_apply (
  const struct PlzjFrame *frame, struct PlzjColor *canvas,
  uint32_t width, uint32_t height);

__attribute_artificial__ __nonnull()
static inline void PlzjFrame_destroy (struct PlzjFrame *frame) {
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


struct PlzjTaggedBuffer {
  unsigned long tag;
  struct PlzjBuffer buf;
};

__attribute_artificial__ __nonnull()
static inline void PlzjTaggedBuffer_destroy (
    struct PlzjTaggedBuffer *tbuf) {
  PlzjBuffer_destroy(&tbuf->buf);
}


struct PlzjVideo {
  struct PlzjFrame **frames;
  size_t frames_cnt;
  struct PlzjTaggedBuffer **cursors;
  size_t cursors_cnt;

  const struct Plzj *pl;
  uint32_t frame_ms;
};

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int PlzjVideo_write_apng (
  const struct PlzjVideo *video, FILE *out, unsigned int flags,
  unsigned int transitions_cnt, int compression_level, unsigned int nproc);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_save_apng (
  const struct PlzjVideo *video, const char *path, unsigned int flags,
  unsigned int transitions_cnt, int compression_level, unsigned int nproc);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_save_cursors (const struct PlzjVideo *video, const char *dir);
DLL_PUBLIC __THROW __nonnull()
int PlzjVideo_read_cursors (
  struct PlzjVideo *video, FILE *in, struct PlzjTaggedBuffer **tbufp);

__attribute_artificial__ __nonnull()
static inline void PlzjVideo_destroy (struct PlzjVideo *video) {
  for (size_t i = 0; i < video->cursors_cnt; i++) {
    PlzjTaggedBuffer_destroy(video->cursors[i]);
    free(video->cursors[i]);
  }
  free(video->cursors);
  for (size_t i = 0; i < video->frames_cnt; i++) {
    PlzjFrame_destroy(video->frames[i]);
    free(video->frames[i]);
  }
  free(video->frames);
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PlzjVideo_init (
  struct PlzjVideo *video, const struct Plzj *pl, int32_t frames_limit,
  bool read_cursor);


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_VIDEO_H */
