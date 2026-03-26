#include "monitor_test_utils.h"

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t first = 1u;
    uint8_t second = 2u;
    uint8_t values[MON_MAX_TRACES + 1u];
    char names[MON_MAX_TRACES + 1u][MON_MAX_NAME_LENGTH];
    size_t index;

    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);

    MON_TRACE_NAMED_U8("dup_ptr", &first);
    MON_TRACE_NAMED_U8("dup_ptr", &second);
    expect_output_eq(NULL, 1, "[monitor] duplicate trace name: dup_ptr\n");

    MON_TRACE_NAMED_VALUE_U8("dup_value", 7u);
    MON_TRACE_NAMED_U8("dup_value", &first);
    expect_output_eq(NULL, 1, "[monitor] duplicate trace name: dup_value\n");

    mon_trace_u8(NULL, "null_ptr");
    expect_output_eq(NULL, 1, "[monitor] null trace pointer ignored\n");

    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        values[index] = (uint8_t)index;
        (void)snprintf(names[index], sizeof(names[index]), "slot%02zu", index);
        mon_trace_u8(&values[index], names[index]);
    }

    values[MON_MAX_TRACES] = 0xFFu;
    (void)snprintf(names[MON_MAX_TRACES],
                   sizeof(names[MON_MAX_TRACES]),
                   "slot%02zu",
                   (size_t)MON_MAX_TRACES);
    mon_trace_u8(&values[MON_MAX_TRACES], names[MON_MAX_TRACES]);
    expect_output_eq(NULL, 1, "[monitor] trace registry full\n");

    return 0;
}
