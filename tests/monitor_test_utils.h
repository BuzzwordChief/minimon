#ifndef MONITOR_TEST_UTILS_H
#define MONITOR_TEST_UTILS_H

#include "monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MONITOR_DEFAULT_WELCOME "minimon ready. Type 'help' for commands.\n"
#define MONITOR_TRACE_OUTPUT_ON_TEXT "Automatic trace output: on\n"
#define MONITOR_TRACE_OUTPUT_OFF_TEXT "Automatic trace output: off\n"
#ifdef NDEBUG
#define MONITOR_DEFAULT_TRACE_OUTPUT_ENABLED 0
#define MONITOR_DEFAULT_TRACE_OUTPUT_TEXT MONITOR_TRACE_OUTPUT_OFF_TEXT
#else
#define MONITOR_DEFAULT_TRACE_OUTPUT_ENABLED 1
#define MONITOR_DEFAULT_TRACE_OUTPUT_TEXT MONITOR_TRACE_OUTPUT_ON_TEXT
#endif
#define MONITOR_HELP_TEXT                                               \
    "Commands:\n"                                                       \
    "  help\n"                                                          \
    "  list\n"                                                          \
    "  get <name>\n"                                                    \
    "  set <name> <value>\n"                                            \
    "  trace [on|off]\n"

static inline void expect_true_impl(int condition,
                                    const char *expression,
                                    const char *file,
                                    int line)
{
    if (condition == 0) {
        (void)fprintf(stderr,
                      "Expectation failed at %s:%d: %s\n",
                      file,
                      line,
                      expression);
        abort();
    }
}

#define EXPECT_TRUE(expression) \
    expect_true_impl((expression) != 0, #expression, __FILE__, __LINE__)

static inline const char *expect_output(const char *text, int device_ready)
{
    const char *output = mon_task(text, device_ready);

    EXPECT_TRUE(output != NULL);
    return output;
}

static inline void expect_output_eq(const char *text,
                                    int device_ready,
                                    const char *expected)
{
    const char *output = expect_output(text, device_ready);

    EXPECT_TRUE(strcmp(output, expected) == 0);
}

static inline void expect_no_output(const char *text, int device_ready)
{
    const char *output = mon_task(text, device_ready);

    EXPECT_TRUE(output == NULL);
}

#endif
