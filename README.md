# minimon

`minimon` is a small self-contained monitor module for embedded targets. It is designed for low-friction integration into firmware projects and also builds on Linux, macOS, and Windows for tests, tooling, and host-side simulation.

The module gives you:

- A tiny line-based monitor shell
- Read/write tracing of live variables
- Read-only traced values
- Buffered application log output through the same transport
- Optional float support

It is implemented as one public header and one C source file:

- [include/monitor.h](include/monitor.h)
- [src/monitor.c](src/monitor.c)

## Properties

- No dynamic allocation
- Fixed-size internal buffers
- Stateful, not re-entrant
- Friendly to Cortex-M style polling loops
- Portable C11

## Shell Commands

The shell is line-oriented and built around `mon_task()`.

- `help`
- `list`
- `get <name>`
- `set <name> <value>`
- `trace [on|off]`

Examples:

```text
get counter
set led_level 3
set threshold 0x20
trace off
list
```

## Build

### CMake

```sh
cmake -S . -B build -DMONITOR_BUILD_EXAMPLE=ON -DMONITOR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:

- `MONITOR_BUILD_EXAMPLE=ON|OFF`
- `MONITOR_BUILD_TESTS=ON|OFF`
- `MONITOR_ENABLE_FLOAT_SUPPORT=ON|OFF`
- `MONITOR_ENABLE_SIZE_OPTIMIZATIONS=ON|OFF`

### Embedded Integration

Add the two module files to your firmware build, or include this repository as a subdirectory and link `minimon`.

If you need tighter RAM limits, override the configuration macros when compiling the library:

```c
MON_MAX_TRACES
MON_MAX_NAME_LENGTH
MON_MAX_INPUT_LENGTH
MON_MAX_MESSAGE_LENGTH
MON_MAX_QUEUED_MESSAGES
MON_MAX_FORMAT_LENGTH
```

Current default values:

- `MON_MAX_TRACES = 32`
- `MON_MAX_NAME_LENGTH = 32`
- `MON_MAX_INPUT_LENGTH = 64`
- `MON_MAX_MESSAGE_LENGTH = 96`
- `MON_MAX_QUEUED_MESSAGES = 8`
- `MON_MAX_FORMAT_LENGTH = MON_MAX_MESSAGE_LENGTH`

## Basic Usage

Initialize once, register traces, then call `mon_task()` from your main loop or transport service routine.

```c
#include "monitor.h"

#include <stdint.h>

static uint8_t g_counter;
static int16_t g_temperature_c;

void app_init(void)
{
    mon_reset("demo monitor\n");

    MON_TRACE_NAMED_U8("counter", &g_counter);
    MON_TRACE_NAMED_I16("temperature_c", &g_temperature_c);
    MON_TRACE_NAMED_VALUE_U16("build_id", 0x1234u);
}
```

### Driving The Monitor

`mon_task(input, device_ready)` does three jobs:

- Consumes newly received input bytes
- Detects trace changes
- Returns one buffered output string when the transport is ready

Typical UART-style polling loop:

```c
static void monitor_service(const char *rx_line, int tx_ready)
{
    const char *tx = mon_task(rx_line, tx_ready);

    while (tx != NULL) {
        uart_write_string(tx);
        tx = mon_task(NULL, tx_ready);
    }
}
```

If your receive path is byte-oriented, accumulate bytes or pass partial strings over time. A command runs when `\n` or `\r` arrives.

## Writable Trace Example

Registered variables can be read with `get` and updated with `set`.

```c
static uint32_t ticks;

void register_monitor_items(void)
{
    MON_TRACE_NAMED_U32("ticks", &ticks);
}
```

Shell session:

```text
get ticks
ticks (u32) = 100

set ticks 250
ticks (u32) = 250
```

## Read-Only Value Example

Read-only values are copied into the monitor state. Re-register the same name whenever the value changes.

```c
static uint16_t health_code(void)
{
    return 0x42u;
}

void publish_health(void)
{
    MON_TRACE_NAMED_VALUE_U16("health", health_code());
}
```

`set health ...` will be rejected as read-only.

## Application Logging

Use `mon_print()` to send formatted application messages through the same output queue.

```c
void app_step(uint32_t step)
{
    (void)mon_print("[app] step=%lu\n", (unsigned long)step);
}
```

Output is buffered and returned by later calls to `mon_task()`.

## Important Lifetime Rules

This is the main integration detail to get right:

- Pointers registered with `MON_TRACE_*()` must remain valid until `mon_reset()`
- The `welcome_message` passed to `mon_reset()` must remain valid until the next `mon_reset()`
- Trace names are retained by pointer, not copied, so `human_identifier` strings must also remain valid until `mon_reset()`

String literals are ideal:

```c
MON_TRACE_NAMED_U8("counter", &g_counter);
mon_reset("board monitor\n");
```

Do not pass short-lived stack buffers as names or welcome strings.

## Float Support

Float support is optional and disabled by default.

Enable it with:

```sh
cmake -S . -B build -DMONITOR_ENABLE_FLOAT_SUPPORT=ON
```

Then register `float` or `double` values with:

- `MON_TRACE_F32`
- `MON_TRACE_F64`
- `MON_TRACE_VALUE_F32`
- `MON_TRACE_VALUE_F64`

## Example

A small interactive demo is available in [examples/monitor_example.c](examples/monitor_example.c).

Build and run it:

```sh
cmake -S . -B build -DMONITOR_BUILD_EXAMPLE=ON
cmake --build build
./build/monitor_example
```

## Notes

- When the output queue is full, new messages are dropped and a drop notice is emitted later when space becomes available.
- Input is bounded by `MON_MAX_INPUT_LENGTH`.
- Automatic trace output defaults to `on` in debug builds and `off` in release builds.
