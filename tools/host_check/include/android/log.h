// Host-check shim for <android/log.h> (NDK API subset used by Nova Engine).
// Only active with -DNOVA_HOST_CHECK=ON; real builds use the NDK header.
#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT,
} android_LogPriority;

int __android_log_write(int prio, const char* tag, const char* text);

int __android_log_print(int prio, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list args);

#ifdef __cplusplus
}
#endif
