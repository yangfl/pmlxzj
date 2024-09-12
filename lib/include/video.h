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


struct PmlxzjLxePacketIter {
  union PmlxzjLxePacket packet;
  /// current frame number
  int_least32_t frame_no;
  /// current packet number within this frame
  int_least32_t frame_packet_no;

  /// offset of current packet header
  off_t begin_offset;
  /// offset of current data stream (without length header)
  off_t offset;
  /// offset of next packet header
  off_t end_offset;

  /// number of frames to process
  int_least32_t frames_cnt;

  /// video width
  uint_least32_t width;
  /// video height
  uint_least32_t height;

  /// input file
  FILE *file;
};

__THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjLxePacketIter_init (
  struct PmlxzjLxePacketIter *iter, const struct Pmlxzj *pl,
  int_fast32_t frames_limit);

__THROW __wur __nonnull()
int PmlxzjLxePacketIter_next (struct PmlxzjLxePacketIter *iter);

#define PmlxzjLxePacketIter_NEXT_FRAME 0
#define PmlxzjLxePacketIter_NEXT_CURSOR 1
#define PmlxzjLxePacketIter_NEXT_IMAGE 2


struct PmlxzjSegment {
  size_t size;
  off_t offset;
};


struct PmlxzjBuffer {
  size_t size;
  void *data;
};

__nonnull()
static inline void PmlxzjBuffer_destroy (struct PmlxzjBuffer *buf) {
  free(buf->data);
}

__THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjBuffer_init (
  struct PmlxzjBuffer *buf, FILE *file, size_t size, off_t offset);
__THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjBuffer_init_seg (
  struct PmlxzjBuffer *buf, FILE *file, const struct PmlxzjSegment *seg);


struct PmlxzjPoint {
  int_least32_t x;
  int_least32_t y;
};


struct PmlxzjRect {
  struct PmlxzjPoint p1;
  struct PmlxzjPoint p2;
};

__wur __attribute_pure__ __nonnull() __attr_access((__read_only__, 1))
static inline bool PmlxzjRect_valid (const struct PmlxzjRect *rect) {
  return rect->p1.x <= rect->p2.x && rect->p1.y <= rect->p2.y;
}

__wur __attribute_pure__ __nonnull() __attr_access((__read_only__, 1))
static inline uint_fast32_t PmlxzjRect_width (const struct PmlxzjRect *rect) {
  return rect->p2.x - rect->p1.x;
}

__wur __attribute_pure__ __nonnull() __attr_access((__read_only__, 1))
static inline uint_fast32_t PmlxzjRect_height (const struct PmlxzjRect *rect) {
  return rect->p2.y - rect->p1.y;
}

__nonnull() __attr_access((__read_only__, 2))
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

__nonnull() __attr_access((__write_only__, 1))
static inline int PmlxzjRect_init (struct PmlxzjRect *rect) {
  *rect = (struct PmlxzjRect) {
    {INT_LEAST32_MAX, INT_LEAST32_MAX}, {INT_LEAST32_MIN, INT_LEAST32_MIN}};
  return 0;
}


struct PmlxzjCursor {
  /// x: left, y: top
  struct PmlxzjPoint p;
  struct PmlxzjSegment seg;
  struct PmlxzjBuffer buf;
};

__THROW __nonnull() __attr_access((__read_only__, 1))
int PmlxzjCursor_print (
  const struct PmlxzjCursor *cursor, FILE *out, off_t offset);

__nonnull()
static inline int PmlxzjCursor_read (struct PmlxzjCursor *cursor, FILE *file) {
  return PmlxzjBuffer_init_seg(&cursor->buf, file, &cursor->seg);
}

__nonnull()
static inline void PmlxzjCursor_destroy (struct PmlxzjCursor *cursor) {
  PmlxzjBuffer_destroy(&cursor->buf);
}

__nonnull() __attr_access((__write_only__, 1)) __attr_access((__read_only__, 2))
static inline int PmlxzjCursor_init (
    struct PmlxzjCursor *cursor, const struct PmlxzjLxeCursor *h_cursor) {
  cursor->p.x = le32toh(h_cursor->left);
  cursor->p.y = le32toh(h_cursor->top);
  cursor->seg.size = le32toh(h_cursor->size);
  cursor->seg.offset = 0;
  cursor->buf = (struct PmlxzjBuffer) {};
  return 0;
}

__THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjCursor_init_file (struct PmlxzjCursor *cursor, FILE *file);


struct PmlxzjImage {
  /// p1.x: left, p1.y: top, p2.x: right, p2.y: bottom
  struct PmlxzjRect rect;
  struct PmlxzjSegment seg;
  struct PmlxzjBuffer buf;
};

__THROW __wur __attribute_pure__ __nonnull() __attr_access((__read_only__, 1))
bool PmlxzjImage_valid (
  const struct PmlxzjImage *image, uint_fast32_t width, uint_fast32_t height);
__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PmlxzjImage_apply (
  const struct PmlxzjImage *image, unsigned char *pixels,
  uint_fast32_t width, uint_fast32_t height);
__THROW __nonnull() __attr_access((__read_only__, 1))
int PmlxzjImage_print (
  const struct PmlxzjImage *image, FILE *out, off_t offset);
__THROW __nonnull((1, 2)) __attr_access((__read_only__, 4))
int PmlxzjImage_read (
  struct PmlxzjImage *image, FILE *file, unsigned int type, const void *key);

__nonnull()
static inline void PmlxzjImage_destroy (struct PmlxzjImage *image) {
  PmlxzjBuffer_destroy(&image->buf);
}

__nonnull() __attr_access((__write_only__, 1)) __attr_access((__read_only__, 2))
static inline int PmlxzjImage_init (
    struct PmlxzjImage *image, const struct PmlxzjLxeImage *h_image) {
  image->rect.p1.x = le32toh(h_image->left);
  image->rect.p1.y = le32toh(h_image->top);
  image->rect.p2.x = le32toh(h_image->right);
  image->rect.p2.y = le32toh(h_image->bottom);
  image->seg.size = le32toh(h_image->size);
  image->seg.offset = 0;
  image->buf = (struct PmlxzjBuffer) {};
  return 0;
}

__THROW __nonnull() __attr_access((__write_only__, 1))
int PmlxzjImage_init_file (struct PmlxzjImage *image, FILE *file);


struct PmlxzjFrame {
  struct PmlxzjCursor cursor;
  struct PmlxzjImage **patches;
  uint_least16_t patches_cnt;
};

__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__write_only__, 2))
int PmlxzjFrame_apply (
  const struct PmlxzjFrame *frame, unsigned char *pixels,
  uint_fast32_t width, uint_fast32_t height);
__nonnull()
struct PmlxzjImage *PmlxzjFrame_new_patch (struct PmlxzjFrame *frame);

__nonnull()
static inline void PmlxzjFrame_destroy (struct PmlxzjFrame *frame) {
  for (size_t i = 0; i < frame->patches_cnt; i++) {
    PmlxzjImage_destroy(frame->patches[i]);
  }
  free(frame->patches);
}

__nonnull() __attr_access((__write_only__, 1))
static inline int PmlxzjFrame_init (struct PmlxzjFrame *frame) {
  frame->cursor = (struct PmlxzjCursor) {};
  frame->patches = NULL;
  frame->patches_cnt = 0;
  return 0;
}


struct PmlxzjVideo {
  struct PmlxzjFrame **frames;
  uint_least32_t frames_cnt;
};

__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjVideo_write_apng (
  const struct PmlxzjVideo *video, const struct Pmlxzj *pl, FILE *out,
  bool use_subframes, unsigned int nproc);
__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 2)) __attr_access((__read_only__, 3))
int PmlxzjVideo_save_apng (
  const struct PmlxzjVideo *video, const struct Pmlxzj *pl, const char *path,
  bool use_subframes, unsigned int nproc);
__THROW __nonnull() __attr_access((__read_only__, 1))
__attr_access((__read_only__, 3))
int PmlxzjVideo_save_cursors (
  const struct PmlxzjVideo *video, FILE *in, const char *dir);
__nonnull()
struct PmlxzjFrame *PmlxzjVideo_new_frame (struct PmlxzjVideo *video);

__nonnull()
static inline void PmlxzjVideo_destroy (struct PmlxzjVideo *video) {
  for (size_t i = 0; i < video->frames_cnt; i++) {
    PmlxzjFrame_destroy(video->frames[i]);
  }
  free(video->frames);
}

__nonnull() __attr_access((__write_only__, 1)) __attr_access((__read_only__, 2))
int PmlxzjVideo_init (
  struct PmlxzjVideo *video, const struct Pmlxzj *pl,
  int_fast32_t frames_limit);


#ifdef __cplusplus
}
#endif

#endif /* PMLXZJ_VIDEO_H */
