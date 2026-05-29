#include "allocator.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *log_fp = NULL;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static log_level_t log_min = LOG_DEBUG;

static const char *level_str[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

int log_init(const char *path)
{
    pthread_mutex_lock(&log_lock);

    if (log_fp && log_fp != stderr)
        fclose(log_fp);

    if (path == NULL || strcmp(path, "stderr") == 0) {
        log_fp = stderr;
    } else {
        log_fp = fopen(path, "a");
        if (!log_fp) {
            perror("log_init: fopen");
            log_fp = stderr;
            pthread_mutex_unlock(&log_lock);
            return -1;
        }
    }

    time_t now = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(log_fp,
            "\n========================================\n"
            "  Allocator Log - %s  PID=%d\n"
            "========================================\n",
            buf, (int)getpid());
    fflush(log_fp);

    pthread_mutex_unlock(&log_lock);
    return 0;
}

void log_close(void)
{
    pthread_mutex_lock(&log_lock);
    if (log_fp && log_fp != stderr) {
        fprintf(log_fp, "=== Log kapanıyor ===\n\n");
        fflush(log_fp);
        fclose(log_fp);
    }
    log_fp = NULL;
    pthread_mutex_unlock(&log_lock);
}

void log_write(log_level_t level, const char *fmt, ...)
{
    if (level < log_min) return;

    pthread_mutex_lock(&log_lock);

    if (!log_fp) log_fp = stderr;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);

    fprintf(log_fp, "[%s.%03ld] [%s] [tid=%lu] ",
            tbuf,
            ts.tv_nsec / 1000000L,
            level_str[level],
            (unsigned long)pthread_self());

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);

    fprintf(log_fp, "\n");
    fflush(log_fp);

    pthread_mutex_unlock(&log_lock);
}
