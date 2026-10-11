#pragma once

// Nova Engine — assertions.
// NOVA_ASSERT  : checks an invariant; active in debug builds (NDEBUG unset).
// NOVA_ENSURE  : always-on check; used for preconditions that must hold in
//                release builds as well (e.g. JNI handshakes).
// Both log the failure through the ring-buffer logger before aborting, so
// crash dumps retain the failed condition.
//
// The trailing message arguments are optional (C++20 __VA_OPT__):
//   NOVA_ENSURE(ptr != nullptr);
//   NOVA_ENSURE(ptr != nullptr, "device '%s' must exist", name);

#include "core/Log.h"

#include <cstdlib>

#if !defined(NDEBUG)
#define NOVA_ASSERT(cond, ...)                                                       \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ::nova::core::log::write(::nova::core::LogLevel::Error,                  \
                                     "ASSERT failed: %s (%s:%d) ", #cond, __FILE__,   \
                                     __LINE__);                                       \
            __VA_OPT__(::nova::core::log::write(::nova::core::LogLevel::Error,        \
                                                __VA_ARGS__);)                        \
            ::nova::core::log::abortProcess();                                       \
        }                                                                            \
    } while (false)
#else
#define NOVA_ASSERT(cond, ...) \
    do {                       \
        (void)sizeof(cond);    \
    } while (false)
#endif

#define NOVA_ENSURE(cond, ...)                                                       \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ::nova::core::log::write(::nova::core::LogLevel::Error,                  \
                                     "ENSURE failed: %s (%s:%d) ", #cond, __FILE__,   \
                                     __LINE__);                                       \
            __VA_OPT__(::nova::core::log::write(::nova::core::LogLevel::Error,        \
                                                __VA_ARGS__);)                        \
            ::nova::core::log::abortProcess();                                       \
        }                                                                            \
    } while (false)
