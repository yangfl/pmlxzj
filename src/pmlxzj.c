#define _BSD_SOURCE

#include <endian.h>
#include <getopt.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/include/parser.h"
#include "lib/utils.h"
#include "lib/macro.h"
#include "lib/log.h"


struct PmlxzjOptions {
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
  long frames_limit;
  long nproc;
  bool use_subframes;
  bool with_cursor;
  bool force;
  bool verbose;
};


static void usage (const char *progname) {
  fputs("Usage: ", stdout);
  fputs(progname, stdout);
  fputs(" [OPTIONS]... <video.exe> [<output>]\n\
  <video.exe>    video.exe to use\n\
  <output>       output path\n\
\n\
If <output> is not specified and no actions are specified, print input file\n\
info\n\
\n\
If <output> is specified and no actions are specified, extract all components\n\
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
                        smooth cursor movement by interpolation (ignored if '-r'\n\
                        specified) (default: 30)\n\
  -x, --raw             do not draw cursor in the output video file\n\
  -m, --modified        use modified APNG, suitable for archiving (ignored if\n\
                        no '-r' specified)\n\
        Further reduce file size by additional transition frames, with their\n\
        delay times set to 1 ms. Some players might enforce a minimal delay time\n\
        for one frame (100 ms for example), but ffmpeg can process it correctly.\n\
  -n, --frames <n>      only process first <n> frames\n\
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


static int get_options (struct PmlxzjOptions *options, int argc, char **argv) {
  *options = (struct PmlxzjOptions) {
    .fps = 30,
    .frames_limit = -1,
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
    {"frames", required_argument, NULL, 'n'},
    {"threads", required_argument, NULL, 't'},

    {"unlock", no_argument, NULL, 'u'},
    {"set-key", required_argument, NULL, 260},

    {"key", required_argument, NULL, 'k'},
    {"force", no_argument, NULL, 'f'},

    {"verbose", no_argument, NULL, 'v'},
    {"debug", no_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {}
  };

  for (int option; (option = getopt_long(
       argc, argv, "-ear:xmn:t:uk:fvdh", longopts, NULL)) != -1;) {
    switch (option) {
      case 1:
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
        break;

      case 'a':
        options->extract_cursor = true;
        options->extract_txts = true;
        // fallthrough
      case 'e':
        options->extract_video = true;
        options->extract_audio = true;
        break;
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

      case 'r':
        if_fail (argtof(optarg, &options->fps, 0, 480) == 0) {
          fputs("error: framerate not a positive number\n", stderr);
          return -2;
        }
        break;
      case 'x':
        options->with_cursor = false;
        break;
      case 'm':
        options->use_subframes = true;
        break;
      case 'n':
        if_fail (argtol(optarg, &options->frames_limit, 0, INT32_MAX) == 0) {
          fputs("error: number of frame not a positive number\n", stderr);
          return -2;
        }
        break;
      case 't':
        if_fail (argtol(optarg, &options->nproc, 0, INT_MAX) == 0) {
          fputs("error: number of thread not a positive number\n", stderr);
          return -2;
        }
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
        // fallthrough
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
        sc_log_level = SC_LOG_DEBUG;
        break;
      case 'h':
        usage(argv[0]);
        return 1;
      default:
        // getopt already print error for us
        // fprintf(stderr, "error: unknown option '%c'\n", option);
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
      options->extract_cursor = true;
      options->extract_txts = true;
    }

    if (actions_extract) {
      for (unsigned int i = strlen(options->output_path);
          i > 0 && options->output_path[i - 1] == '/'; i--) {
        options->output_path[i - 1] = '\0';
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
    const struct PmlxzjOptions *options, const struct Pmlxzj *pl,
    const char *path) {
  fprintf(stdout, "File: %s\n", path);
  Pmlxzj_print_info(pl, stdout);
  if (options->verbose || options->frames_limit >= 0) {
    fputs("\n", stdout);
    Pmlxzj_print_video(pl, stdout, options->frames_limit);
  }
  return 0;
}


static int set_password (
    const struct PmlxzjOptions *options, struct Pmlxzj *pl) {
  if (pl->encrypted < 0) {
    if_fail (options->password != NULL && options->password[0] != '\0') {
      fputs(
        "error: video is play locked, but no password was provided; "
        "use -k to specify it\n", stderr);
      return 1;
    }
    if_fail (Pmlxzj_set_password_iconv(
        pl, options->password, options->force) == 0) {
      if (!options->force) {
        fputs("error: wrong password\n", stderr);
        return 1;
      }
      fputs("warning: wrong password, but force continue as requested\n",
            stderr);
    }
  }
  return 0;
}


static int do_extract (
    const struct PmlxzjOptions *options, const struct Pmlxzj *pl) {
  const char *dir = options->output_path;

  struct stat statbuf;
  if (stat(dir, &statbuf) != 0) {
    if_fail (mkdir(dir, 0755) == 0) {
      fprintf(stderr, "error: failed to create output dir %s\n  ", dir);
      (void) ERR_SYS(mkdir);
      goto fail_err;
    }
  } else if (S_ISDIR(statbuf.st_mode) == 0) {
    fprintf(
      stderr, "error: output path %s exists but is not a directory\n", dir);
    return 1;
  }

  const char *what;
  float fps = 0;

  if (options->extract_video || options->extract_cursor) {
    uint32_t frame_ms = le32toh(pl->video.frame_ms);
    fps = 1000. / frame_ms;
    unsigned int ratio = 1;
    if (options->with_cursor) {
      ratio = (options->fps * frame_ms + 500) / 1000;
      if (ratio < 1) {
        ratio = 1;
      }
      fps *= ratio;
      if (options->verbose || fabs(fps - options->fps) > 0.01) {
        printf("FPS is set to %.2f\n", fps);
      }
    }

    if_fail (Pmlxzj_extract_video_or_cursor(
        pl, dir, options->frames_limit,
        options->with_cursor ? 2 : options->use_subframes ? 1 : 0, ratio,
        options->nproc, options->extract_video, options->extract_cursor) == 0) {
      what = "video / cursor";
      goto fail;
    }
    fputs("Video / cursor extracted.\n", stdout);
  }

  if (options->extract_audio) {
    if (pl->audio_offset == -1) {
      fputs("File does not contain audio.\n", stdout);
    } else {
      if_fail (Pmlxzj_extract_audio(pl, dir) == 0) {
        what = "audio";
        goto fail;
      }
      fputs("Audio extracted.\n", stdout);
    }
  }

  if (options->extract_txts) {
    if_fail (Pmlxzj_extract_txts(pl, dir) == 0) {
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
        case PMLXZJ_AUDIO_MP3:
          audio_fn = "audio.mp3";
          break;
        case PMLXZJ_AUDIO_AAC:
          audio_fn = "audio.aac";
          break;
      }
      printf("-i '%s/%s' ", dir, audio_fn);
    }
    printf(
      "-i '%s/%s' -r %.2f "
      "-c:v libx264 -pix_fmt yuv444p10le -tune stillimage -preset veryslow "
      "'%s/video.mp4'\n",
      dir, options->with_cursor ? "video.apng" : "video_raw.apng", fps, dir);
  }
  return 0;

fail:
  fprintf(stderr, "error: failed to extract %s\n", what);
fail_err:
  sc_print_err(stderr, "  ");
  return 1;
}


static int do_modify (
    const struct PmlxzjOptions *options, const struct Pmlxzj *pl) {
  if (options->set_password && (
      options->new_password == NULL || options->new_password[0] == '\0')) {
    if (pl->encrypted == 0) {
      if (!options->force) {
        fputs("error: video is not locked; use -f to force (copy it as is)\n",
              stderr);
        return 1;
      }
      fputs(
        "warning: video is not locked, but force continue (copy it as is) "
        "as requested\n", stderr);
    }
  }

  const char *what;

  struct Pmlxzj plu = *pl;
  plu.file = fopen(options->output_path, "w+b");
  if_fail (plu.file != NULL) {
    (void) ERR_SYS(fopen);
    what = "open";
    goto fail_open;
  }

  what = "copy";
  if_fail (fseeko(pl->file, 0, SEEK_SET) == 0) {
    (void) ERR_SYS(fseeko);
    goto fail;
  }
  goto_if_fail (copy(plu.file, pl->file, pl->file_size) == 0) fail;

  if (options->set_password) {
    if_fail (Pmlxzj_set_playlock_iconv(&plu, options->new_password) == 0) {
      what = "change password of";
      goto fail;
    }
  }

  printf("Saved modified video to %s .\n", options->output_path);
  if (options->verbose) {
    fputs("\n", stdout);
    Pmlxzj_init(&plu, plu.file);
    do_dump(options, &plu, options->output_path);
  }
  Pmlxzj_destroy(&plu);
  return 0;

fail:
  unlink(options->output_path);
fail_open:
  fprintf(stderr, "error: failed to %s video file\n", what);
  sc_print_err(stderr, "  ");
  Pmlxzj_destroy(&plu);
  return 1;
}


int main (int argc, char **argv) {
  if (argc <= 1) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  setlocale(LC_ALL, "");

  struct PmlxzjOptions options;
  int ret = EXIT_FAILURE;

  switch (get_options(&options, argc, argv)) {
    case 1:
      goto end_arg;
    case -1:
      goto oom;
    case -2:
      goto fail_arg;
  }

  struct Pmlxzj pl;
  if_fail (Pmlxzj_init_file(&pl, options.input_path, "rb") == 0) {
    fprintf(stderr, "error: failed to load input file \"%s\"\n",
            options.input_path);
    sc_print_err(stderr, "  ");
    goto fail_arg;
  }

  if (options.output_path == NULL) {
    do_dump(&options, &pl, options.input_path);
  } else {
    if (options.extract_video || options.extract_cursor ||
        options.set_password) {
      goto_if_fail (set_password(&options, &pl) == 0) fail;
    }
    if (options.extract_video || options.extract_cursor ||
        options.extract_audio || options.extract_txts) {
      goto_if_fail (do_extract(&options, &pl) == 0) fail;
    } else {
      goto_if_fail (do_modify(&options, &pl) == 0) fail;
    }
  }

  ret = EXIT_SUCCESS;
fail:
  Pmlxzj_destroy(&pl);
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
