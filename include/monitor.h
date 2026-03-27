#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MON_MAX_TRACES
#define MON_MAX_TRACES 32u
#endif

#ifndef MON_MAX_NAME_LENGTH
#define MON_MAX_NAME_LENGTH 32u
#endif

#ifndef MON_MAX_INPUT_LENGTH
#define MON_MAX_INPUT_LENGTH 96u
#endif

#ifndef MON_MAX_MESSAGE_LENGTH
#define MON_MAX_MESSAGE_LENGTH 128u
#endif

#ifndef MON_MAX_QUEUED_MESSAGES
#define MON_MAX_QUEUED_MESSAGES 16u
#endif

#ifndef MON_MAX_FORMAT_LENGTH
#define MON_MAX_FORMAT_LENGTH 256u
#endif

#ifndef MON_MAX_WELCOME_LENGTH
#define MON_MAX_WELCOME_LENGTH 128u
#endif

#ifndef MONITOR_ENABLE_FLOAT_SUPPORT
#define MONITOR_ENABLE_FLOAT_SUPPORT 0
#endif

/*
 * Reset all internal monitor state and configure a welcome message.
 *
 * welcome_message may be NULL to request the built-in generic welcome message.
 * The message is copied into internal storage, may be truncated to
 * MON_MAX_WELCOME_LENGTH - 1 bytes, and is queued immediately by mon_reset().
 *
 * Registered pointers must remain valid until they are cleared with mon_reset().
 * The module is stateful and not re-entrant.
 */
void mon_reset(const char *welcome_message);

/*
 * Process newly received shell input, trace registered values, and return one
 * buffered output chunk when the transport is ready.
 *
 * Shell commands:
 *   help
 *       Print the welcome message followed by the supported shell commands.
 *   list
 *       Print every currently registered trace as "<name> (<type>) = <value>".
 *   get <name>
 *       Print the current value of a registered trace.
 *   set <name> <value>
 *       Parse <value> and write it into a registered trace.
 *       Integer values accept the prefixes handled by strtoull()/strtoll(),
 *       for example decimal and 0x-prefixed hexadecimal input.
 *       When MONITOR_ENABLE_FLOAT_SUPPORT is non-zero, floating-point values
 *       accept the formats handled by strtod().
 *       Read-only value traces reject write attempts.
 *   trace [on|off]
 *       Without an argument, print whether automatic trace-change output is
 *       currently enabled. With "on" or "off", enable or disable automatic
 *       output of registered trace changes. The default after mon_reset() is
 *       enabled when NDEBUG is not defined and disabled otherwise.
 *
 * Input is line-oriented. A command is executed once a '\n' or '\r' terminator
 * is received. Input may be provided incrementally across multiple calls.
 *
 * input may be NULL when no new bytes were received. The returned pointer is
 * valid until the next call into the monitor module. NULL means no output is
 * available for transmission on this call.
 */
const char *mon_task(const char *input, int device_ready);

/*
 * Queue formatted application output for later transmission by mon_task().
 *
 * The formatted text is copied into the monitor's internal message queue and
 * may be truncated to MON_MAX_FORMAT_LENGTH - 1 bytes.
 */
const char *mon_print(const char *fmt, ...);

/*
 * Register variables for tracing and shell access.
 *
 * The supplied pointer must remain valid until mon_reset() clears the monitor
 * state. Re-registering the same pointer updates its metadata without clearing
 * the previously observed value.
 */
#define MON_TRACE_NAMED_U8(name, ptr) mon_trace_u8((ptr), (name))
#define MON_TRACE_U8(ptr) mon_trace_u8((ptr), #ptr)
void mon_trace_u8(uint8_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_I8(name, ptr) mon_trace_i8((ptr), (name))
#define MON_TRACE_I8(ptr) mon_trace_i8((ptr), #ptr)
void mon_trace_i8(int8_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_U16(name, ptr) mon_trace_u16((ptr), (name))
#define MON_TRACE_U16(ptr) mon_trace_u16((ptr), #ptr)
void mon_trace_u16(uint16_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_I16(name, ptr) mon_trace_i16((ptr), (name))
#define MON_TRACE_I16(ptr) mon_trace_i16((ptr), #ptr)
void mon_trace_i16(int16_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_U32(name, ptr) mon_trace_u32((ptr), (name))
#define MON_TRACE_U32(ptr) mon_trace_u32((ptr), #ptr)
void mon_trace_u32(uint32_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_I32(name, ptr) mon_trace_i32((ptr), (name))
#define MON_TRACE_I32(ptr) mon_trace_i32((ptr), #ptr)
void mon_trace_i32(int32_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_U64(name, ptr) mon_trace_u64((ptr), (name))
#define MON_TRACE_U64(ptr) mon_trace_u64((ptr), #ptr)
void mon_trace_u64(uint64_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_I64(name, ptr) mon_trace_i64((ptr), (name))
#define MON_TRACE_I64(ptr) mon_trace_i64((ptr), #ptr)
void mon_trace_i64(int64_t *value, const char *human_identifier);

#define MON_TRACE_NAMED_F32(name, ptr) mon_trace_f32((ptr), (name))
#define MON_TRACE_F32(ptr) mon_trace_f32((ptr), #ptr)
void mon_trace_f32(float *value, const char *human_identifier);

#define MON_TRACE_NAMED_F64(name, ptr) mon_trace_f64((ptr), (name))
#define MON_TRACE_F64(ptr) mon_trace_f64((ptr), #ptr)
void mon_trace_f64(double *value, const char *human_identifier);

/*
 * Register read-only values for tracing and shell reads.
 *
 * The passed value is copied into internal storage. Re-register the same name
 * whenever the source value changes. Read-only traces can be listed and read
 * from the shell, but set <name> <value> is rejected for them. When
 * MONITOR_ENABLE_FLOAT_SUPPORT is zero, the float trace APIs queue
 * "[monitor] float support disabled" and do not register a trace.
 */
#define MON_TRACE_NAMED_VALUE_U8(name, value) mon_trace_u8_value((value), (name))
#define MON_TRACE_VALUE_U8(value) mon_trace_u8_value((value), #value)
void mon_trace_u8_value(uint8_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_I8(name, value) mon_trace_i8_value((value), (name))
#define MON_TRACE_VALUE_I8(value) mon_trace_i8_value((value), #value)
void mon_trace_i8_value(int8_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_U16(name, value) mon_trace_u16_value((value), (name))
#define MON_TRACE_VALUE_U16(value) mon_trace_u16_value((value), #value)
void mon_trace_u16_value(uint16_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_I16(name, value) mon_trace_i16_value((value), (name))
#define MON_TRACE_VALUE_I16(value) mon_trace_i16_value((value), #value)
void mon_trace_i16_value(int16_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_U32(name, value) mon_trace_u32_value((value), (name))
#define MON_TRACE_VALUE_U32(value) mon_trace_u32_value((value), #value)
void mon_trace_u32_value(uint32_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_I32(name, value) mon_trace_i32_value((value), (name))
#define MON_TRACE_VALUE_I32(value) mon_trace_i32_value((value), #value)
void mon_trace_i32_value(int32_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_U64(name, value) mon_trace_u64_value((value), (name))
#define MON_TRACE_VALUE_U64(value) mon_trace_u64_value((value), #value)
void mon_trace_u64_value(uint64_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_I64(name, value) mon_trace_i64_value((value), (name))
#define MON_TRACE_VALUE_I64(value) mon_trace_i64_value((value), #value)
void mon_trace_i64_value(int64_t value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_F32(name, value) mon_trace_f32_value((value), (name))
#define MON_TRACE_VALUE_F32(value) mon_trace_f32_value((value), #value)
void mon_trace_f32_value(float value, const char *human_identifier);

#define MON_TRACE_NAMED_VALUE_F64(name, value) mon_trace_f64_value((value), (name))
#define MON_TRACE_VALUE_F64(value) mon_trace_f64_value((value), #value)
void mon_trace_f64_value(double value, const char *human_identifier);

#ifdef __cplusplus
}
#endif

#endif
