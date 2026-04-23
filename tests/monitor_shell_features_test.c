#include "monitor_test_utils.h"

#include <stdint.h>

int main(void)
{
    uint8_t counter = 2u;

    /* Echo of printable characters without submit. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("abc", 1, "abc");

    /* Backspace echoes the visual-erase sequence and edits the buffer. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("a\bb", 1, "a\b \bb");

    /* Submit: echoed line is a self-contained message, then the response. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("get counter\n", 1, "get counter\r\n");
    expect_output_eq(NULL, 1, "counter (u8) = 2\n");

    /* Tab completion on a unique trace-name prefix. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("get coun", 1, "get coun");
    expect_output_eq("\t", 1, "ter");
    expect_output_eq("\n", 1, "\r\n");
    expect_output_eq(NULL, 1, "counter (u8) = 2\n");

    /* Tab completion with no matches is a silent no-op. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("get zzz", 1, "get zzz");
    expect_no_output("\t", 1);
    expect_output_eq("\n", 1, "\r\n");
    expect_output_eq(NULL, 1, "Unknown variable: zzz\n");

    /* History: UP arrow recalls the previous command, DOWN clears the line. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("list\n", 1, "list\r\n");
    expect_output_eq(NULL, 1, "counter (u8) = 2\n");
    expect_output_eq("\x1B[A", 1, "list");
    expect_output_eq("\x1B[B", 1, "\b \b\b \b\b \b\b \b");

    /* Right/left arrows are silently dropped; no output is produced. */
    expect_no_output("\x1B[C", 1);
    expect_no_output("\x1B[D", 1);

    /* Submitting the recalled history line still runs the command. */
    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_NAMED_U8("counter", &counter);
    expect_output_eq("list\n", 1, "list\r\n");
    expect_output_eq(NULL, 1, "counter (u8) = 2\n");
    expect_output_eq("\x1B[A", 1, "list");
    expect_output_eq("\n", 1, "\r\n");
    expect_output_eq(NULL, 1, "counter (u8) = 2\n");

    return 0;
}
