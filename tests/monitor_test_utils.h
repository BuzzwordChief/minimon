#ifndef MONITOR_TEST_UTILS_H
#define MONITOR_TEST_UTILS_H

#include "monitor.h"

#include <assert.h>
#include <string.h>

#define MONITOR_DEFAULT_WELCOME "minimon ready. Type 'help' for commands.\n"
#define MONITOR_HELP_TEXT                                               \
    "Commands:\n"                                                       \
    "  help\n"                                                          \
    "  list\n"                                                          \
    "  get <name>\n"                                                    \
    "  set <name> <value>\n"

static inline const char *expect_output(const char *text, int device_ready)
{
    const char *output = mon_task(text, device_ready);

    assert(output != NULL);
    return output;
}

static inline void expect_output_eq(const char *text,
                                    int device_ready,
                                    const char *expected)
{
    const char *output = expect_output(text, device_ready);

    assert(strcmp(output, expected) == 0);
}

static inline void expect_no_output(const char *text, int device_ready)
{
    const char *output = mon_task(text, device_ready);

    assert(output == NULL);
}

#endif
