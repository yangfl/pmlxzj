#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "lib/include/platform/endian.h"
#include "lib/platform/nowide.h"

#include "lib/include/parser.h"
#include "lib/utils.h"
#include "lib/macro.h"
#include "lib/log.h"


struct PlzjOptions {
  char *input_path;
  char *output_path;

  char *password;
  char *new_password;

  struct {
    bool extract_video : 1;
    bool extract_cursor : 1;
    bool extract_audio : 1;
    bool extract_txts : 1;
  };

  struct {
    bool set_password : 1;
    bool change_title : 1;
    bool change_infotext : 1;
    bool do_register : 1;
  };

  float fps;
  long section_i;
  long frames_limit;
  long compression_level;
  long nproc;
  bool use_subframes;
  bool with_cursor;
  bool force;
  bool verbose;
};


static void usage (const char *progname) {
  fputs("Usage: ", stdout);
  fputms(progname, stdout);
  fputs(" [OPTIONS]... <video.exe> [<output>]\n\
  <video.exe>    video.exe to use\n\
  <output>       output path\n\
\n\
If <output> is not specified and no actions are specified, print input file\n\
info.\n\
\n\
If <output> is specified and no actions are specified, extract video and audio\n\
into <output> directory (create if not exist).\n\
\n\
Extract options:\n\
Default output directory is '<video>.extracted'.\n\
  -e, --extract         extract video and audio, suitable for video processing\n\
  -a, --extract-all     extract everything\n\
  --video               extract video\n\
  --cursor              extract cursor icons and traces\n\
  --audio               extract audio\n\
  --txts                extract auxiliary text files (mouse events)\n\
  -r, --framerate <fps> target to <fps> frame per second as much as possible,\n\
                        smooth cursor movement by interpolation (ignored if '-x'\n\
                        specified) (default: 30)\n\
  -x, --raw             do not draw cursor in the output video file\n\
  -m, --modified        use modified APNG, suitable for archiving (ignored if\n\
                        no '-x' specified)\n\
        Further reduce file size by additional transition frames, with their\n\
        delay times set to 1 ms. Some players might enforce a minimal delay time\n\
        for one frame (100 ms for example), but ffmpeg can process it correctly.\n\
  -s, --section <n>     extract section <n> (required if file has multiple\n\
                        sections) (default: 0)\n\
  -n, --frames <n>      only process first <n> frames\n\
  -c, --compression <n> specify zlib compression level (0 no compression - 9\n\
                        best compression) (default: 9)\n\
  -t, --threads <n>     use <n> threads (default: number of cores)\n\
\n\
Modify options:\n\
Default output path is '<video>.modified.exe'.\n\
  -u, --unlock          remove edit lock or play lock (password required if play\n\
                        locked)\n\
  --set-key <password>  set password to <password>\n\
\n\
General options:\n\
  -k, --key <password>  use <password> as password\n\
  -f, --force           force operation, ignore errors\n\
\n\
Program options:\n\
  -v, --verbose         verbose mode, show frame info\n\
  -d, --debug           enables debugging messages\n\
  -h, --help            print this help text\n\
", stdout);
}


__attribute_artificial__
static inline int argtol (const char *s, long *res, long min_, long max_) {
  char *s_end;
  long num = strtol(s, &s_end, 10);
  return_if_fail (*s_end == '\0' && min_ <= num && num <= max_) 1;
  *res = num;
  return 0;
}


__attribute_artificial__
static inline int argtof (const char *s, float *res, float min_, float max_) {
  char *s_end;
  float num = strtof(s, &s_end);
  return_if_fail (*s_end == '\0' && min_ <= num && num <= max_) 1;
  *res = num;
  return 0;
}


__attribute_artificial__
static inline char *get_extension (const char *path) {
  for (size_t i = strlen(path); i > 0; ) {
    i--;
    if (path[i] == '/' || path[i] == '\\') {
      break;
    } else if (path[i] == '.') {
      return (char *) path + i + 1;
    }
  }
  return NULL;
}


static int get_options (struct PlzjOptions *options, int argc, char **argv) {
  *options = (struct PlzjOptions) {
    .fps = 30,
    .section_i = -1,
    .frames_limit = -1,
    .compression_level = Z_BEST_COMPRESSION,
    .with_cursor = true,
  };

  static const struct option longopts[] = {
    {"extract", no_argument, NULL, 'e'},
    {"extract-all", no_argument, NULL, 'a'},
    {"video", no_argument, NULL, 256},
    {"cursor", no_argument, NULL, 257},
    {"audio", no_argument, NULL, 258},
    {"txts", no_argument, NULL, 259},

    {"framerate", required_argument, NULL, 'r'},
    {"raw", no_argument, NULL, 'x'},
    {"modified", no_argument, NULL, 'm'},
    {"section", required_argument, NULL, 's'},
    {"frames", required_argument, NULL, 'n'},
    {"compression", required_argument, NULL, 'c'},
    {"threads", required_argument, NULL, 't'},

    {"unlock", no_argument, NULL, 'u'},
    {"set-key", required_argument, NULL, 260},

    {"key", required_argument, NULL, 'k'},
    {"force", no_argument, NULL, 'f'},

    {"verbose", no_argument, NULL, 'v'},
    {"debug", no_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},

    {0}
  };

  for (int option; (option = getopt_long(
       argc, argv, "-ear:xms:n:c:t:uk:fvdh", longopts, NULL)) != -1;) {
    if (option == 1) {
      if (options->input_path == NULL) {
        options->input_path = strdup(optarg);
        return_if_fail (options->input_path != NULL) -1;
      } else if (options->output_path == NULL) {
        options->output_path = strdup(optarg);
        return_if_fail (options->output_path != NULL) -1;
      } else {
        fputs("error: too many positional options\n", stderr);
        return -2;
      }
    } else if (option > 255) {
      switch (option) {
        case 256:
          options->extract_video = true;
          break;
        case 257:
          options->extract_cursor = true;
          break;
        case 258:
          options->extract_audio = true;
          break;
        case 259:
          options->extract_txts = true;
          break;
        case 260:
          if (options->new_password != NULL) {
            free(options->new_password);
          }
          if (optarg[0] == '\0') {
            options->new_password = NULL;
          } else {
            options->new_password = strdup(optarg);
            return_if_fail (options->new_password != NULL) -1;
          }
          options->set_password = true;
          break;
        default:
          return -2;
      }
    } else if ('A' <= option && option <= 'z') {
      switch (option) {
        case 'a':
          options->extract_video = true;
          options->extract_audio = true;
          options->extract_cursor = true;
          options->extract_txts = true;
          break;
        case 'e':
          options->extract_video = true;
          options->extract_audio = true;
          break;

        case 'r':
          if_fail (argtof(optarg, &options->fps, 0, 480) == 0) {
            fputs("error: framerate not a non-negative number\n", stderr);
            return -2;
          }
          break;
        case 'x':
          options->with_cursor = false;
          break;
        case 'm':
          options->use_subframes = true;
          break;
        case 's':
          if_fail (argtol(optarg, &options->section_i, 0, INT_MAX) == 0) {
            fputs("error: section index not a non-negative integer\n", stderr);
            return -2;
          }
          break;
        case 'n':
          if_fail (argtol(optarg, &options->frames_limit, 0, INT32_MAX) == 0) {
            fputs(
              "error: number of frame not a non-negative integer\n", stderr);
            return -2;
          }
          break;
        case 'c':
          if_fail (argtol(
              optarg, &options->compression_level, Z_NO_COMPRESSION,
              Z_BEST_COMPRESSION) == 0) {
            fputs("error: invalid compression level\n", stderr);
            return -2;
          }
          break;
        case 't':
#ifdef NO_THREADS
          fputs(
            "warning: threads not supported on this platform, forcing single "
            "thread\n", stderr);
#else
          if_fail (argtol(optarg, &options->nproc, 0, INT_MAX) == 0) {
            fputs(
              "error: number of thread not a non-negative integer\n", stderr);
            return -2;
          }
#endif
          break;

        case 'u':
          options->set_password = true;
          break;

        case 'k':
          if (options->password != NULL) {
            free(options->password);
          }
          if (optarg[0] == '\0') {
            options->password = NULL;
          } else {
            options->password = strdup(optarg);
            return_if_fail (options->password != NULL) -1;
          }
          break;
        case 'f':
          options->force = true;
          break;

        case 'v':
          options->verbose = true;
          break;
        case 'd':
          if (sc_log_level < SC_LOG_DEBUG) {
            sc_log_level = SC_LOG_DEBUG;
          } else {
            sc_log_level = SC_LOG_VERBOSE;
          }
          break;
        case 'h':
          usage(argv[0]);
          return 1;
        default:
          // getopt already print error for us
          // fprintf(stderr, "error: unknown option '%c'\n", option);
          return -2;
      }
    } else {
      return -2;
    }
  }

  if_fail (options->input_path != NULL) {
    fputs("error: missing input file\n", stderr);
    return -2;
  }

  bool actions_extract =
    options->extract_audio || options->extract_video ||
    options->extract_cursor || options->extract_txts;
  bool actions_modify = options->set_password;

  if (actions_extract + actions_modify > 1) {
    fputs(
      "error: can only specify one group of extract or modify actions\n",
      stderr);
    return -2;
  }

  if (options->output_path != NULL) {
    if (!actions_extract && !actions_modify) {
      options->extract_audio = true;
      options->extract_video = true;
      actions_extract = true;
    }

    if (actions_extract) {
      for (unsigned int i = strlen(options->output_path); i > 0; ) {
        i--;
        char c = options->output_path[i];
        if (c != '/' && c != '\\') {
          break;
        }
        options->output_path[i] = '\0';
      }
    }
  } else if (actions_extract || actions_modify) {
    size_t len = strlen(options->input_path);
    options->output_path = malloc(len + 64);
    return_if_fail (options->output_path != NULL) -1;
    memcpy(options->output_path, options->input_path, len + 1);

    char *output_ext = get_extension(options->output_path);

    if (actions_extract) {
      strcpy(output_ext == NULL ? options->output_path + len : output_ext - 1,
             ".extracted");
    } else if (actions_modify) {
      if (output_ext == NULL) {
        strcpy(options->output_path + len, ".modified");
      } else {
        sprintf(output_ext - 1, ".modified.%s",
                output_ext - options->output_path + options->input_path);
      }
    }
  }

  return 0;
}


static int do_dump (
    const struct PlzjOptions *options, const struct PlzjFile *pf,
    const char *path) {
  fmprintf(stdout, "File: %s\n", path);

  for (uint32_t i = 0; i < pf->sections_cnt; i++) {
    if (pf->extended) {
      fprintf(stdout, "\n====== Section %" PRIu32 " ======\n", i);
    }

    const struct Plzj *pl = pf->sections + i;

    Plzj_print_info(pl, stdout, pf->extended);
    if (options->verbose || options->frames_limit >= 0) {
      fputs("\n", stdout);
      Plzj_print_video(pl, stdout, options->frames_limit);
    }
  }

  return 0;
}


static int set_password (
    const struct PlzjOptions *options, struct PlzjFile *pf) {
  return_if_fail (pf->sections[0].key_set < 0) 1;

  if_fail (options->password != NULL && options->password[0] != '\0') {
    fputs(
      "error: video is play locked, but no password was provided; "
      "use -k to specify it\n", stderr);
    return -1;
  }

  int ret = PlzjFile_set_password_iconv(
    pf, options->password, options->force);
  if_fail (ret >= 0) {
    fputs("error: wrong password\n", stderr);
    return -1;
  }

  if (ret == 1) {
    fputs("warning: wrong password, but force continue as requested\n", stderr);
    return 1;
  }
  return 0;
}


static int do_extract (
    const struct PlzjOptions *options, struct PlzjFile *pf, int i) {
  if (i < 0) {
    if_fail (pf->sections_cnt <= 1) {
      fprintf(
        stderr, "error: file has %" PRIu32
        " sections, use '-s' to specify one (0-%" PRIu32 ")\n",
        pf->sections_cnt, pf->sections_cnt - 1);
      return -1;
    }
    i = 0;
  } else if_fail ((unsigned int) i < pf->sections_cnt) {
    fprintf(
      stderr, "error: file has %" PRIu32
      " sections, but section %d was requested\n", pf->sections_cnt, i);
    return -1;
  }

  if (options->extract_video || options->extract_cursor) {
    return_if_fail (set_password(options, pf) >= 0) -1;
  }

  const struct Plzj *pl = pf->sections + i;
  const char *dir = options->output_path;

  struct stat statbuf;
  int res;
  if (mstat(dir, &statbuf) != 0) {
    res = mmkdir(dir, 0755);
    if_fail (res == 0) {
      fmprintf(stderr, "error: failed to create output dir %s\n", dir);
      (void) ERR_STD(mmkdir);
      goto fail_err;
    }
  } else if (S_ISDIR(statbuf.st_mode) == 0) {
    fmprintf(
      stderr, "error: output path %s exists but is not a directory\n", dir);
    return -1;
  }

  const char *what;
  float fps = 0;

  if (options->extract_video || options->extract_cursor) {
    uint32_t frame_ms = le32toh(pl->video.frame_ms);
    fps = 1000. / frame_ms;
    unsigned int ratio = 1;
    if (options->with_cursor) {
      ratio = ((uint32_t) (options->fps * frame_ms) + 500) / 1000;
      if (ratio < 1) {
        ratio = 1;
      }
      fps *= ratio;
      if (options->verbose || fabs(fps - options->fps) > 0.01) {
        printf("Framerate set to %.2f.\n", fps);
      }
    }

    if_fail (Plzj_extract_video_or_cursor(
        pl, dir, options->frames_limit,
        options->with_cursor ? 2 : options->use_subframes ? 1 : 0, ratio - 1,
        options->compression_level, options->nproc,
        options->extract_video, options->extract_cursor) == 0) {
      what = "video / cursor";
      goto fail;
    }
    fputs("Video / cursor extracted.\n", stdout);
  }

  if (options->extract_audio) {
    if (pl->audio_offset == -1) {
      fputs("File does not contain audio.\n", stdout);
    } else {
      res = Plzj_extract_audio(pl, dir);
      if_fail (res >= 0) {
        what = "audio";
        goto fail;
      }
      if (res > 0) {
        fputs(
          "Audio header found, but no data exists. "
          "This could be a bug or data corruption.\n", stdout);
      } else {
        fputs("Audio extracted.\n", stdout);
      }
    }
  }

  if (options->extract_txts) {
    if_fail (Plzj_extract_txts(pl, dir) == 0) {
      what = "txt files";
      goto fail;
    }
    fputs("Mouse event and key frame txt files extracted.\n", stdout);
  }

  if (options->extract_video) {
    fputs("\nConvert it with ffmpeg:\n  ffmpeg ", stdout);
    if (options->extract_audio && pl->audio_offset != -1) {
      const char *audio_fn = "audio.wav";
      switch (le32toh(pl->player.audio_type)) {
        case PLZJ_AUDIO_MP3:
          audio_fn = "audio.mp3";
          break;
        case PLZJ_AUDIO_AAC:
          audio_fn = "audio.aac";
          break;
      }
      mprintf("-i '%s/%s' ", dir, audio_fn);
    }
    mprintf(
      "-i '%s" DIR_SEP_S "%s' -vf fps=%.2f "
      "-c:v libx264 -pix_fmt yuv444p10le -tune stillimage -preset veryslow "
      "'%s" DIR_SEP_S "video.mp4'\n",
      dir, options->with_cursor ? "video.apng" : "video_raw.apng", fps, dir);
  }
  return 0;

fail:
  fprintf(stderr, "error: failed to extract %s\n", what);
fail_err:
  sc_print_err(stderr, "  ", "");
  return -1;
}


static int do_modify (
    const struct PlzjOptions *options, const struct PlzjFile *pf) {
  if (options->set_password && (
      options->new_password == NULL || options->new_password[0] == '\0')) {
    if (pf->sections[0].key_set == 0) {
      if (!options->force) {
        fputs("error: video is not locked; use -f to force (copy it as is)\n",
              stderr);
        return -1;
      }
      fputs(
        "warning: video is not locked, but force continue (copy it as is) "
        "as requested\n", stderr);
    }
  }

  const char *what = NULL;
  int ret;

  FILE *file = mfopen(options->output_path, "w+b");
  if_fail (file != NULL) {
    ret = ERR_STD(mfopen);
    what = "open";
    goto fail_open;
  }

  if_fail (fseeko(pf->file, 0, SEEK_SET) == 0) {
    ret = ERR_STD(fseeko);
    what = "copy";
    goto fail_file;
  }
  ret = copy(file, pf->file, pf->file_size, 0);
  if_fail (ret == 0) {
    ret = ERR_STD(fseeko);
    what = "copy";
    goto fail_file;
  }

  struct PlzjFile pf2;
  ret = PlzjFile_init(&pf2, file);
  if_fail (ret == 0) {
    what = "read";
    goto fail_file;
  }

  if (options->set_password) {
    goto_if_fail (set_password(options, &pf2) >= 0) fail;

    ret = PlzjFile_set_playlock_iconv(&pf2, options->new_password);
    if_fail (ret == 0) {
      what = "change password of";
      goto fail;
    }
  }

  mprintf("Saved modified video to %s .\n", options->output_path);
  if (options->verbose) {
    pf2.file = NULL;
    PlzjFile_destroy(&pf2);

    ret = PlzjFile_init(&pf2, file);
    if_fail (ret == 0) {
      what = "read";
      goto fail_file;
    }

    fputs("\n", stdout);
    do_dump(options, &pf2, options->output_path);
  }

  PlzjFile_destroy(&pf2);
  return 0;

  if (0) {
fail:
    PlzjFile_destroy(&pf2);
  }
  if (0) {
fail_file:
    fclose(file);
  }
  munlink(options->output_path);
fail_open:
  if (what != NULL) {
    fprintf(stderr, "error: failed to %s video file\n", what);
    sc_print_err(stderr, "  ", "");
  }
  return -1;
}


int main (int argc, char **argv) {
  set_margv(argc, argv);

  if (argc <= 1) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  struct PlzjOptions options;
  int ret = EXIT_FAILURE;

  switch (get_options(&options, argc, argv)) {
    case 1:
      goto end_arg;
    case -1:
      goto oom;
    case -2:
      goto fail_arg;
  }

  struct PlzjFile pf;
  if_fail (PlzjFile_init_file(&pf, options.input_path, "rb") == 0) {
    fmprintf(stderr, "error: failed to load input file \"%s\"\n",
             options.input_path);
    sc_print_err(stderr, "  ", "");
    goto fail_arg;
  }

  if (options.output_path == NULL) {
    do_dump(&options, &pf, options.input_path);
  } else if (options.extract_video || options.extract_cursor ||
             options.extract_audio || options.extract_txts) {
    goto_if_fail (do_extract(&options, &pf, options.section_i) == 0) fail;
  } else {
    goto_if_fail (do_modify(&options, &pf) == 0) fail;
  }

  ret = EXIT_SUCCESS;
fail:
  PlzjFile_destroy(&pf);
  if (0) {
oom:
    fputs("error: out of memory\n", stderr);
    ret = 255;
  }
  if (0) {
end_arg:
    ret = EXIT_SUCCESS;
  }
fail_arg:
  free(options.new_password);
  free(options.password);
  free(options.output_path);
  free(options.input_path);
  return ret;
}
