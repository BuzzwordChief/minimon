#include "monitor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const char *expect_output(const char *text, int device_ready)
{
    const char *output = mon_task(text, device_ready);

    assert(output != NULL);
    return output;
}

int main(void)
{
    uint8_t counter = 1u;
    int16_t temperature = -3;
    uint16_t status_word = 0x1234u;
    const char *output;

    mon_reset(NULL);
    output = expect_output(NULL, 1);
    assert(strcmp(output, "minimon ready. Type 'help' for commands.\n") == 0);

    MON_TRACE_U8(&counter);
    MON_TRACE_I16(&temperature);
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);

    output = expect_output("get counter\n", 1);
    assert(strcmp(output, "counter (u8) = 1\n") == 0);

    output = expect_output("get status_word\n", 1);
    assert(strcmp(output, "status_word (u16) = 4660\n") == 0);

    counter = 4u;
    output = expect_output(NULL, 1);
    assert(strcmp(output, "trace counter (u8) = 4\n") == 0);

    output = expect_output("set counter 7\n", 1);
    assert(strcmp(output, "counter (u8) = 7\n") == 0);
    assert(counter == 7u);

    counter = 9u;
    MON_TRACE_U8(&counter);
    output = expect_output(NULL, 1);
    assert(strcmp(output, "trace counter (u8) = 9\n") == 0);

    output = expect_output("set temperature -12\n", 1);
    assert(strcmp(output, "temperature (i16) = -12\n") == 0);
    assert(temperature == -12);

    output = expect_output("set status_word 9\n", 1);
    assert(strcmp(output, "Variable is read-only: status_word\n") == 0);

    status_word = 0x1235u;
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);
    output = expect_output(NULL, 1);
    assert(strcmp(output, "trace status_word (u16) = 4661\n") == 0);

    mon_print("application log %u\n", 9u);
    output = expect_output(NULL, 1);
    assert(strcmp(output, "application log 9\n") == 0);

    output = expect_output("list\n", 1);
    assert(strcmp(output, "counter (u8) = 9\n") == 0);

    output = expect_output(NULL, 1);
    assert(strcmp(output, "temperature (i16) = -12\n") == 0);

    output = expect_output(NULL, 1);
    assert(strcmp(output, "status_word (u16) = 4661\n") == 0);

    mon_reset("custom welcome\n");
    output = expect_output(NULL, 1);
    assert(strcmp(output, "custom welcome\n") == 0);

    MON_TRACE_U8(&counter);
    MON_TRACE_I16(&temperature);
    MON_TRACE_NAMED_VALUE_U16("status_word", status_word);

    output = expect_output("reset\n", 1);
    assert(strcmp(output, "Unknown command: reset\n") == 0);

    output = expect_output("list\n", 1);
    assert(strcmp(output, "counter (u8) = 9\n") == 0);

    output = expect_output(NULL, 1);
    assert(strcmp(output, "temperature (i16) = -12\n") == 0);

    output = expect_output(NULL, 1);
    assert(strcmp(output, "status_word (u16) = 4661\n") == 0);

    return 0;
}
