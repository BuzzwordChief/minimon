#include "monitor.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct demo_state {
    uint8_t led_level;
    uint32_t ticks;
    int16_t temperature_c;
#if MONITOR_ENABLE_FLOAT_SUPPORT
    float gain;
#endif
} demo_state_t;

static uint16_t demo_health_code(const demo_state_t *state)
{
    return (uint16_t)(((uint16_t)state->led_level << 8) |
                      (uint16_t)(state->ticks % 0x100u));
}

static void demo_register_traces(demo_state_t *state)
{
    MON_TRACE_NAMED_U8("led_level", &state->led_level);
    MON_TRACE_NAMED_U32("ticks", &state->ticks);
    MON_TRACE_NAMED_I16("temperature_c", &state->temperature_c);
#if MONITOR_ENABLE_FLOAT_SUPPORT
    MON_TRACE_NAMED_F32("gain", &state->gain);
#endif
    MON_TRACE_NAMED_VALUE_U16("health_code", demo_health_code(state));
}

static void demo_drain_monitor(const char *input)
{
    const char *output = mon_task(input, 1);

    while (output != NULL) {
        (void)fputs(output, stdout);
        output = mon_task(NULL, 1);
    }
}

static void demo_step(demo_state_t *state)
{
    state->ticks += 10u;
    state->led_level = (uint8_t)((state->led_level + 1u) % 8u);
    state->temperature_c = (int16_t)(20 + (int16_t)(state->ticks % 7u));
#if MONITOR_ENABLE_FLOAT_SUPPORT
    state->gain = 0.25f + ((float)(state->led_level) * 0.125f);
#endif

    (void)mon_print("[app] simulated step, ticks=%" PRIu32 "\n", state->ticks);
}

int main(void)
{
    demo_state_t state = {
        .led_level = 0u,
        .ticks = 0u,
        .temperature_c = 20
#if MONITOR_ENABLE_FLOAT_SUPPORT
        ,
        .gain = 0.25f
#endif
    };
    char input[MON_MAX_INPUT_LENGTH];

    mon_reset("Welcome to the minimon demo.\n");

    (void)puts("minimon example");
    (void)puts("Monitor shell: help, list, get <name>, set <name> <value>, trace [on|off]");
    (void)puts("Read-only trace example: health_code");
    (void)puts("Example commands: step, print, quit");
    demo_register_traces(&state);
    demo_drain_monitor("list\n");

    for (;;) {
        (void)fputs("> ", stdout);
        (void)fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            (void)fputc('\n', stdout);
            break;
        }

        if ((strcmp(input, "quit\n") == 0) || (strcmp(input, "exit\n") == 0)) {
            break;
        }

        demo_register_traces(&state);

        if (strcmp(input, "step\n") == 0) {
            demo_step(&state);
            demo_drain_monitor(NULL);
            continue;
        }

        if (strcmp(input, "print\n") == 0) {
#if MONITOR_ENABLE_FLOAT_SUPPORT
            (void)mon_print("[app] led=%u temperature=%d gain=%.3f\n",
                            (unsigned int)state.led_level,
                            (int)state.temperature_c,
                            (double)state.gain);
#else
            (void)mon_print("[app] led=%u temperature=%d\n",
                            (unsigned int)state.led_level,
                            (int)state.temperature_c);
#endif
            demo_drain_monitor(NULL);
            continue;
        }

        demo_drain_monitor(input);
    }

    return 0;
}
