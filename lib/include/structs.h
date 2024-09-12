#ifndef PLZJ_STRUCTS_H
#define PLZJ_STRUCTS_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "platform/endian.h"

#include "defs.h"


/******** header ********/

#define PLZJ_MAGIC_LEN 12

/// 屏幕录像专家 天狼星 http://www.tlxsoft.com/
#define PLZJ_MAGIC "pmlxzjtlx"
/// 屏幕录像专家 EXE 多节
#define PLZJ_MAGIC_SECTIONED "pmlxzjedj"


struct __packed PlzjLxeFooterSectioned {
  /// unknown value: 8
  uint32_t unknown_1;
  /// unknown value: 8
  uint32_t unknown_2;
  /// offset to data begin
  uint32_t data_offset;
  /// number of PlzjLxeSection
  uint32_t sections_cnt;
  /// editlock encryption password
  uint32_t editlock_key;
  /// checksum for play lock password
  uint32_t playlock_cksum;
};
static_assert(sizeof(struct PlzjLxeFooterSectioned) == 0x18);

#define PLZJ_OFFSET_FOOTER_SECTIONED (-0x44)


struct __packed PlzjLxeSection {
  char caption[52];
  uint32_t begin_offset;
  uint32_t end_offset;
  uint32_t begin_offset_hi;
  uint32_t end_offset_hi;
  uint32_t field_44;
  uint32_t field_48;
  uint32_t field_4C;
};
static_assert(sizeof(struct PlzjLxeSection) == 0x50);


struct __packed PlzjLxeFooter {
  /// editlock encryption password
  uint32_t editlock_key;
  /// checksum for play lock password
  uint32_t playlock_cksum;
  uint32_t key_3;
  /// only valid when `file[PlzjLxeFooter.data_offset] == -1`
  uint64_t audio_offset64;
  /// unknown value: 8
  uint32_t unknown_1;
  /// unknown value: 8
  uint32_t unknown_2;
  /// offset to data begin
  uint32_t data_offset;
};
static_assert(sizeof(struct PlzjLxeFooter) == 0x20);

#define PLZJ_OFFSET_FOOTER (-0x2c)


struct __packed PlzjLxePlayer {
  /// canvas background color
  struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t unused;
  } background_color;
  /// maximize canvas at startup
  uint32_t maximize;
  /**
   * 0: place canvas at center of the screen at startup
   * 1: place canvas at requested position on screen at startup
   */
  uint32_t set_player_position;
  /// requested left position
  uint32_t player_left;
  /// requested top position
  uint32_t player_top;
  /// 0: borderless canvas, 1: use system window border
  uint32_t native_border;
  uint32_t loop_playback;
  uint32_t auto_scaling;
  /// exit player when playback ends
  uint32_t exit_at_end;
  /// show time at top right
  uint32_t show_time;
  /// fullscreen at startup
  uint32_t fullscreen;
  /// enable right click popup menu
  uint32_t enable_popup_menu;
  uint32_t enable_control_window;
  uint32_t show_control_window;
  char title[24];
  uint32_t video_type;
  uint32_t audio_type;
  uint32_t field_58;
  /// video contains click event sequence
  uint32_t has_clicks;
  /// draw circles when clicked
  uint32_t draw_clicks;
  uint32_t field_64;
  /// images have been quantized to 64 colors
  uint32_t low_image_quality;
  /**
   * draw circle to highlight cursor position
   *
   * Flags:
   *   1: Enable (gbzq_has_cursor)
   *   2: Place circle at left-top of cursor ico (gbzq_has_findcursorhotxy)
   *
   * Original name: gbzq (光标增强)
   */
  uint8_t cursor_highlight;
  /// color index of cursor highlight circle
  uint8_t cursor_highlight_color;
  /**
   * transparency of cursor highlight circle, 0 (opaque) - 10000 (transparent)
   * low: 3000, middle: 5000, high: 7000
   *
   * Original name: gbzqtmd (光标增强透明度)
   */
  uint16_t cursor_highlight_transparency;
  uint32_t field_70;
  uint32_t field_74;
  uint32_t field_78;
  uint32_t field_7C;
  uint32_t field_80;
  uint32_t field_84;
  uint32_t field_88;
  uint32_t field_8C;
  uint32_t field_90;
  uint32_t field_94;
  uint32_t field_98;
  uint32_t field_9C;
  uint32_t field_A0;
  uint32_t field_A4;
  uint32_t field_A8;
  uint32_t field_AC;
  uint32_t field_B0;
};
static_assert(sizeof(struct PlzjLxePlayer) == 0xb4);

#define PLZJ_VIDEO_ENC        1
#define PLZJ_VIDEO_ZLIB       2
#define PLZJ_VIDEO_JK_MUL     10  // 接口？

#define PLZJ_AUDIO_WAV                1
#define PLZJ_AUDIO_WAV_ZLIB           2
#define PLZJ_AUDIO_MP3                5
#define PLZJ_AUDIO_TRUESPEECH         6
#define PLZJ_AUDIO_AAC                7

#define PLZJ_CURSOR_HIGHLIGHT_YELLOW  1
#define PLZJ_CURSOR_HIGHLIGHT_GREEN   2
#define PLZJ_CURSOR_HIGHLIGHT_BLUE    3
#define PLZJ_CURSOR_HIGHLIGHT_RED     4


/******** video ********/

/// https://docwiki.embarcadero.com/Libraries/Athens/en/System.UITypes.TFontStyles
union PlzjTFontStyles {
  struct {
    /// font is bold
    uint8_t fsBold;
    /// font is italic
    uint8_t fsItalic;
    /// font is underlined
    uint8_t fsUnderline;
    /// font is displayed with a horizontal line through it
    uint8_t fsStrikeOut;
  };
  uint32_t style;
};


/// com.tlxsoft.lxeplayerapplication.MainActivity
struct __packed PlzjLxeVideo {
  /**
   * 1: Single monitor
   * 2: Dual monitor
   */
  uint32_t screen_cfg;
  uint32_t width;
  uint32_t height;
  uint32_t frames_cnt;
  /// dumb values; does not matter, = 5
  uint32_t fps;
  /// usually = 200 (FPS = 5)
  uint32_t frame_ms;
  uint32_t field_18;
  uint32_t field_1C;
  uint32_t field_20;
  uint32_t has_cursor;

  char regcode1[20];
  char regcode2[20];

  /// infotext watermark
  char infotext[40];
  /// x coordinate of the infotext watermark
  uint32_t infox;
  /// y coordinate of the infotext watermark
  uint32_t infoy;
  uint32_t infotextfontsize;
  char infotextfontname[20];
  uint32_t infotextfontcolor;
  union PlzjTFontStyles infotextfontstyle;
};
static_assert(sizeof(struct PlzjLxeVideo) == 0xa0);


struct __packed PlzjLxeImage {
  /// frame_no > 0
  int32_t frame_no;
  uint32_t left;
  uint32_t top;
  uint32_t right;
  uint32_t bottom;
  uint32_t size;
  // unsigned char data[];
};
static_assert(sizeof(struct PlzjLxeImage) == 0x18);


struct __packed PlzjLxeCursor {
  /// frame_no_neg = 1 - frame_no
  int32_t frame_no_neg;
  uint32_t left;
  uint32_t top;
  uint32_t size;
  // unsigned char data[];
};
static_assert(sizeof(struct PlzjLxeCursor) == 0x10);


union PlzjLxePacket {
  int32_t varient_no;
  struct PlzjLxeImage image;
  struct PlzjLxeCursor cursor;
};

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline bool PlzjLxePacket_is_cursor (const void *packet) {
  return (int32_t) le32toh(*(const int32_t *) packet) <= 0;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PlzjLxePacket_frame_no (const void *packet) {
  int32_t frame_no = le32toh(*(const int32_t *) packet);
  return frame_no > 0 ? frame_no : 1 - frame_no;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PlzjLxePacket_data_size (const void *packet) {
  return le32toh(PlzjLxePacket_is_cursor(packet) ?
    ((const struct PlzjLxeCursor *) packet)->size :
    ((const struct PlzjLxeImage *) packet)->size);
}


/******** audio ********/

struct __packed PlzjWAVEFORMATEX {
  uint16_t wFormatTag;
  uint16_t nChannels;
  uint32_t nSamplesPerSec;
  uint32_t nAvgBytesPerSec;
  uint16_t nBlockAlign;
  uint16_t wBitsPerSample;
  uint16_t cbSize;
};
static_assert(sizeof(struct PlzjWAVEFORMATEX) == 0x12);


// struct __packed PlzjLxeAudioWav {
//   uint32_t len;
//   unsigned char data[len];
// };


struct __packed PlzjLxeAudioWavZlib {
  uint32_t chunks_cnt;

  // uint32_t len;
  // unsigned char data[len];
  // ...
};

#define PLZJ_AUDIO_WAV_ZLIB_CHUNK_MAX_SIZE 125000


struct __packed PlzjLxeAudioMP3 {
  struct PlzjWAVEFORMATEX wav_spec;
  struct PlzjWAVEFORMATEX mp3_spec;
  /// duration time of each chunk
  uint32_t chunk_duration;

  uint32_t offsets_cnt;
  // uint32_t offsets[offsets_cnt + 1];

  // uint32_t len;
  // unsigned char data[len];
};


struct __packed PlzjLxeAudioTruespeech {
  struct PlzjWAVEFORMATEX wav_spec;
  struct PlzjWAVEFORMATEX ts_spec;

  // uint32_t len;
  // unsigned char data[len];
};


struct __packed PlzjLxeAudioAAC {
  struct PlzjWAVEFORMATEX wav_spec;
  uint32_t sample_pre_chunk;

  uint32_t config_len;
  // uint16_t channels[config_len];

  // uint32_t chunk_sizes_len;
  // uint32_t chunk_sizes[chunk_sizes_len / 4];

  // uint32_t data_size;

  // unsigned char data[chunk_sizes[i]];
  // ...
};

#define PLZJ_ADTS_MAX_FRAME_LEN ((1 << 13) - 1)
#define PLZJ_ADTS_HEADER_LEN 7
#define PLZJ_ADTS_MAX_DATA_LEN (PLZJ_ADTS_MAX_FRAME_LEN - PLZJ_ADTS_HEADER_LEN)


#ifdef __cplusplus
}
#endif

#endif /* PLZJ_STRUCTS_H */
