#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/platform/endian.h"
#include "platform/nowide.h"

#include "include/parser.h"
#include "macro.h"
#include "log.h"
#include "utils.h"


static int _Plzj_extract_audio_wav (
    FILE *file, const char *path, char *filename) {
  snprintf(filename, 64, "audio.wav");

  size_t size;
  int ret = dump_lpe(path, file, &size, 0);
  return_if_fail (ret == 0) ret;

  sc_debug("%s file size: %" PRIuSIZE "\n", "WAV", size);
  return_if_fail (size > 0) 1;
  return 0;
}


static int _Plzj_extract_audio_wav_zlib (
    FILE *file, const char *path, char *filename) {
  struct PlzjLxeAudioWavZlib audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_STD(fread);
  return_if_fail (le32toh(audio.chunks_cnt) > 0) 1;

  snprintf(filename, 64, "audio.wav");
  FILE *out = mfopen(path, "wb");
  return_if_fail (out != NULL) ERR_STD(mfopen);

  size_t total_size = 0;
  int ret = 0;
  for (uint32_t i = 0; i < le32toh(audio.chunks_cnt); i++) {
    size_t chunk_size;
    size_t out_size;
    static_assert(4096 * 32 >= PLZJ_AUDIO_WAV_ZLIB_CHUNK_MAX_SIZE);
    ret = write_lpe_uncompress(out, file, &chunk_size, 4096 * 32, &out_size);
    goto_if_fail (ret == 0) fail;

    if_fail (out_size <= PLZJ_AUDIO_WAV_ZLIB_CHUNK_MAX_SIZE) {
      sc_warning(
        "WAV uncompressed chunk too large (%" PRIuSIZE ""
        " > %d), potentially corrupted\n",
        out_size, PLZJ_AUDIO_WAV_ZLIB_CHUNK_MAX_SIZE);
    }
    sc_debug(
      "WAV zlib chunk %" PRIu32 ": size %" PRIuSIZE ", uncompressed %"
      PRIuSIZE "\n", i, chunk_size, out_size);
    total_size += out_size;
  }
  sc_debug("%s file size: %" PRIuSIZE "\n", "WAV", total_size);

fail:
  fclose(out);
  return ret;
}


static int _Plzj_extract_audio_mp3 (
    FILE *file, const char *path, char *filename) {
  struct PlzjLxeAudioMP3 audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_STD(fread);

  for (uint32_t i = 0; i <= le32toh(audio.offsets_cnt); i++) {
    uint32_t offset_h;
    return_if_fail (fread(&offset_h, sizeof(offset_h), 1, file) == 1)
      ERR_STD(fread);

    uint32_t offset = le32toh(offset_h);
    if (i == 0) {
      if_fail (offset == 0) {
        sc_warning("MP3 offset 0 should be 0, got %" PRIu32 "\n", offset);
      }
    }
    sc_debug("MP3 offset %" PRIu32 ": %" PRIu32 "\n", i, offset);
  }

  snprintf(filename, 64, "audio.mp3");

  size_t size;
  int ret = dump_lpe(path, file, &size, 0);
  return_if_fail (ret == 0) ret;

  sc_debug("%s file size: %" PRIuSIZE "\n", "MP3", size);
  return_if_fail (size > 0) 1;
  return 0;
}


static int _Plzj_extract_audio_truespeech (
    FILE *file, const char *path, char *filename) {
  struct PlzjLxeAudioTruespeech audio;
  return_if_fail (fread(&audio, sizeof(audio), 1, file) == 1) ERR_STD(fread);

  uint32_t data_size_h;
  return_if_fail (fread(&data_size_h, sizeof(data_size_h), 1, file) == 1)
    ERR_STD(fread);
  uint32_t data_size = le32toh(data_size_h);

  unsigned char header[78];
  uint32_t size = sizeof(header) + data_size;
  sc_debug("%s file size: %" PRIuSIZE "\n", "TrueSpeech", (size_t) size);

  snprintf(filename, 64, "audio.wav");
  FILE *out = mfopen(path, "wb");
  return_if_fail (out != NULL) ERR_STD(mfopen);

  memcpy(header + 0, "RIFF", 4);
  *(uint32_t *) (header + 4) = htole32(size - 8);
  memcpy(header + 8, "WAVE", 4);

  memcpy(header + 12, "fmt ", 4);
  static_assert(sizeof(audio.ts_spec) == 18);
  *(uint32_t *) (header + 16) = htole32(sizeof(audio.ts_spec) + 32);
  memcpy(header + 20, &audio.ts_spec, sizeof(audio.ts_spec));
  memcpy(header + 38, "\1\0\xf0", 4);
  memset(header + 42, 0, 28);

  memcpy(header + 70, "data", 4);
  *(uint32_t *) (header + 74) = htole32(data_size);

  int ret;

  if_fail (fwrite(&header, sizeof(header), 1, out) == 1) {
    ret = ERR_STD(fwrite);
    goto fail;
  }

  ret = copy(out, file, data_size, 0);

fail:
  fclose(out);
  return ret;
}


struct PlzjAudioAAC {
  struct PlzjWAVEFORMATEX wav_spec;
  uint32_t sample_pre_chunk;

  uint32_t config_len;
  // https://wiki.multimedia.cx/index.php/MPEG-4_Audio#Audio_Specific_Config
  uint8_t *config;

  uint32_t stream_data_sizes_cnt;
  uint32_t *stream_data_sizes;

  uint32_t data_size;
  off_t data_offset;

  uint8_t object_type;
  uint8_t sampling_freq_idx;
  uint32_t sampling_freq;
  uint8_t channel_cfg;
};


// https://wiki.multimedia.cx/index.php/ADTS
static void PlzjAudioAAC_frame_header (
    const struct PlzjAudioAAC *aac, void *buf, uint16_t data_len) {
  uint8_t *header = buf;

  header[0] = 0xff;

  uint8_t mpeg_version = 0;  // MPEG-4
  uint8_t layer = 0;  // always set to 0
  uint8_t protection_absence = 1;  // no CRC
  header[1] =
    SET_BIT_FIELD(0xf, 4, 4) |
    SET_BIT_FIELD(mpeg_version, 3, 1) |
    SET_BIT_FIELD(layer, 1, 2) |
    SET_BIT_FIELD(protection_absence, 0, 1);

  uint8_t profile = aac->object_type - 1;
  uint8_t sampling_freq_idx = aac->sampling_freq_idx;
  uint8_t private_bit = 0;
  uint8_t channel_cfg = aac->channel_cfg;
  header[2] =
    SET_BIT_FIELD(profile, 6, 2) |
    SET_BIT_FIELD(sampling_freq_idx, 2, 4) |
    SET_BIT_FIELD(private_bit, 1, 1) |
    SET_BIT_FIELD(channel_cfg >> 2, 0, 1);

  uint8_t originality = 0;
  uint8_t home_usage = 0;
  uint8_t copyrighted = 0;
  uint8_t copyright_start = 0;
  uint16_t frame_len = data_len + PLZJ_ADTS_HEADER_LEN;
  header[3] =
    SET_BIT_FIELD(channel_cfg, 6, 2) |
    SET_BIT_FIELD(originality, 5, 1) |
    SET_BIT_FIELD(home_usage, 4, 1) |
    SET_BIT_FIELD(copyrighted, 3, 1) |
    SET_BIT_FIELD(copyright_start, 2, 1) |
    SET_BIT_FIELD(frame_len >> 11, 0, 1);
  header[4] = frame_len >> 3;

  uint16_t buffer_fullness = 0x7ff;  // variable bitrate, buffer fullness isn't
                                     // applicable
  header[5] =
    SET_BIT_FIELD(frame_len, 5, 3) |
    SET_BIT_FIELD(buffer_fullness >> 6, 0, 5);

  uint8_t number_of_frames = 1 - 1;
  header[6] =
    SET_BIT_FIELD(buffer_fullness, 2, 6) |
    SET_BIT_FIELD(number_of_frames, 0, 2);
}


static void PlzjAudioAAC_destroy (const struct PlzjAudioAAC *aac) {
  free(aac->config);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-allocation-size"
  free(aac->stream_data_sizes);
#pragma GCC diagnostic pop
}


static int PlzjAudioAAC_init (struct PlzjAudioAAC *aac, FILE *in) {
  struct PlzjLxeAudioAAC h_aac;
  return_if_fail (fread(&h_aac, sizeof(h_aac), 1, in) == 1) ERR_STD(fread);

  // read config
  aac->config_len = le32toh(h_aac.config_len);
  return_if_fail (aac->config_len >= 2) ERR_FMT(
    PL_EFORMAT, "AAC Audio Specific Config too short (%" PRIu32 " < 2)",
    aac->config_len);

  aac->config = malloc(max(aac->config_len, 6));
  return_if_fail (aac->config != NULL) ERR_STD(malloc);

  int ret;

  if_fail (fread(aac->config, aac->config_len, 1, in) == 1) {
    ret = ERR_STD(fread);
    goto fail_read_config;
  }

  // read stream sizes
  uint32_t stream_sizes_len_h;
  if_fail (fread(
      &stream_sizes_len_h, sizeof(stream_sizes_len_h), 1, in) == 1) {
    ret = ERR_STD(fread);
    goto fail_stream_data_sizes;
  }
  uint32_t stream_sizes_len = le32toh(stream_sizes_len_h);
  if_fail (stream_sizes_len % sizeof(uint32_t) == 0) {
    sc_warning("AAC stream sizes length %" PRIu32 " is not a multiple of 4\n",
               stream_sizes_len);
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-allocation-size"
  aac->stream_data_sizes = malloc(stream_sizes_len);
#pragma GCC diagnostic pop
  if_fail (aac->stream_data_sizes != NULL) {
    ret = ERR_STD(malloc);
    goto fail_stream_data_sizes;
  }
  if_fail (fread(aac->stream_data_sizes, stream_sizes_len, 1, in) == 1) {
    ret = ERR_STD(fread);
    goto fail_read_stream_data_sizes;
  }

  aac->stream_data_sizes_cnt = stream_sizes_len / sizeof(uint32_t);
  if_fail (aac->stream_data_sizes_cnt > 0) {
    sc_warning("AAC has no streams\n");
  }
  for (uint32_t i = 0; i < aac->stream_data_sizes_cnt; i++) {
    aac->stream_data_sizes[i] = le32toh(aac->stream_data_sizes[i]);
  }
  for (uint32_t i = 0; i < aac->stream_data_sizes_cnt; i++) {
    if_fail (aac->stream_data_sizes[i] <= PLZJ_ADTS_MAX_DATA_LEN) {
      ret = ERR_FMT(
        PL_EFORMAT, "AAC stream too long (%" PRIu32 " > %d)",
        aac->stream_data_sizes[i], PLZJ_ADTS_MAX_DATA_LEN);
      goto fail_read_stream_data_sizes;
    }
  }

  // read data size
  uint32_t data_size_h;
  if_fail (fread(&data_size_h, sizeof(data_size_h), 1, in) == 1) {
    ret = ERR_STD(fread);
    goto fail_read_stream_data_sizes;
  }
  aac->data_size = le32toh(data_size_h);

  size_t data_size = 0;
  for (uint32_t i = 0; i < aac->stream_data_sizes_cnt; i++) {
    uint32_t stream_data_size = aac->stream_data_sizes[i];
    data_size += stream_data_size;
  }
  if_fail (data_size == aac->data_size) {
    sc_warning(
      "AAC data size mismatched, expected %" PRIuSIZE ", got %" PRIu32 "\n",
      data_size, aac->data_size);
  }

  aac->data_offset = ftello(in);
  if_fail (aac->data_offset != -1) {
    ret = ERR_STD(ftello);
    goto fail_read_stream_data_sizes;
  }

  // set config
  unsigned int ptr = 0;

  aac->object_type = bitstream_get(aac->config, ptr, 5);
  ptr += 5;
  if_fail (aac->object_type > 0 && aac->object_type <= 1 << 2) {
    ret = ERR(PL_EFORMAT);
    goto fail_read_stream_data_sizes;
  }
  if (aac->object_type == 31) {
    aac->object_type = bitstream_get(aac->config, ptr, 6) + 32;
    ptr += 6;
  }

  aac->sampling_freq_idx = bitstream_get(aac->config, ptr, 4);
  ptr += 4;
  if_fail (aac->sampling_freq_idx < 15) {
    ret = ERR(PL_EFORMAT);
    goto fail_read_stream_data_sizes;
  }
  if (aac->sampling_freq_idx != 15) {
    static const uint32_t sampling_freq_table[] = {
      96000, 88200, 64000, 48000, 44100, 32000,
      24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    aac->sampling_freq =
      aac->sampling_freq_idx >= arraysize(sampling_freq_table) ? 0 :
      sampling_freq_table[aac->sampling_freq_idx];
  } else {
    ret = ERR(PL_EFORMAT);
    goto fail_read_stream_data_sizes;

    aac->sampling_freq = bitstream_get(aac->config, ptr, 24);
    ptr += 24;
  }

  aac->channel_cfg = bitstream_get(aac->config, ptr, 4);
  ptr += 4;

  uint16_t nChannels = le16toh(h_aac.wav_spec.nChannels);
  if_fail (aac->channel_cfg == nChannels) {
    sc_warning(
      "AAC channel config mismatch %" PRIu8 " != %" PRIu16 "\n",
      aac->channel_cfg, nChannels);
  }
  uint32_t nSamplesPerSec = le32toh(h_aac.wav_spec.nSamplesPerSec);
  if_fail (aac->sampling_freq == nSamplesPerSec) {
    sc_warning(
      "AAC sampling frequency mismatch %" PRIu32 " != %" PRIu32 "\n",
      aac->sampling_freq, nSamplesPerSec);
  }

  aac->wav_spec = h_aac.wav_spec;
  aac->sample_pre_chunk = le32toh(h_aac.sample_pre_chunk);
  return 0;

fail_read_stream_data_sizes:
  free(aac->stream_data_sizes);
fail_stream_data_sizes:
fail_read_config:
  free(aac->config);
  return ret;
}


static int _Plzj_extract_audio_aac (
    FILE *file, const char *path, char *filename) {
  struct PlzjAudioAAC aac;
  return_with_nonzero (PlzjAudioAAC_init(&aac, file));

  if (sc_log_begin(SC_LOG_DEBUG)) {
    sc_log_print("AAC Audio Specific Config:");
    for (uint32_t i = 0; i < aac.config_len; i++) {
      sc_log_print(" %02x", aac.config[i]);
    }
    sc_log_print("\n");
    sc_log_end(SC_LOG_DEBUG);
  }

  for (uint32_t i = 0; i < aac.stream_data_sizes_cnt; i++) {
    uint32_t stream_data_size = aac.stream_data_sizes[i];
    sc_debug("AAC stream %" PRIu32 " size: %" PRIu32 "\n", i, stream_data_size);
  }
  sc_debug(
    "AAC total data size: %" PRIu32 ", file size : %" PRIu32 "\n",
    aac.data_size,
    aac.data_size + PLZJ_ADTS_HEADER_LEN * aac.stream_data_sizes_cnt);

  int ret;

  snprintf(filename, 64, "audio.aac");
  FILE *out = mfopen(path, "wb");
  if_fail (out != NULL) {
    ret = ERR_STD(mfopen);
    goto fail_out;
  }

  for (uint32_t i = 0; i < aac.stream_data_sizes_cnt; i++) {
    uint32_t stream_data_size = aac.stream_data_sizes[i];

    unsigned char header[PLZJ_ADTS_HEADER_LEN];
    PlzjAudioAAC_frame_header(&aac, header, stream_data_size);
    if_fail (fwrite(header, sizeof(header), 1, out) == 1) {
      ret = ERR_STD(fwrite);
      goto fail;
    }

    ret = copy(out, file, stream_data_size, 0);
    goto_if_fail (ret == 0) fail;
  }

  ret = 0;
fail:
  fclose(out);
fail_out:
  PlzjAudioAAC_destroy(&aac);
  return ret;
}


int Plzj_extract_audio (const struct Plzj *pl, const char *dir) {
  return_if_fail (pl->audio_offset != -1) 0;
  return_if_fail (fseeko(pl->file, pl->audio_offset, SEEK_SET) == 0)
    ERR_STD(fseeko);

  size_t dir_len = strlen(dir);
  char path[dir_len + 65];
  memcpy(path, dir, dir_len);
  char *filename = path + dir_len;
  filename[0] = DIR_SEP;
  filename++;

  switch (le32toh(pl->player.audio_type)) {
    case PLZJ_AUDIO_WAV:
      return _Plzj_extract_audio_wav(pl->file, path, filename);
    case PLZJ_AUDIO_WAV_ZLIB:
      return _Plzj_extract_audio_wav_zlib(pl->file, path, filename);
    case PLZJ_AUDIO_MP3:
      return _Plzj_extract_audio_mp3(pl->file, path, filename);
    case PLZJ_AUDIO_TRUESPEECH:
      return _Plzj_extract_audio_truespeech(pl->file, path, filename);
    case PLZJ_AUDIO_AAC:
      return _Plzj_extract_audio_aac(pl->file, path, filename);
    default:
      return ERR(PL_ENOTSUP);
  }

  return 0;
}
