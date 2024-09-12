#ifndef PMLXZJ_SERIALIZED_H
#define PMLXZJ_SERIALIZED_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define _BSD_SOURCE

#include <assert.h>
#include <endian.h>
#include <stdbool.h>
#include <stdint.h>

#include "defs.h"


/******** header ********/

struct PmlxzjLxePlayer {
  uint32_t color;
  uint32_t fullscreen;
  uint32_t set_control_pos;
  uint32_t control_left;
  uint32_t control_top;
  uint32_t native_border;
  uint32_t loop_playback;
  uint32_t auto_scaling;
  uint32_t quit_at_end;
  uint32_t show_time;
  uint32_t border_style_1;  // ?
  uint32_t enable_popup_menu;
  uint32_t enable_control_window;
  uint32_t show_control_window;
  char title[24];
  uint32_t video_type;
  uint32_t audio_type;
  uint32_t field_58;
  /// click events (光标增强 gbzq)
  uint32_t has_clicks;
  uint32_t field_60;
  uint32_t field_64;
  uint32_t field_68;
  // gbzq_has_cursor & 1
  // gbzq_has_findcursorhotxy & 2
  uint8_t field_6C;
  uint8_t gbzqcolorbh;
  uint16_t gbzqtmd;
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
} __packed;
static_assert(sizeof(struct PmlxzjLxePlayer) == 0xb4);

#define PMLXZJ_VIDEO_ENC        1
#define PMLXZJ_VIDEO_ZLIB       2
#define PMLXZJ_VIDEO_JK_MUL     10  // 接口？

#define PMLXZJ_AUDIO_WAV                1
#define PMLXZJ_AUDIO_WAV_ZLIB           2
#define PMLXZJ_AUDIO_MP3                5
#define PMLXZJ_AUDIO_TRUE_SPEECH        6
#define PMLXZJ_AUDIO_AAC                7


struct PmlxzjLxeFooter {
  uint32_t editlock_key;
  /// checksum for play lock password
  uint32_t playlock_cksum;
  uint32_t key_3;
  uint32_t key_4;
  uint32_t key_5;
  uint32_t unknown_3;
  uint32_t unknown_4;
  uint32_t data_offset;
  char magic[12];
} __packed;
static_assert(sizeof(struct PmlxzjLxeFooter) == 0x2c);

/// 屏幕录像专家 天狼星 http://www.tlxsoft.com/
#define PMLXZJ_MAGIC "pmlxzjtlx"


/******** video ********/

/// https://docwiki.embarcadero.com/Libraries/Athens/en/System.UITypes.TFontStyles
union PmlxzjTFontStyles {
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
struct PmlxzjLxeVideo {
  /// only 1 observed
  uint32_t version;
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
  union PmlxzjTFontStyles infotextfontstyle;
} __packed;
static_assert(sizeof(struct PmlxzjLxeVideo) == 0xa0);


struct PmlxzjLxeImage {
  /// frame_no > 0
  int32_t frame_no;
  uint32_t left;
  uint32_t top;
  uint32_t right;
  uint32_t bottom;
  uint32_t size;
  // unsigned char data[];
} __packed;
static_assert(sizeof(struct PmlxzjLxeImage) == 0x18);


struct PmlxzjLxeCursor {
  /// frame_no_neg = 1 - frame_no
  int32_t frame_no_neg;
  uint32_t left;
  uint32_t top;
  uint32_t size;
  // unsigned char data[];
} __packed;
static_assert(sizeof(struct PmlxzjLxeCursor) == 0x10);


union PmlxzjLxePacket {
  int32_t varient_no;
  struct PmlxzjLxeImage image;
  struct PmlxzjLxeCursor cursor;
};

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline bool PmlxzjLxePacket_is_cursor (const void *packet) {
  return (int32_t) le32toh(*(const int32_t *) packet) <= 0;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PmlxzjLxePacket_frame_no (const void *packet) {
  int32_t frame_no = le32toh(*(const int32_t *) packet);
  return frame_no > 0 ? frame_no : 1 - frame_no;
}

__attribute_artificial__ __attribute_warn_unused_result__ __attribute_pure__
__nonnull() __attr_access((__read_only__, 1))
static inline uint32_t PmlxzjLxePacket_data_size (const void *packet) {
  return le32toh(PmlxzjLxePacket_is_cursor(packet) ?
    ((const struct PmlxzjLxeCursor *) packet)->size :
    ((const struct PmlxzjLxeImage *) packet)->size);
}


/******** audio ********/

struct PmlxzjWAVEFORMATEX {
  uint16_t wFormatTag;
  uint16_t nChannels;
  uint32_t nSamplesPerSec;
  uint32_t nAvgBytesPerSec;
  uint16_t nBlockAlign;
  uint16_t wBitsPerSample;
  uint16_t cbSize;
} __packed;
static_assert(sizeof(struct PmlxzjWAVEFORMATEX) == 0x12);


// struct PmlxzjLxeAudioWav {
//   nothing
// } __packed;


struct PmlxzjLxeAudioWavZlib {
  uint32_t segments_cnt;
} __packed;


struct PmlxzjLxeAudioMP3 {
  struct PmlxzjWAVEFORMATEX wav_spec;
  struct PmlxzjWAVEFORMATEX mp3_spec;
  uint32_t unk_audio_prop;
  uint32_t segments_cnt;
  // uint32_t offsets[segments_cnt + 1];
} __packed;


struct PmlxzjLxeAudioAAC {
  struct PmlxzjWAVEFORMATEX wav_spec;
  uint32_t unused_field_80;
  /// <= 4
  uint32_t format_len;
  // uint8_t formats[format_len];
} __packed;


#ifdef __cplusplus
}
#endif

#endif /* PMLXZJ_SERIALIZED_H */
