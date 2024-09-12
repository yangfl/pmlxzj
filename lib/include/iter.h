#ifndef PMLXZJ_ITER_H
#define PMLXZJ_ITER_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

#include "defs.h"
#include "parser.h"
#include "serialized.h"


struct PmlxzjLxePacketIter {
  union PmlxzjLxePacket packet;
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

DLL_PUBLIC __THROW __attribute_warn_unused_result__ __nonnull()
int PmlxzjLxePacketIter_next (struct PmlxzjLxePacketIter *iter);

DLL_PUBLIC __THROW __nonnull() __attr_access((__write_only__, 1))
__attr_access((__read_only__, 2))
int PmlxzjLxePacketIter_init (
  struct PmlxzjLxePacketIter *iter, const struct Pmlxzj *pl,
  int32_t frames_limit);

#define PmlxzjLxePacketIter_NEXT_FRAME 0
#define PmlxzjLxePacketIter_NEXT_CURSOR 1
#define PmlxzjLxePacketIter_NEXT_IMAGE 2


#ifdef __cplusplus
}
#endif

#endif /* PMLXZJ_ITER_H */
