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
  printf("Convert SoMat SIE data files to a single CSV.\n\n");
  printf("Options:\n");
  printf(" -c,--channel ID\tOnly export specific channel (can be used multiple "
         "times)\n");
  printf(" -o,--output DIR\tOutput directory for CSV file (default: current "
         "directory)\n");
  printf(" -s,--skip-missing\tSkip channels with no data instead of exiting "
         "with an error\n");
  printf(" -v,--verbose\tPrint progress information\n");
  printf(" -h,--help\tShow this help message\n");
  printf(" --version\tShow version information\n\n");
  printf("Examples:\n");
  printf(
      "  %s data.sie                       # Export all channels to data.csv\n",
      programName);
  printf(
      "  %s -c 32 -c 33 data.sie           # Export only channels 32 and 33\n",
      programName);
}

void print_version() { printf("sie_to_csv version %s\n", VERSION); }

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

char *get_channel_units(void *ch) {
  void *dim = sie_get_dimension(ch, 1);
  if (!dim)
    return strdup("");
  void *tag = sie_get_tag(dim, "core:units");
  if (!tag)
    return strdup("");
  char *units = sie_tag_get_value(tag);
  if (!units)
    return strdup("");
  char *result = strdup(units);
  sie_free(units);
  return result;
}

char *get_test_id_string(void *file) {
  void *test_iter = sie_get_tests(file);
  if (!test_iter)
    return strdup("");
  void *test = sie_iterator_next(test_iter);
  if (!test) {
    sie_release(test_iter);
    return strdup("");
  }
  void *tag = sie_get_tag(test, "core:description");
  sie_release(test_iter);
  if (!tag)
    return strdup("");
  char *desc = sie_tag_get_value(tag);
  if (!desc)
    return strdup("");
  char *out = malloc(strlen(desc) + 8);
  if (!out)
    return strdup("");
  char *p = desc;
  char *o = out;
  while (*p) {
    if (*p != ' ' && *p != ':') {
      *o++ = *p;
    }
    p++;
  }
  strcpy(o, "_Master");
  sie_free(desc);
  return out;
}

char *get_channel_short_name(const char *full_name) {
  const char *at = strchr(full_name, '@');
  if (!at)
    return strdup(full_name);
  const char *start = at + 1;
  const char *dot = strrchr(start, '.');
  if (!dot)
    return strdup(start);
  size_t len = dot - start;
  char *buf = malloc(len + 1);
  if (!buf)
    return strdup("");
  strncpy(buf, start, len);
  buf[len] = '\0';
  return buf;
}

typedef struct {
  void *channel;
  unsigned int id;
  char *short_name;
  char *units;
  double *data;
} channel_info;

int main(int argc, char *argv[]) {
  int opt;
  int channel_ids[256];
  int num_channel_ids = 0;
  char output_dir[1024] = ".";
  int skip_missing = 0;
  int verbose = 0;

  static struct option long_options[] = {{"channel", required_argument, 0, 'c'},
                                         {"output", required_argument, 0, 'o'},
                                         {"skip-missing", no_argument, 0, 's'},
                                         {"verbose", no_argument, 0, 'v'},
                                         {"help", no_argument, 0, 'h'},
                                         {"version", no_argument, 0, 0},
                                         {0, 0, 0, 0}};

  while ((opt = getopt_long(argc, argv, "c:o:svh", long_options, NULL)) != -1) {
    switch (opt) {
    case 'c':
      if (num_channel_ids < 256)
        channel_ids[num_channel_ids++] = atoi(optarg);
      break;
    case 'o':
      strncpy(output_dir, optarg, sizeof(output_dir) - 1);
      output_dir[sizeof(output_dir) - 1] = '\0';
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

  char *test_id = get_test_id_string(file);

  void *channel_iterator = sie_get_channels(file);
  if (!channel_iterator) {
    fprintf(stderr, "Error: Failed to get channel iterator\n");
    free(test_id);
    sie_release(file);
    sie_context_done(context);
    return 1;
  }

  channel_info *channels = NULL;
  int channel_count = 0;
  void *ch;
  while ((ch = sie_iterator_next(channel_iterator)) != NULL) {
    unsigned int ch_id = sie_get_id(ch);
    int export_this = 0;
    if (num_channel_ids == 0) {
      export_this = 1;
    } else {
      for (int j = 0; j < num_channel_ids; j++) {
        if (channel_ids[j] == (int)ch_id) {
          export_this = 1;
          break;
        }
      }
    }
    if (export_this) {
      channels = realloc(channels, (channel_count + 1) * sizeof(channel_info));
      if (!channels) {
        fprintf(stderr, "Memory allocation failed\n");
        free(test_id);
        sie_release(channel_iterator);
        sie_release(file);
        sie_context_done(context);
        return 1;
      }
      const char *full_name = sie_get_name(ch);
      channels[channel_count].channel = sie_retain(ch); // retain to keep alive
      channels[channel_count].id = ch_id;
      channels[channel_count].short_name = get_channel_short_name(full_name);
      channels[channel_count].units = get_channel_units(ch);
      channels[channel_count].data = NULL;
      channel_count++;
      if (verbose) {
        printf("Selected channel %u: %s [%s]\n", ch_id,
               channels[channel_count - 1].short_name,
               channels[channel_count - 1].units);
      }
    }
  }
  sie_release(channel_iterator); // safe to release now – channels are retained

  if (channel_count == 0) {
    fprintf(stderr, "No channels selected for export.\n");
    free(test_id);
    sie_release(file);
    sie_context_done(context);
    return 1;
  }

  size_t total_samples = get_total_samples(channels[0].channel);
  if (total_samples == 0) {
    fprintf(stderr, "Error: Could not determine total number of samples.\n");
    for (int i = 0; i < channel_count; i++) {
      free(channels[i].short_name);
      free(channels[i].units);
      sie_release(channels[i].channel);
    }
    free(channels);
    free(test_id);
    sie_release(file);
    sie_context_done(context);
    return 1;
  }
  if (verbose) {
    printf("Total samples per channel: %zu\n", total_samples);
  }

  double *time_data = malloc(total_samples * sizeof(double));
  if (!time_data) {
    fprintf(stderr, "Memory allocation failed for time data.\n");
    for (int i = 0; i < channel_count; i++) {
      free(channels[i].short_name);
      free(channels[i].units);
      sie_release(channels[i].channel);
    }
    free(channels);
    free(test_id);
    sie_release(file);
    sie_context_done(context);
    return 1;
  }

  for (int i = 0; i < channel_count; i++) {
    channels[i].data = malloc(total_samples * sizeof(double));
    if (!channels[i].data) {
      fprintf(stderr, "Memory allocation failed for channel %s\n",
              channels[i].short_name);
      for (int j = 0; j < i; j++)
        free(channels[j].data);
      free(time_data);
      for (int j = 0; j < channel_count; j++) {
        free(channels[j].short_name);
        free(channels[j].units);
        sie_release(channels[j].channel);
      }
      free(channels);
      free(test_id);
      sie_release(file);
      sie_context_done(context);
      return 1;
    }
  }

  struct timeval last_tv;
  gettimeofday(&last_tv, NULL);

  for (int idx = 0; idx < channel_count; idx++) {
    void *spigot = sie_attach_spigot(channels[idx].channel);
    if (!spigot) {
      fprintf(stderr, "Error: Failed to attach spigot to channel %s\n",
              channels[idx].short_name);
      continue;
    }
    void *output;
    size_t samples_read = 0;
    while ((output = sie_spigot_get(spigot)) != NULL) {
      size_t num_rows = sie_output_get_num_rows(output);
      size_t num_dims = sie_output_get_num_dims(output);
      if (num_rows == 0 || num_dims < 2)
        continue;
      if (sie_output_get_type(output, 0) == SIE_OUTPUT_FLOAT64 &&
          sie_output_get_type(output, 1) == SIE_OUTPUT_FLOAT64) {
        sie_float64 *tdata = sie_output_get_float64(output, 0);
        sie_float64 *vdata = sie_output_get_float64(output, 1);
        if (tdata && vdata) {
          for (size_t i = 0; i < num_rows; i++) {
            if (idx == 0) {
              time_data[samples_read + i] = tdata[i];
            }
            channels[idx].data[samples_read + i] = vdata[i];
          }
          samples_read += num_rows;
        }
      }
    }
    sie_release(spigot);
    if (samples_read != total_samples) {
      fprintf(stderr, "Warning: Channel %s read %zu samples, expected %zu\n",
              channels[idx].short_name, samples_read, total_samples);
    }

    struct timeval now;
    gettimeofday(&now, NULL);
    long diff_ms = (now.tv_sec - last_tv.tv_sec) * 1000 +
                   (now.tv_usec - last_tv.tv_usec) / 1000;
    if (diff_ms >= 100) {
      int percent = (int)(100.0 * (idx + 1) / channel_count);
      int bars = percent / 2;
      fprintf(stderr, "\r[");
      for (int i = 0; i < 50; i++) {
        fputc(i < bars ? '#' : '-', stderr);
      }
      fprintf(stderr, "] %3d%%  Loading channel %d/%d", percent, idx + 1,
              channel_count);
      fflush(stderr);
      last_tv = now;
    }
  }
  fprintf(stderr, "\r[##################################################] "
                  "100%%  Channels loaded\n");

  char output_filename[2048];
  const char *base = basename((char *)sie_file);
  char *dot = strrchr(base, '.');
  if (dot)
    *dot = '\0';
  if (output_dir[strlen(output_dir) - 1] == '/')
    snprintf(output_filename, sizeof(output_filename), "%s%s.csv", output_dir,
             base);
  else
    snprintf(output_filename, sizeof(output_filename), "%s/%s.csv", output_dir,
             base);

  FILE *out = fopen(output_filename, "w");
  if (!out) {
    fprintf(stderr, "Error: Cannot create %s: %s\n", output_filename,
            strerror(errno));
    for (int i = 0; i < channel_count; i++)
      free(channels[i].data);
    free(time_data);
    for (int i = 0; i < channel_count; i++) {
      free(channels[i].short_name);
      free(channels[i].units);
      sie_release(channels[i].channel);
    }
    free(channels);
    free(test_id);
    sie_release(file);
    sie_context_done(context);
    return 1;
  }

  fprintf(out, "%s", test_id);
  for (int i = 0; i < channel_count; i++) {
    fprintf(out, ",%s", test_id);
  }
  fprintf(out, "\n");

  fprintf(out, "time_sec");
  for (int i = 0; i < channel_count; i++) {
    fprintf(out, ",%s", channels[i].short_name);
  }
  fprintf(out, "\n");

  fprintf(out, "seconds");
  for (int i = 0; i < channel_count; i++) {
    fprintf(out, ",%s", channels[i].units);
  }
  fprintf(out, "\n");

  for (size_t row = 0; row < total_samples; row++) {
    fprintf(out, "%.15g", time_data[row]);
    for (int i = 0; i < channel_count; i++) {
      fprintf(out, ",%.15g", channels[i].data[row]);
    }
    fprintf(out, "\n");
    if (row % 1000000 == 0 && row > 0) {
      int percent = (int)(100.0 * row / total_samples);
      int bars = percent / 2;
      fprintf(stderr, "\r[");
      for (int i = 0; i < 50; i++) {
        fputc(i < bars ? '#' : '-', stderr);
      }
      fprintf(stderr, "] %3d%%  Writing row %zu / %zu", percent, row,
              total_samples);
      fflush(stderr);
    }
  }
  fprintf(stderr, "\r[##################################################] "
                  "100%%  Writing complete\n");

  fclose(out);

  if (verbose) {
    printf("Exported %zu rows, %d columns to %s\n", total_samples,
           channel_count + 1, output_filename);
  }

  for (int i = 0; i < channel_count; i++) {
    free(channels[i].data);
    free(channels[i].short_name);
    free(channels[i].units);
  }
  free(channels);
  free(time_data);
  free(test_id);
  sie_release(file);
  int leaked = sie_context_done(context);
  if (leaked != 0 && verbose)
    fprintf(stderr, "Warning: Leaked %d SIE objects\n", leaked);

  return 0;
}
