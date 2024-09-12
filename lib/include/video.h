#ifndef PMLXZJ_VIDEO_H
#define PMLXZJ_VIDEO_H 1

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
#include "serialized.h"


struct PmlxzjSegment {
  size_t size;
  off_t offset;
};


struct PmlxzjBuffer {
  size_t size;
  void *data;
};

__attribute_artificial__ __nonnull()
static inline void PmlxzjBuffer_destroy (struct PmlxzjBuffer *buf) {
  free(buf->data);
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjBuffer_init_file (
  struct PmlxzjBuffer *buf, FILE *file, size_t size, off_t offset);

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PmlxzjBuffer_init_file_seg (
    struct PmlxzjBuffer *buf, FILE *file, const struct PmlxzjSegment *seg) {
  return PmlxzjBuffer_init_file(buf, file, seg->size, seg->offset);
}


struct PmlxzjPoint {
  int32_t x;
  int32_t y;
};


struct PmlxzjRect {
  struct PmlxzjPoint p1;
  struct PmlxzjPoint p2;
};

__attribute_artificial__ __wur __attribute_pure__ __nonnull()
__attr_access((__read_only__, 1))
static inline bool PmlxzjRect_valid (const struct PmlxzjRect *rect) {
  return rect->p1.x <= rect->p2.x && rect->p1.y <= rect->p2.y;
}

__attribute_artificial__ __wur __attribute_pure__ __nonnull()
__attr_access((__read_only__, 1))
static inline uint32_t PmlxzjRect_width (const struct PmlxzjRect *rect) {
  return rect->p2.x - rect->p1.x;
}

__attribute_artificial__ __wur __attribute_pure__ __nonnull()
__attr_access((__read_only__, 1))
static inline uint32_t PmlxzjRect_height (const struct PmlxzjRect *rect) {
  return rect->p2.y - rect->p1.y;
}

__attribute_artificial__ __nonnull() __attr_access((__read_only__, 2))
static inline void PmlxzjRect_extend (
    struct PmlxzjRect *rect, const struct PmlxzjRect *rect2) {
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
static inline int PmlxzjRect_init (struct PmlxzjRect *rect) {
  *rect = (struct PmlxzjRect) {
    {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MIN}};
  return 0;
}


struct PmlxzjClick {
  unsigned int type;
  /// x: left, y: top
  struct PmlxzjPoint p;
};


struct PmlxzjCursor {
  /// x: left, y: top
  struct PmlxzjPoint p;
  struct PmlxzjClick event;
  struct PmlxzjSegment seg;
  struct PmlxzjTaggedBuffer *tbuf;
};

__attribute_artificial__ __nonnull()
static inline bool PmlxzjCursor_valid (const struct PmlxzjCursor *cursor) {
  return cursor->p.x >= -32 && cursor->p.y >= -32;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int PmlxzjCursor_print (
  const struct PmlxzjCursor *cursor, FILE *out, off_t offset);

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PmlxzjCursor_init (
    struct PmlxzjCursor *cursor, const struct PmlxzjLxeCursor *h_cursor) {
  cursor->p.x = le32toh(h_cursor->left);
  cursor->p.y = le32toh(h_cursor->top);
  cursor->event = (struct PmlxzjClick) {};
  cursor->seg.size = le32toh(h_cursor->size);
  cursor->seg.offset = -1;
  cursor->tbuf = NULL;
  return 0;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjCursor_init_file (struct PmlxzjCursor *cursor, FILE *file);


struct PmlxzjImage {
  /// p1.x: left, p1.y: top, p2.x: right, p2.y: bottom
  struct PmlxzjRect rect;
  struct PmlxzjSegment seg;
  struct PmlxzjBuffer buf;
};

DLL_PUBLIC __THROW __wur __attribute_pure__ __nonnull()
__attr_access((__read_only__, 1))
bool PmlxzjImage_valid (
  const struct PmlxzjImage *image, uint32_t width, uint32_t height);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PmlxzjImage_apply (
  const struct PmlxzjImage *image, unsigned char *canvas,
  uint32_t width, uint32_t height);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
int PmlxzjImage_print (
  const struct PmlxzjImage *image, FILE *out, off_t offset);
DLL_PUBLIC __THROW __nonnull((1, 2)) __attr_access((__read_only__, 4))
int PmlxzjImage_read (
  struct PmlxzjImage *image, FILE *file, unsigned int type, const void *key);

__attribute_artificial__ __nonnull()
static inline void PmlxzjImage_destroy (struct PmlxzjImage *image) {
  PmlxzjBuffer_destroy(&image->buf);
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
static inline int PmlxzjImage_init (
    struct PmlxzjImage *image, const struct PmlxzjLxeImage *h_image) {
  image->rect.p1.x = le32toh(h_image->left);
  image->rect.p1.y = le32toh(h_image->top);
  image->rect.p2.x = le32toh(h_image->right);
  image->rect.p2.y = le32toh(h_image->bottom);
  image->seg.size = le32toh(h_image->size);
  image->seg.offset = -1;
  image->buf = (struct PmlxzjBuffer) {};
  return 0;
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjImage_init_file (struct PmlxzjImage *image, FILE *file);


struct PmlxzjFrame {
  struct PmlxzjCursor cursor;
  struct PmlxzjImage **patches;
  uint16_t patches_cnt;
};

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PmlxzjFrame_apply (
  const struct PmlxzjFrame *frame, unsigned char *canvas,
  uint32_t width, uint32_t height);
DLL_PUBLIC __THROW __nonnull()
struct PmlxzjImage *PmlxzjFrame_new_patch (struct PmlxzjFrame *frame);

__attribute_artificial__ __nonnull()
static inline void PmlxzjFrame_destroy (struct PmlxzjFrame *frame) {
  for (size_t i = 0; i < frame->patches_cnt; i++) {
    PmlxzjImage_destroy(frame->patches[i]);
  }
  free(frame->patches);
}

__attribute_artificial__ __nonnull() __attr_access((__write_only__, 1))
static inline int PmlxzjFrame_init (struct PmlxzjFrame *frame) {
  frame->cursor = (struct PmlxzjCursor) {.p = {-1000, -1000}};
  frame->patches = NULL;
  frame->patches_cnt = 0;
  return 0;
}


struct PmlxzjTaggedBuffer {
  unsigned long tag;
  struct PmlxzjBuffer buf;
};

__attribute_artificial__ __nonnull()
static inline void PmlxzjTaggedBuffer_destroy (
    struct PmlxzjTaggedBuffer *tbuf) {
  PmlxzjBuffer_destroy(&tbuf->buf);
}


struct PmlxzjVideo {
  struct PmlxzjFrame **frames;
  size_t frames_cnt;
  struct PmlxzjTaggedBuffer **cursors;
  size_t cursors_cnt;

  uint32_t frame_ms;
};

DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjVideo_write_apng (
  const struct PmlxzjVideo *video, const struct Pmlxzj *pl, FILE *out,
  unsigned int flags, unsigned int frames_k, unsigned int nproc);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
int PmlxzjVideo_save_apng (
  const struct PmlxzjVideo *video, const struct Pmlxzj *pl, const char *path,
  unsigned int flags, unsigned int frames_k, unsigned int nproc);
DLL_PUBLIC __THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjVideo_save_cursors (
  const struct PmlxzjVideo *video, const char *dir);
DLL_PUBLIC __THROW __nonnull()
int PmlxzjVideo_read_cursors (
  struct PmlxzjVideo *video, FILE *in, struct PmlxzjTaggedBuffer **tbufp);

__attribute_artificial__ __nonnull()
static inline void PmlxzjVideo_destroy (struct PmlxzjVideo *video) {
  for (size_t i = 0; i < video->cursors_cnt; i++) {
    PmlxzjTaggedBuffer_destroy(video->cursors[i]);
  }
  free(video->cursors);
  for (size_t i = 0; i < video->frames_cnt; i++) {
    PmlxzjFrame_destroy(video->frames[i]);
  }
  free(video->frames);
}

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjVideo_init (
  struct PmlxzjVideo *video, const struct Pmlxzj *pl,
  int32_t frames_limit, bool read_cursor);


#ifdef __cplusplus
}
#endif

#endif /* PMLXZJ_VIDEO_H */
