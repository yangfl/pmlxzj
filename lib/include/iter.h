#ifndef PLZJ_ITER_H
#define PLZJ_ITER_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

#include "defs.h"
#include "parser.h"
#include "structs.h"


struct PlzjLxePacketIter {
  union PlzjLxePacket packet;
  /// current frame number
  int32_t frame_no;
  /// current packet number within this frame
  int32_t frame_packet_no;

  /// offset of current packet header
  off_t begin_offset;
  /// offset of current data stream (without length header)
  off_t offset;
  /// offset of next packet header
  off_t end_offset;

  /// number of frames to process
  int32_t frames_cnt;

  /// video width
  uint32_t width;
  /// video height
  uint32_t height;

  /// input file
  FILE *file;
};

PLZJ_API __THROW __attribute_warn_unused_result__ __nonnull()
int PlzjLxePacketIter_next (struct PlzjLxePacketIter *iter);

PLZJ_API __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PlzjLxePacketIter_init (
  struct PlzjLxePacketIter *iter, const struct Plzj *pl, int32_t frames_limit);

#define PlzjLxePacketIter_NEXT_FRAME 0
#define PlzjLxePacketIter_NEXT_CURSOR 1
#define PlzjLxePacketIter_NEXT_IMAGE 2


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_ITER_H */
