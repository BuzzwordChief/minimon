#include "monitor_test_utils.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t counter = 1u;
    int16_t temperature = -3;
    uint16_t status_word = 0x1234u;
    const char *output;

    mon_reset(NULL);
    expect_output_eq(NULL, 1, MONITOR_DEFAULT_WELCOME);
    expect_output_eq("trace\n", 1, MONITOR_DEFAULT_TRACE_OUTPUT_TEXT);

    MON_TRACE_U8(&counter);
    MON_TRACE_I16(&temperature);
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);

    output = expect_output("get counter\n", 1);
    EXPECT_TRUE(strcmp(output, "counter (u8) = 1\n") == 0);

    output = expect_output("get status_word\n", 1);
    EXPECT_TRUE(strcmp(output, "status_word (u16) = 4660\n") == 0);

    counter = 4u;
    if (MONITOR_DEFAULT_TRACE_OUTPUT_ENABLED != 0) {
        output = expect_output(NULL, 1);
        EXPECT_TRUE(strcmp(output, "trace counter (u8) = 4\n") == 0);
    } else {
        expect_no_output(NULL, 1);
    }

    expect_output_eq("trace off\n", 1, MONITOR_TRACE_OUTPUT_OFF_TEXT);
    counter = 5u;
    expect_no_output(NULL, 1);

    expect_output_eq("trace on\n", 1, MONITOR_TRACE_OUTPUT_ON_TEXT);
    expect_no_output(NULL, 1);

    counter = 6u;
    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "trace counter (u8) = 6\n") == 0);

    output = expect_output("set counter 7\n", 1);
    EXPECT_TRUE(strcmp(output, "counter (u8) = 7\n") == 0);
    EXPECT_TRUE(counter == 7u);

    counter = 9u;
    MON_TRACE_U8(&counter);
    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "trace counter (u8) = 9\n") == 0);

    output = expect_output("set temperature -12\n", 1);
    EXPECT_TRUE(strcmp(output, "temperature (i16) = -12\n") == 0);
    EXPECT_TRUE(temperature == -12);

    output = expect_output("set status_word 9\n", 1);
    EXPECT_TRUE(strcmp(output, "Variable is read-only: status_word\n") == 0);

    status_word = 0x1235u;
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);
    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "trace status_word (u16) = 4661\n") == 0);

    mon_print("application log %u\n", 9u);
    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "application log 9\n") == 0);

    output = expect_output("list\n", 1);
    EXPECT_TRUE(strcmp(output, "counter (u8) = 9\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "temperature (i16) = -12\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "status_word (u16) = 4661\n") == 0);

    mon_reset("custom welcome\n");
    expect_output_eq(NULL, 1, "custom welcome\n");
    expect_output_eq("trace\n", 1, MONITOR_DEFAULT_TRACE_OUTPUT_TEXT);

    MON_TRACE_U8(&counter);
    MON_TRACE_I16(&temperature);
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);

    output = expect_output("reset\n", 1);
    EXPECT_TRUE(strcmp(output, "Unknown command: reset\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "custom welcome\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, MONITOR_HELP_TEXT) == 0);

    output = expect_output("list\n", 1);
    EXPECT_TRUE(strcmp(output, "counter (u8) = 9\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "temperature (i16) = -12\n") == 0);

    output = expect_output(NULL, 1);
    EXPECT_TRUE(strcmp(output, "status_word (u16) = 4661\n") == 0);

    return 0;
}
