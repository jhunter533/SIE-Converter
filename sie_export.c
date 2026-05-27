#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <sie.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define VERSION "1.0"

void print_usage(const char *programName) {
  printf("Usage: %s [OPTIONS] <input.sie>\n\n", programName);
  printf("Convert SoMat SIE data files to CSV format.\n\n");
  printf("Options:\n");
  printf(" -c,--channel ID\tOnly export specific channel (can be used multiple "
         "times)\n");
  printf(" -o,--output DIR\tOutput directory for CSV files (default: current "
         "directory)\n");
  printf(
      " -p,--prefix PREFIX\tPrefix for output files (default: channel name)\n");
  printf(" -s,--skip-missing\tSkip channels with no data instead of exiting "
         "with an error\n");
  printf(" -v,--verbose\tPrint progress information\n");
  printf(" -h,--help\tShow this help message\n");
  printf(" --version\tShow version information\n\n");
  printf("Examples:\n");
  printf(" %s data.sie #Export all channels\n", programName);
  printf(" %s -c 19 -c 20 data.sie #Export only channels 19 and 20\n",
         programName);
}

void print_version() { printf("sie_to_csv version %s\n", VERSION); }

void sanitize_filename(const char *src, char *dst, size_t dst_size) {
  char *d = dst;
  const char *s = src;
  while (*s && (d - dst) < dst_size - 1) {
    if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
        (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' || *s == '.') {
      *d++ = *s;
    } else if (*s == ' ' || *s == '/' || *s == '\\' || *s == ':') {
      *d++ = '_';
    }
    s++;
  }
  *d = '\0';
}

double getChannelSampleRate(void *ch) {
  void *dim = sie_get_dimension(ch, 0);
  if (dim) {
    void *rate_tag = sie_get_tag(dim, "core:sample_rate");
    if (rate_tag) {
      char *rate_str = sie_tag_get_value(rate_tag);
      if (rate_str) {
        double rate = atof(rate_str);
        sie_free(rate_str);
        return rate;
      }
    }
  }
  return 0.0;
}

size_t get_total_samples(void *ch) {
  void *tag = sie_get_tag(ch, "core:output_samples");
  if (tag) {
    char *val = sie_tag_get_value(tag);
    if (val) {
      size_t samples = (size_t)atoll(val);
      sie_free(val);
      return samples;
    }
  }
  return 0;
}

int exportChannel(void *ch, const char *output_dir, const char *prefix,
                  int verbose) {
  unsigned int chID = sie_get_id(ch);
  const char *chName = sie_get_name(ch);

  if (verbose) {
    printf("Processing channel %u: %s\n", chID, chName);
  }

  void *spigot = sie_attach_spigot(ch);
  if (!spigot) {
    fprintf(stderr, "Error: Failed to attach spigot to channel %u (%s)\n", chID,
            chName);
    return -1;
  }

  char filename[2048];
  char safeName[2048];
  if (prefix && strlen(prefix) > 0) {
    if (output_dir[strlen(output_dir) - 1] == '/')
      snprintf(filename, sizeof(filename), "%s%s_ch%u.csv", output_dir, prefix,
               chID);
    else
      snprintf(filename, sizeof(filename), "%s/%s_ch%u.csv", output_dir, prefix,
               chID);
  } else {
    sanitize_filename(chName, safeName, sizeof(safeName));
    if (output_dir[strlen(output_dir) - 1] == '/')
      snprintf(filename, sizeof(filename), "%s%s_ch%u.csv", output_dir,
               safeName, chID);
    else
      snprintf(filename, sizeof(filename), "%s/%s_ch%u.csv", output_dir,
               safeName, chID);
  }

  FILE *out = fopen(filename, "w");
  if (!out) {
    fprintf(stderr, "Error: Cannot create %s: %s\n", filename, strerror(errno));
    sie_release(spigot);
    return -1;
  }

  fprintf(out, "time_sec,%s\n", chName);

  size_t total_expected = get_total_samples(ch);
  size_t total_samples = 0;
  int block_count = 0;
  void *output;

  size_t last_update_samples = 0;
  struct timeval last_tv;
  gettimeofday(&last_tv, NULL);

  while ((output = sie_spigot_get(spigot)) != NULL) {
    size_t num_rows = sie_output_get_num_rows(output);
    size_t num_dims = sie_output_get_num_dims(output);

    if (num_rows == 0 || num_dims < 2) {
      continue;
    }

    if (sie_output_get_type(output, 0) == SIE_OUTPUT_FLOAT64 &&
        sie_output_get_type(output, 1) == SIE_OUTPUT_FLOAT64) {
      sie_float64 *time_data = sie_output_get_float64(output, 0);
      sie_float64 *value_data = sie_output_get_float64(output, 1);
      if (time_data && value_data) {
        for (size_t i = 0; i < num_rows; i++) {
          fprintf(out, "%f,%f\n", time_data[i], value_data[i]);
        }
        total_samples += num_rows;
        block_count++;

        int need_update = 0;
        if (total_expected > 0) {
          double pct = 100.0 * total_samples / total_expected;
          double last_pct = 100.0 * last_update_samples / total_expected;
          if (pct - last_pct >= 0.5)
            need_update = 1;
        } else if (total_samples - last_update_samples >= 10000) {
          need_update = 1;
        }

        if (need_update) {
          struct timeval now;
          gettimeofday(&now, NULL);
          long diff_ms = (now.tv_sec - last_tv.tv_sec) * 1000 +
                         (now.tv_usec - last_tv.tv_usec) / 1000;
          if (diff_ms >= 100) {
            if (total_expected > 0) {
              int percent = (int)(100.0 * total_samples / total_expected);
              int bars = percent / 2;
              fprintf(stderr, "\r[");
              for (int i = 0; i < 50; i++) {
                fputc(i < bars ? '#' : '-', stderr);
              }
              fprintf(stderr, "] %3d%%  %zu / %zu samples", percent,
                      total_samples, total_expected);
            } else {
              fprintf(stderr, "\r[");
              int spin = (block_count / 100) % 4;
              const char *spin_chars = "|/-\\";
              fputc(spin_chars[spin], stderr);
              fprintf(stderr, "] %d blocks, %zu samples", block_count,
                      total_samples);
            }
            fflush(stderr);
            last_update_samples = total_samples;
            last_tv = now;
          }
        }
      }
    }
  }

  if (total_expected > 0) {
    fprintf(stderr,
            "\r[##################################################] 100%%  %zu "
            "/ %zu samples\n",
            total_samples, total_expected);
  } else {
    fprintf(stderr, "\r %d blocks, %zu samples\n", block_count, total_samples);
  }

  fclose(out);
  sie_release(spigot);

  if (total_samples == 0) {
    if (verbose) {
      fprintf(stderr,
              "Warning: Channel %u (%s) has no float64 data in dimensions 0/1, "
              "skipping\n",
              chID, chName);
    }
    remove(filename);
    return -1;
  }

  if (verbose) {
    printf("Exported channel %u (%s): %zu samples in %d blocks -> %s\n", chID,
           chName, total_samples, block_count, filename);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int opt;
  int channel_ids[256];
  int num_channel_ids = 0;
  char output_dir[1024] = ".";
  char prefix[1024] = "";
  int skip_missing = 0;
  int verbose = 0;

  static struct option long_options[] = {{"channel", required_argument, 0, 'c'},
                                         {"output", required_argument, 0, 'o'},
                                         {"prefix", required_argument, 0, 'p'},
                                         {"skip-missing", no_argument, 0, 's'},
                                         {"verbose", no_argument, 0, 'v'},
                                         {"help", no_argument, 0, 'h'},
                                         {"version", no_argument, 0, 0},
                                         {0, 0, 0, 0}};

  while ((opt = getopt_long(argc, argv, "c:o:p:svh", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'c':
      if (num_channel_ids < 256)
        channel_ids[num_channel_ids++] = atoi(optarg);
      break;
    case 'o':
      strncpy(output_dir, optarg, sizeof(output_dir) - 1);
      output_dir[sizeof(output_dir) - 1] = '\0';
      break;
    case 'p':
      strncpy(prefix, optarg, sizeof(prefix) - 1);
      prefix[sizeof(prefix) - 1] = '\0';
      break;
    case 's':
      skip_missing = 1;
      break;
    case 'v':
      verbose = 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 0:
      if (strcmp(long_options[optind - 1].name, "version") == 0) {
        print_version();
        return 0;
      }
      break;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "Error: No input file specified\n\n");
    print_usage(argv[0]);
    return 1;
  }

  const char *sie_file = argv[optind];

  if (strcmp(output_dir, ".") != 0) {
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", output_dir);
    if (system(mkdir_cmd) != 0)
      fprintf(stderr, "Warning: could not create output directory %s\n",
              output_dir);
  }

  if (verbose)
    printf("Opening file: %s\n", sie_file);

  void *context = sie_context_new();
  if (!context) {
    fprintf(stderr, "Error: Failed to create library context\n");
    return 1;
  }

  void *file = sie_file_open(context, sie_file);
  if (!file) {
    void *exception = sie_get_exception(context);
    if (exception) {
      char *msg = sie_verbose_report(exception);
      fprintf(stderr, "Error: Failed to open %s: %s\n", sie_file, msg);
      sie_release(exception);
    } else {
      fprintf(stderr, "Error: Failed to open %s\n", sie_file);
    }
    sie_context_done(context);
    return 1;
  }

  void *channel_iterator = sie_get_channels(file);
  if (!channel_iterator) {
    fprintf(stderr, "Error: Failed to get channel iterator\n");
    sie_release(file);
    sie_context_done(context);
    return 1;
  }

  int exported = 0;
  int errors = 0;
  int channel_count = 0;
  void *ch;

  while ((ch = sie_iterator_next(channel_iterator)) != NULL) {
    channel_count++;
    unsigned int ch_id = sie_get_id(ch);
    const char *ch_name = sie_get_name(ch);

    if (verbose) {
      printf("Found channel %u: %s\n", ch_id, ch_name);
    }

    int should_export = 0;
    if (num_channel_ids == 0) {
      should_export = 1;
    } else {
      for (int j = 0; j < num_channel_ids; j++) {
        if (channel_ids[j] == (int)ch_id) {
          should_export = 1;
          break;
        }
      }
    }

    if (should_export) {
      int result = exportChannel(ch, output_dir, prefix, verbose);
      if (result == 0) {
        exported++;
      } else if (!skip_missing) {
        errors++;
        fprintf(stderr, "Error: Failed to export channel %u\n", ch_id);
      }
    }
  }

  sie_release(channel_iterator);
  sie_release(file);

  int leaked = sie_context_done(context);
  if (leaked != 0 && verbose)
    fprintf(stderr, "Warning: Leaked %d SIE objects\n", leaked);

  if (verbose) {
    printf("\nSummary: Exported %d of %d channels\n", exported, channel_count);
    if (errors > 0)
      printf("Errors: %d\n", errors);
  }

  return (errors > 0) ? 1 : 0;
}
