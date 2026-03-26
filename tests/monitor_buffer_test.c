#include "monitor_test_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t counter = 3u;
    char long_input[MON_MAX_INPUT_LENGTH + 8u];
    char expected[32];
    size_t index;

    mon_reset(NULL);
    expect_no_output(NULL, 0);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    MON_TRACE_U8(&counter);

    /* Command output must remain buffered while the device is not ready. */
    expect_no_output("get counter\n", 0);
    expect_output_eq(NULL, 1, "counter (u8) = 3\n");

    (void)memset(long_input, 'a', sizeof(long_input));
    long_input[sizeof(long_input) - 2u] = '\n';
    long_input[sizeof(long_input) - 1u] = '\0';
    expect_output_eq(long_input, 1, "[monitor] input line too long\n");

    for (index = 0u; index < (MON_MAX_QUEUED_MESSAGES + 4u); index++) {
        (void)mon_print("msg%02zu\n", index);
    }

    for (index = 0u; index < MON_MAX_QUEUED_MESSAGES; index++) {
        (void)snprintf(expected, sizeof(expected), "msg%02zu\n", index);
        expect_output_eq(NULL, 1, expected);
    }

    expect_output_eq(NULL, 1, "[monitor] dropped 4 message(s)\n");
    expect_no_output(NULL, 1);

    return 0;
}
