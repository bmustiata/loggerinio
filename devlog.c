#include "devlog.h"

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#define DEV_LOG_DEFAULT_OUTPUT_PATH "C:\\temp\\devlog.log"
#else
#define DEV_LOG_DEFAULT_OUTPUT_PATH "/tmp/devlog.log"
#endif

#define DEV_LOG_READ_CONFIG_POLL_MS 5000L

char** DEV_LOG_EXCLUDED_MATCHES = 0;
int dev_excluded_matches_count = 0;
char dev_log_config_file_name[8192] = {};
char dev_log_output_file_name[8192] = {};
unsigned char dev_log_initial_config_happened = 0;
FILE* dev_log_output_file = 0;

long long dev_last_time_config_was_read = -1;
long long dev_readed_config_file_mtime = -1;

// forward definitions
void dev_log_output_line(const char *, ...);
unsigned char dev_is_line_excluded(const char*);
void dev_log_line_into_output(const char*);
unsigned char dev_log_last_time_config_read_was_long_ago();
unsigned char dev_log_config_file_has_changed();
void dev_log_reread_config_file_for_exclusions();
long long dev_log_fstat_mtime_ms(const char*);
long long dev_log_current_time_ms();
void dev_log_initial_config();
FILE* dev_log_output();

/**
 * Log the line into the output. If it's excluded, it won't be logged.
 *
 * The initialization happens here, since we want to export a single function
 * for the user.
 */
void dev_log_line(const char *format, ...) {
    if (!dev_log_initial_config_happened) {
        dev_log_initial_config_happened = 1;
        dev_log_initial_config();
    }

    va_list args;
    va_start(args, format);

    char line[8192];
    vsnprintf(line, 8192, format, args);

    va_end(args);

    if (dev_log_last_time_config_read_was_long_ago()) {
        if (dev_log_config_file_has_changed()) {
            dev_log_reread_config_file_for_exclusions();
        }
    }

    if (!dev_is_line_excluded(line)) {
        dev_log_line_into_output(line);
    }
}

/**
 * Perform the initial config
 */
void dev_log_initial_config() {
    const char* config_file = getenv("DEV_LOG_CONFIG_FILE");
    const char* output_file = getenv("DEV_LOG_OUTPUT_FILE");
    const char* home_folder = getenv("HOME");

    if (config_file) {
        strcpy(dev_log_config_file_name, config_file);
    } else {
#ifdef _WIN32
        snprintf(dev_log_config_file_name, sizeof(dev_log_config_file_name), "%s\\%s", home_folder, "devlog.txt");
#else
        snprintf(dev_log_config_file_name, sizeof(dev_log_config_file_name), "%s/%s", home_folder, ".devlog");
#endif
    }

    if (output_file) {
        strcpy(dev_log_output_file_name, output_file);
    } else {
        strcpy(dev_log_output_file_name, DEV_LOG_DEFAULT_OUTPUT_PATH);
    }

    dev_log_output_line("DEVLOG: using config file: %s", dev_log_config_file_name);
    dev_log_reread_config_file_for_exclusions();
}

/**
 * Log the line without caring about exclusions. It always reaches the output.
 */
void dev_log_output_line(const char *format, ...) {
    va_list args;
    va_start(args, format);

    char line[8192];
    vsnprintf(line, 8192, format, args);

    va_end(args);

    dev_log_line_into_output(line);
}

/**
 * Checks against all the excluded matches that were configured if the
 * line should't make it into the output.
 */
inline unsigned char dev_is_line_excluded(const char* line) {
    for (size_t i = 0; i < dev_excluded_matches_count; i++) {
        if (strstr(line, DEV_LOG_EXCLUDED_MATCHES[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * Log the line into the output
 */
void dev_log_line_into_output(const char* line) {
    // FILE* output_file = dev_log_get_output();
    char time[30] = {0};

#ifdef _WIN32
#else
    int ms;
    struct timeval tv;
    struct tm *tm;
    char buffer[26];

    gettimeofday(&tv, NULL);

    ms = (int) (tv.tv_usec/1000.0);
    if (ms>=1000) {
        ms -= 1000;
        tv.tv_sec++;
    }

    tm = localtime(&tv.tv_sec);

    strftime(buffer, 26, "%Y%m%d/%H%M%S", tm);
    snprintf(time, sizeof time, "%s.%03d", buffer, ms);
#endif

    fprintf(dev_log_output(), "%s - %s\n", time, line);
}

/**
 * Check if the config was read a while back.
 */
unsigned char dev_log_last_time_config_read_was_long_ago() {
    if (dev_last_time_config_was_read < 0) {
        return 1;
    }

    if ((dev_log_current_time_ms() - dev_last_time_config_was_read) > DEV_LOG_READ_CONFIG_POLL_MS) {
        return 1;
    }

    return 0;
}

/**
 * Check if the config file has changed.
 */
unsigned char dev_log_config_file_has_changed() {
    long long current_config_file_mtime = dev_log_fstat_mtime_ms(dev_log_config_file_name);
    return current_config_file_mtime != dev_readed_config_file_mtime;
}

char* rtrim(char* s){
    for (long i = strlen(s) - 1; i >= 0; i--) {
        if (!isspace(s[i])) {
            break;
        }

        // blank all the end spaces
        s[i] = 0;
    }

    return s;
}

/**
 * Read the configuration file for lines to exclude.
 */
void dev_log_reread_config_file_for_exclusions() {
    dev_last_time_config_was_read = dev_log_current_time_ms();

    dev_log_output_line("DEVLOG: reading config file: %s", dev_log_config_file_name);

    FILE* config_file = fopen(dev_log_config_file_name, "r");
    char* line = 0;
    size_t size = 0;

    if (!config_file) {
        dev_log_output_line("DEVLOG: failure reading config file: %s (%d - %s)",
                            dev_log_config_file_name, errno, strerror(errno));
        return;
    }

    // free the current matches
    if (DEV_LOG_EXCLUDED_MATCHES) {
        for (int i = 0; i < dev_excluded_matches_count; i++) {
            free(DEV_LOG_EXCLUDED_MATCHES[i]);
            DEV_LOG_EXCLUDED_MATCHES[i] = 0;
        }

        free(DEV_LOG_EXCLUDED_MATCHES);
    }

    // read the lines into our array
    while (getline(&line, &size, config_file) >= 0) {
        DEV_LOG_EXCLUDED_MATCHES = (char **)realloc(DEV_LOG_EXCLUDED_MATCHES, ++dev_excluded_matches_count * sizeof(void *));
        DEV_LOG_EXCLUDED_MATCHES[dev_excluded_matches_count - 1] = rtrim(line);

        line = 0;
        size = 0;
    }

    fclose(config_file);

    dev_log_output_line("DEVLOG: done reading config file: %s", dev_log_config_file_name);
}

/**
 * Return the current time in millis.
 * @return
 */
long long dev_log_current_time_ms() {
    struct timeval  tv;
    gettimeofday(&tv, NULL);

    long long time_in_millis = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;

    return time_in_millis;
}

/**
 * Read the mtime from the file.
 * @param file_name
 * @return
 */
long long dev_log_fstat_mtime_ms(const char* file_name) {
    struct stat statbuf = {};

    if (stat(file_name, &statbuf) < 0) {
        dev_log_output_line("DEVLOG: unable to stat %s, errno: %d, %s",
                file_name, errno, strerror(errno));
        return -1;
    }

    return statbuf.st_mtime * 1000LL;
}

FILE* dev_log_output() {
    if (dev_log_output_file) {
        return dev_log_output_file;
    }

    dev_log_output_file = fopen(dev_log_output_file_name, "a");
    return dev_log_output_file;
}
