#define _BSD_SOURCE

#include <endian.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "include/parser.h"
#include "utils.h"
#include "macro.h"
#include "log.h"


static int _Pmlxzj_extract_audio_wav (
    FILE *file, const char *path, char *filename) {
  snprintf(filename, 64, "audio.wav");
  return dump_lpe(path, file);
}


static int _Pmlxzj_extract_audio_wav_zlib (
    FILE *file, const char *path, char *filename) {
  struct PmlxzjLxeAudioWavZlib audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_SYS(fread);
  return_if_fail (audio.segments_cnt != 0) 0;

  snprintf(filename, 64, "audio.wav");
  FILE *out = fopen(path, "wb");
  return_if_fail (out != NULL) ERR_SYS(fopen);

  int ret = 0;
  for (uint32_t i = 0; i < le32toh(audio.segments_cnt); i++) {
    ret = write_lpe_uncompress(out, file);
    break_if_fail (ret == 0);
  }

  fclose(out);
  return ret;
}


static int _Pmlxzj_extract_audio_mp3 (
    FILE *file, const char *path, char *filename) {
  struct PmlxzjLxeAudioMP3 audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_SYS(fread);
  return_if_fail (audio.segments_cnt != 0) 0;

  for (uint32_t i = 0; i <= le32toh(audio.segments_cnt); i++) {
    uint32_t offset;
    return_if_fail (fread(&offset, sizeof(offset), 1, file) == 1)
      ERR_SYS(fread);
    (void) offset;
  }

  snprintf(filename, 64, "audio.mp3");
  return dump_lpe(path, file);
}


static int _Pmlxzj_extract_audio_aac (
    FILE *file, const char *path, char *filename) {
  struct PmlxzjLxeAudioAAC audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_SYS(fread);

  for (uint32_t i = 0; i <= le32toh(audio.format_len); i++) {
    uint8_t format;
    return_if_fail (fread(&format, sizeof(format), 1, file) == 1)
      ERR_SYS(fread);
    (void) format;
  }

  uint32_t segment_sizes_len_h;
  return_if_fail (fread(
    &segment_sizes_len_h, sizeof(segment_sizes_len_h), 1, file) == 1
  ) ERR_SYS(fread);
  uint32_t segment_sizes_cnt =
    le32toh(segment_sizes_len_h) / sizeof(uint32_t);

  uint32_t segment_sizes[segment_sizes_cnt];
  return_if_fail (fread(
    segment_sizes, segment_sizes_cnt * sizeof(uint32_t), 1, file) == 1
  ) ERR_SYS(fread);

  for (uint32_t i = 0; i < segment_sizes_cnt; i++) {
    if (segment_sizes[i] > ((1 << 13) - 1 - 7)) {
      return ERR(PL_EFORMAT);
    }
  }

  snprintf(filename, 64, "audio.aac");
  (void) path;
  return ERR(PL_ENOTSUP);
}


int Pmlxzj_extract_audio (const struct Pmlxzj *pl, const char *dir) {
  return_if_fail (pl->audio_offset != -1) 0;
  return_if_fail (fseeko(pl->file, pl->audio_offset, SEEK_SET) == 0)
    ERR_SYS(fseeko);

  size_t dir_len = strlen(dir);
  char path[dir_len + 65];
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = '/';
  filename++;

  switch (le32toh(pl->player.audio_type)) {
    case PMLXZJ_AUDIO_WAV:
      return _Pmlxzj_extract_audio_wav(pl->file, path, filename);
    case PMLXZJ_AUDIO_WAV_ZLIB:
      return _Pmlxzj_extract_audio_wav_zlib(pl->file, path, filename);
    case PMLXZJ_AUDIO_MP3:
      return _Pmlxzj_extract_audio_mp3(pl->file, path, filename);
    case PMLXZJ_AUDIO_AAC:
      return _Pmlxzj_extract_audio_aac(pl->file, path, filename);
    default:
      return ERR(PL_ENOTSUP);
  }

  return 0;
}
