#include "devlog.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <cstdarg>
#include <cstring>
#include <errno.h>
#include <string.h>
#include <chrono>
#include <stdlib.h>

#ifdef _WIN32
#define DEV_LOG_DEFAULT_OUTPUT_PATH "c:/temp/devlog.txt"
#else
#define DEV_LOG_DEFAULT_OUTPUT_PATH "/tmp/devlog.txt"
#endif

#define DEV_LOG_READ_CONFIG_POLL_SECONDS 5

const char** DEV_LOG_EXCLUDED_MATCHES;
int dev_excluded_matches_count;
char dev_log_config_file[8192] = {};
bool dev_log_initial_config_happened = false;

long long dev_last_time_config_was_read = -1;
long long dev_readed_config_file_mtime = -1;

// forward definitions
void dev_log_output_line(const char *, ...);
inline bool dev_is_line_excluded(const char*);
void dev_log_line_into_output(const char*);
bool dev_log_last_time_config_read_was_long_ago();
bool dev_log_config_file_has_changed();
void dev_log_reread_config_file_for_exclusions();
long long dev_log_ftime_ms(const char*);
long long current_time_ms();
void dev_log_initial_config();

/**
 * Log the line into the output. If it's excluded, it won't be logged.
 */
void dev_log_line(const char *format, ...) {
    if (!dev_log_initial_config_happened) {
        dev_log_initial_config_happened = true;
        dev_log_initial_config();
    }

    va_list args;
    va_start(args, format);

    char line[8192];
    vsnprintf(line, 8192, format, args);

    va_end(args);

    if (dev_log_last_time_config_read_was_long_ago()) {
        dev_last_time_config_was_read = current_time_ms();
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
    const char* home_folder = getenv("HOME");

    if (config_file) {
        strcpy(dev_log_config_file, config_file);
    } else {
#ifdef _WIN32
        snprintf(dev_log_config_file, sizeof(dev_log_config_file), "%s\\%s", home_folder, "devlog.txt");
#else
        snprintf(dev_log_config_file, sizeof(dev_log_config_file), "%s/%s", home_folder, ".devlog");
#endif
    }

    dev_log_output_line("DEVLOG: using config file: %s", dev_log_config_file);
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
inline bool dev_is_line_excluded(const char* line) {
    for (size_t i = 0; i < dev_excluded_matches_count; i++) {
        if (strstr(line, DEV_LOG_EXCLUDED_MATCHES[i]) >= 0) {
            return true;
        }
    }

    return false;
}

/**
 * Log the line into the output
 */
void dev_log_line_into_output(const char* line) {
    printf("%s\n", line);
}

bool dev_log_last_time_config_read_was_long_ago() {
    if (dev_last_time_config_was_read < 0) {
        return true;
    }

    if ((current_time_ms() - dev_last_time_config_was_read) > DEV_LOG_READ_CONFIG_POLL_SECONDS) {
        return true;
    }

    return false;
}

bool dev_log_config_file_has_changed() {
    long long current_config_file_mtime = dev_log_ftime_ms(dev_log_config_file);
    return current_config_file_mtime != dev_readed_config_file_mtime;
}

void dev_log_reread_config_file_for_exclusions() {
    dev_last_time_config_was_read = current_time_ms();
}

long long current_time_ms() {
    using namespace std::chrono;

    auto millisec_since_epoch = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return millisec_since_epoch;
}

long long dev_log_ftime_ms(const char* file_name) {
    struct stat statbuf;

    if (stat(file_name, &statbuf) < 0) {
        dev_log_output_line("DEVLOG: unable to stat %s, errno: %d, %s",
                file_name, errno, strerror(errno));
        return -1;
    }

    return statbuf.st_mtime * 1000LL;
}

int main(int argc, const char* argv[]) {
    dev_log_line("time is: %lld", dev_log_ftime_ms("/etc/passwd"));
    dev_log_line("what %s is %s", "the heck", "this");
    dev_log_line("what %s is %s", "the heck", "that");
    dev_log_line("what %s is %s", "the heck", "it");
    dev_log_line("what %s is %s", "the heck", "she");
    dev_log_line("what %s is %s", "the heck", "he");

    return 0;
}

