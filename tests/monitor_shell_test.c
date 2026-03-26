#include "monitor_test_utils.h"

#include <stdint.h>

static const char g_help_text[] =
    MONITOR_HELP_TEXT;

int main(void)
{
    uint8_t counter = 2u;

    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);

    MON_TRACE_U8(&counter);

    expect_no_output("he", 1);
    expect_output_eq("lp\n", 1, MONITOR_DEFAULT_WELCOME);
    expect_output_eq(NULL, 1, g_help_text);

    expect_no_output("get coun", 1);
    expect_output_eq("ter\n", 1, "counter (u8) = 2\n");

    expect_output_eq("get\tcounteq\br\n", 1, "counter (u8) = 2\n");
    expect_output_eq("help extra\n", 1, "Usage: help\n");
    expect_output_eq("list extra\n", 1, "Usage: list\n");
    expect_output_eq("get\n", 1, "Usage: get <name>\n");
    expect_output_eq("set counter\n", 1, "Usage: set <name> <value>\n");
    expect_output_eq("set missing 1\n", 1, "Unknown variable: missing\n");
    expect_output_eq("set counter nope\n", 1, "Invalid value for counter: nope\n");
    expect_output_eq("?\n", 1, MONITOR_DEFAULT_WELCOME);
    expect_output_eq(NULL, 1, g_help_text);
    expect_output_eq("unknown\n", 1, "Unknown command: unknown\n");
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    expect_output_eq(NULL, 1, g_help_text);

    return 0;
}
