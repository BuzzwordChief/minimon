#include "monitor.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if MONITOR_ENABLE_FLOAT_SUPPORT
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#endif

_Static_assert(MON_MAX_TRACES > 0u, "MON_MAX_TRACES must be greater than zero");
_Static_assert(MON_MAX_TRACES <= UINT8_MAX,
               "MON_MAX_TRACES must fit in uint8_t");
_Static_assert(MON_MAX_NAME_LENGTH > 1u,
               "MON_MAX_NAME_LENGTH must be greater than one");
_Static_assert(MON_MAX_NAME_LENGTH <= UINT8_MAX,
               "MON_MAX_NAME_LENGTH must fit in uint8_t");
_Static_assert(MON_MAX_INPUT_LENGTH > 1u,
               "MON_MAX_INPUT_LENGTH must be greater than one");
_Static_assert(MON_MAX_INPUT_LENGTH <= UINT8_MAX,
               "MON_MAX_INPUT_LENGTH must fit in uint8_t");
_Static_assert(MON_MAX_MESSAGE_LENGTH > 1u,
               "MON_MAX_MESSAGE_LENGTH must be greater than one");
_Static_assert(MON_MAX_MESSAGE_LENGTH <= UINT8_MAX,
               "MON_MAX_MESSAGE_LENGTH must fit in uint8_t");
_Static_assert(MON_MAX_QUEUED_MESSAGES > 0u,
               "MON_MAX_QUEUED_MESSAGES must be greater than zero");
_Static_assert(MON_MAX_QUEUED_MESSAGES <= UINT8_MAX,
               "MON_MAX_QUEUED_MESSAGES must fit in uint8_t");
_Static_assert(MON_MAX_FORMAT_LENGTH > 1u,
               "MON_MAX_FORMAT_LENGTH must be greater than one");
#if MONITOR_ENABLE_SHELL_FEATURES
_Static_assert(MON_HISTORY_DEPTH > 0u,
               "MON_HISTORY_DEPTH must be greater than zero");
_Static_assert(MON_HISTORY_DEPTH <= UINT8_MAX,
               "MON_HISTORY_DEPTH must fit in uint8_t");
#endif

static const char g_mon_default_welcome[] =
    "minimon ready. Type 'help' for commands.\n";
static const char g_mon_help_text[] =
    "Commands:\n"
    "  help\n"
    "  list\n"
    "  get <name>\n"
    "  set <name> <value>\n"
    "  trace [on|off]\n";
static const char g_mon_trace_output_on_text[] =
    "Automatic trace output: on\n";
static const char g_mon_trace_output_off_text[] =
    "Automatic trace output: off\n";
#if !MONITOR_ENABLE_FLOAT_SUPPORT
static const char g_mon_float_support_disabled[] =
    "[monitor] float support disabled\n";
#endif

typedef enum mon_value_type {
    MON_VALUE_U8 = 0,
    MON_VALUE_I8,
    MON_VALUE_U16,
    MON_VALUE_I16,
    MON_VALUE_U32,
    MON_VALUE_I32,
    MON_VALUE_U64,
    MON_VALUE_I64,
#if MONITOR_ENABLE_FLOAT_SUPPORT
    MON_VALUE_F32,
    MON_VALUE_F64,
#endif
    MON_VALUE_TYPE_COUNT
} mon_value_type_t;

enum {
    MON_TYPE_FLAG_SIGNED = 0x01u,
    MON_TYPE_FLAG_FLOAT = 0x02u
};

typedef struct mon_type_info {
    uint8_t size;
    uint8_t flags;
    char name[4];
} mon_type_info_t;

static const mon_type_info_t g_mon_type_info[MON_VALUE_TYPE_COUNT] = {
    { 1u, 0u,                    "u8"  },
    { 1u, MON_TYPE_FLAG_SIGNED,  "i8"  },
    { 2u, 0u,                    "u16" },
    { 2u, MON_TYPE_FLAG_SIGNED,  "i16" },
    { 4u, 0u,                    "u32" },
    { 4u, MON_TYPE_FLAG_SIGNED,  "i32" },
    { 8u, 0u,                    "u64" },
    { 8u, MON_TYPE_FLAG_SIGNED,  "i64" },
#if MONITOR_ENABLE_FLOAT_SUPPORT
    { 4u, MON_TYPE_FLAG_FLOAT,   "f32" },
    { 8u, MON_TYPE_FLAG_FLOAT,   "f64" },
#endif
};

typedef enum mon_write_result {
    MON_WRITE_RESULT_OK = 0,
    MON_WRITE_RESULT_INVALID,
    MON_WRITE_RESULT_READ_ONLY
} mon_write_result_t;

typedef struct mon_name_view {
    const char *ptr;
    uint8_t length;
} mon_name_view_t;

typedef struct mon_trace_entry {
    void *source_ptr;
    const char *name_ptr;
    uint8_t last_value[8];
    uint8_t name_length;
    uint8_t type;
    uint8_t flags;
} mon_trace_entry_t;

enum {
    MON_TRACE_FLAG_WRITABLE = 0x01u
};

enum {
    MON_STATE_FLAG_INPUT_OVERFLOW = 0x01u,
    MON_STATE_FLAG_TRACE_OUTPUT = 0x02u,
    MON_STATE_FLAG_OUTPUT_ACTIVE = 0x04u,
    MON_STATE_FLAG_HAS_WRITABLES = 0x08u
};

typedef struct mon_state {
    char messages[MON_MAX_QUEUED_MESSAGES][MON_MAX_MESSAGE_LENGTH];
    char input_buffer[MON_MAX_INPUT_LENGTH];
    const char *welcome_message;
    uint32_t dropped_messages;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t staging_length;
    uint8_t input_length;
    uint8_t trace_count;
    uint8_t flags;
    mon_trace_entry_t traces[MON_MAX_TRACES];
#if MONITOR_ENABLE_SHELL_FEATURES
    char history[MON_HISTORY_DEPTH][MON_MAX_INPUT_LENGTH];
    uint8_t history_count;
    uint8_t history_head;
    uint8_t history_cursor;
    uint8_t esc_state;
#endif
} mon_state_t;

static mon_state_t g_mon_state;

static bool mon_is_whitespace(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r');
}

static bool mon_state_flag_is_set(uint8_t flag)
{
    return (g_mon_state.flags & flag) != 0u;
}

static void mon_state_set_flag(uint8_t flag, bool enabled)
{
    if (enabled) {
        g_mon_state.flags = (uint8_t)(g_mon_state.flags | flag);
    } else {
        g_mon_state.flags = (uint8_t)(g_mon_state.flags & (uint8_t)(~flag));
    }
}

static uint8_t mon_next_queue_index(uint8_t index)
{
    index++;
    if (index >= MON_MAX_QUEUED_MESSAGES) {
        index = 0u;
    }
    return index;
}

static void mon_maybe_inject_drop_notice(void);

static void mon_stage_ensure_begin(void)
{
    if (g_mon_state.staging_length == 0u) {
        mon_maybe_inject_drop_notice();
    }
}

static void mon_stage_append_n(const char *text, size_t length)
{
    while (length > 0u) {
        size_t available;
        size_t chunk;
        char *slot;

        mon_stage_ensure_begin();

        if (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES) {
            g_mon_state.dropped_messages++;
            return;
        }

        slot = g_mon_state.messages[g_mon_state.tail];
        available = (MON_MAX_MESSAGE_LENGTH - 1u) - g_mon_state.staging_length;
        chunk = (length < available) ? length : available;

        (void)memcpy(&slot[g_mon_state.staging_length], text, chunk);
        g_mon_state.staging_length =
            (uint8_t)(g_mon_state.staging_length + chunk);
        text += chunk;
        length -= chunk;

        if (length > 0u) {
            slot[g_mon_state.staging_length] = '\0';
            g_mon_state.tail = mon_next_queue_index(g_mon_state.tail);
            g_mon_state.count++;
            g_mon_state.staging_length = 0u;
        }
    }
}

static void mon_stage_append_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    mon_stage_append_n(text, strlen(text));
}

static void mon_stage_append_char(char c)
{
    mon_stage_append_n(&c, 1u);
}

static void mon_stage_append_u32(uint32_t value)
{
    char digits[10];
    size_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (count > 0u) {
        count--;
        mon_stage_append_char(digits[count]);
    }
}

static void mon_stage_append_i32(int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        mon_stage_append_char('-');
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }
    mon_stage_append_u32(magnitude);
}

static void mon_stage_append_u64(uint64_t value)
{
    char digits[20];
    size_t count = 0u;

    if (value <= UINT32_MAX) {
        mon_stage_append_u32((uint32_t)value);
        return;
    }

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (count > 0u) {
        count--;
        mon_stage_append_char(digits[count]);
    }
}

static void mon_stage_append_i64(int64_t value)
{
    uint64_t magnitude;

    if (value < 0) {
        mon_stage_append_char('-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }
    mon_stage_append_u64(magnitude);
}

static void mon_stage_commit(void)
{
    char *slot;

    if (g_mon_state.staging_length == 0u) {
        return;
    }

    slot = g_mon_state.messages[g_mon_state.tail];
    slot[g_mon_state.staging_length] = '\0';
    g_mon_state.tail = mon_next_queue_index(g_mon_state.tail);
    g_mon_state.count++;
    g_mon_state.staging_length = 0u;
}

static void mon_stage_finish_line(void)
{
    mon_stage_append_char('\n');
    mon_stage_commit();
}

static void mon_maybe_inject_drop_notice(void)
{
    uint32_t dropped;

    if ((g_mon_state.dropped_messages == 0u) ||
        (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES)) {
        return;
    }

    dropped = g_mon_state.dropped_messages;
    g_mon_state.dropped_messages = 0u;

    mon_stage_append_n("[monitor] dropped ", 18u);
    mon_stage_append_u32(dropped);
    mon_stage_append_n(" message(s)\n", 12u);
    mon_stage_commit();
}

static void mon_queue_text(const char *text)
{
    mon_stage_append_text(text);
    mon_stage_commit();
}

static void mon_queue_named_line(const char *prefix, const char *name)
{
    mon_stage_append_text(prefix);
    mon_stage_append_text(name);
    mon_stage_finish_line();
}

static mon_name_view_t mon_normalize_identifier(const char *source)
{
    mon_name_view_t view = { "", 0u };
    const char *start = source;
    size_t length;

    if (start == NULL) {
        return view;
    }

    while ((*start != '\0') && mon_is_whitespace(*start)) {
        start++;
    }

    if (*start == '&') {
        start++;
        while ((*start != '\0') && mon_is_whitespace(*start)) {
            start++;
        }
    }

    length = strlen(start);

    while ((length > 0u) && mon_is_whitespace(start[length - 1u])) {
        length--;
    }

    while ((length >= 2u) && (start[0] == '(') && (start[length - 1u] == ')')) {
        start++;
        length -= 2u;

        while ((length > 0u) && mon_is_whitespace(*start)) {
            start++;
            length--;
        }

        while ((length > 0u) && mon_is_whitespace(start[length - 1u])) {
            length--;
        }
    }

    if (length >= MON_MAX_NAME_LENGTH) {
        length = MON_MAX_NAME_LENGTH - 1u;
    }

    view.ptr = start;
    view.length = (uint8_t)length;
    return view;
}

static bool mon_name_equals_view(const mon_trace_entry_t *entry,
                                 mon_name_view_t name)
{
    if (entry->name_length != name.length) {
        return false;
    }
    if (name.length == 0u) {
        return true;
    }
    return memcmp(entry->name_ptr, name.ptr, name.length) == 0;
}

static mon_trace_entry_t *mon_find_trace_by_view(mon_name_view_t name)
{
    uint8_t index;

    if (name.length == 0u) {
        return NULL;
    }

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        if (mon_name_equals_view(&g_mon_state.traces[index], name)) {
            return &g_mon_state.traces[index];
        }
    }
    return NULL;
}

static mon_trace_entry_t *mon_find_trace_by_name(const char *name)
{
    mon_name_view_t view;
    size_t length;

    if (name == NULL) {
        return NULL;
    }
    length = strlen(name);
    if ((length == 0u) || (length >= MON_MAX_NAME_LENGTH)) {
        return NULL;
    }
    view.ptr = name;
    view.length = (uint8_t)length;
    return mon_find_trace_by_view(view);
}

static mon_trace_entry_t *mon_find_trace_by_pointer(const void *ptr)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];
        if (((entry->flags & MON_TRACE_FLAG_WRITABLE) != 0u) &&
            (entry->source_ptr == ptr)) {
            return entry;
        }
    }
    return NULL;
}

static bool mon_name_conflicts(const mon_trace_entry_t *skip,
                               mon_name_view_t name)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        const mon_trace_entry_t *entry = &g_mon_state.traces[index];
        if (entry == skip) {
            continue;
        }
        if (mon_name_equals_view(entry, name)) {
            return true;
        }
    }
    return false;
}

static void mon_queue_duplicate_name(mon_name_view_t name)
{
    mon_stage_append_n("[monitor] duplicate trace name: ", 32u);
    mon_stage_append_n(name.ptr, name.length);
    mon_stage_finish_line();
}

static uint8_t mon_type_size(uint8_t type)
{
    return g_mon_type_info[type].size;
}

static uint8_t mon_type_flags(uint8_t type)
{
    return g_mon_type_info[type].flags;
}

static void mon_read_source(const mon_trace_entry_t *entry, uint8_t out[8])
{
    (void)memset(out, 0, 8u);

    if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
        (void)memcpy(out, entry->last_value, 8u);
        return;
    }

    if (entry->source_ptr != NULL) {
        (void)memcpy(out, entry->source_ptr, mon_type_size(entry->type));
    }
}

static uint64_t mon_bytes_as_u64(const uint8_t bytes[8], uint8_t size)
{
    uint64_t value = 0u;
    (void)memcpy(&value, bytes, size);
    return value;
}

static int64_t mon_bytes_as_i64(const uint8_t bytes[8], uint8_t size)
{
    uint64_t u = mon_bytes_as_u64(bytes, size);

    switch (size) {
    case 1u:
        return (int64_t)(int8_t)u;
    case 2u:
        return (int64_t)(int16_t)u;
    case 4u:
        return (int64_t)(int32_t)u;
    default:
        return (int64_t)u;
    }
}

static void mon_stage_append_value(uint8_t type, const uint8_t bytes[8])
{
    const uint8_t size = mon_type_size(type);
    const uint8_t flags = mon_type_flags(type);

#if MONITOR_ENABLE_FLOAT_SUPPORT
    if ((flags & MON_TYPE_FLAG_FLOAT) != 0u) {
        char buffer[32];
        int written;

        if (size == 4u) {
            float f;
            (void)memcpy(&f, bytes, 4u);
            written = snprintf(buffer, sizeof(buffer), "%.9g", (double)f);
        } else {
            double d;
            (void)memcpy(&d, bytes, 8u);
            written = snprintf(buffer, sizeof(buffer), "%.17g", d);
        }

        if (written < 0) {
            mon_stage_append_char('?');
            return;
        }
        if ((size_t)written >= sizeof(buffer)) {
            written = (int)sizeof(buffer) - 1;
        }
        mon_stage_append_n(buffer, (size_t)written);
        return;
    }
#endif

    if ((flags & MON_TYPE_FLAG_SIGNED) != 0u) {
        if (size <= 4u) {
            mon_stage_append_i32((int32_t)mon_bytes_as_i64(bytes, size));
        } else {
            mon_stage_append_i64(mon_bytes_as_i64(bytes, size));
        }
    } else {
        if (size <= 4u) {
            mon_stage_append_u32((uint32_t)mon_bytes_as_u64(bytes, size));
        } else {
            mon_stage_append_u64(mon_bytes_as_u64(bytes, size));
        }
    }
#if !MONITOR_ENABLE_FLOAT_SUPPORT
    (void)flags;
#endif
}

static void mon_queue_entry_value(const mon_trace_entry_t *entry,
                                  const char *prefix)
{
    uint8_t bytes[8];

    mon_read_source(entry, bytes);

    if (prefix != NULL) {
        mon_stage_append_text(prefix);
    }
    mon_stage_append_n(entry->name_ptr, entry->name_length);
    mon_stage_append_n(" (", 2u);
    mon_stage_append_text(g_mon_type_info[entry->type].name);
    mon_stage_append_n(") = ", 4u);
    mon_stage_append_value(entry->type, bytes);
    mon_stage_finish_line();
}

static int mon_digit_value(char ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return ch - '0';
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return 10 + (ch - 'a');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return 10 + (ch - 'A');
    }
    return -1;
}

static bool mon_parse_u32_digits(const char *text,
                                 uint32_t maximum,
                                 uint32_t *value)
{
    const char *cursor = text;
    uint32_t parsed = 0u;
    uint32_t base = 10u;

    if (*cursor == '\0') {
        return false;
    }

    if ((cursor[0] == '0') && ((cursor[1] == 'x') || (cursor[1] == 'X'))) {
        base = 16u;
        cursor += 2;
    } else if ((cursor[0] == '0') && (cursor[1] != '\0')) {
        base = 8u;
    }

    if (*cursor == '\0') {
        return false;
    }

    while (*cursor != '\0') {
        const int digit = mon_digit_value(*cursor);
        if ((digit < 0) || ((uint32_t)digit >= base)) {
            return false;
        }
        if (parsed > ((maximum - (uint32_t)digit) / base)) {
            return false;
        }
        parsed = (parsed * base) + (uint32_t)digit;
        cursor++;
    }

    *value = parsed;
    return true;
}

static bool mon_parse_u64_digits(const char *text,
                                 uint64_t maximum,
                                 uint64_t *value)
{
    const char *cursor = text;
    uint64_t parsed = 0u;
    uint64_t base = 10u;

    if (*cursor == '\0') {
        return false;
    }

    if ((cursor[0] == '0') && ((cursor[1] == 'x') || (cursor[1] == 'X'))) {
        base = 16u;
        cursor += 2;
    } else if ((cursor[0] == '0') && (cursor[1] != '\0')) {
        base = 8u;
    }

    if (*cursor == '\0') {
        return false;
    }

    while (*cursor != '\0') {
        const int digit = mon_digit_value(*cursor);
        if ((digit < 0) || ((uint64_t)digit >= base)) {
            return false;
        }
        if (parsed > ((maximum - (uint64_t)digit) / base)) {
            return false;
        }
        parsed = (parsed * base) + (uint64_t)digit;
        cursor++;
    }

    *value = parsed;
    return true;
}

static bool mon_parse_integer(const char *text,
                              uint8_t size,
                              bool is_signed,
                              int64_t *out_signed,
                              uint64_t *out_unsigned)
{
    const char *cursor = text;
    bool negative = false;

    if (cursor == NULL) {
        return false;
    }

    if (*cursor == '+') {
        cursor++;
    } else if (*cursor == '-') {
        if (!is_signed) {
            return false;
        }
        negative = true;
        cursor++;
    }

    if (is_signed) {
        uint64_t magnitude_limit;
        uint64_t magnitude;

        if (negative) {
            magnitude_limit = (uint64_t)1u << ((size * 8u) - 1u);
        } else {
            magnitude_limit = ((uint64_t)1u << ((size * 8u) - 1u)) - 1u;
        }

        if (size <= 4u) {
            uint32_t m32;
            if (!mon_parse_u32_digits(cursor,
                                      (uint32_t)magnitude_limit,
                                      &m32)) {
                return false;
            }
            magnitude = m32;
        } else {
            if (!mon_parse_u64_digits(cursor, magnitude_limit, &magnitude)) {
                return false;
            }
        }

        if (negative) {
            if (magnitude == magnitude_limit) {
                *out_signed = (int64_t)(-(int64_t)(magnitude - 1u)) - 1;
            } else {
                *out_signed = -(int64_t)magnitude;
            }
        } else {
            *out_signed = (int64_t)magnitude;
        }
        return true;
    }

    if (size <= 4u) {
        uint64_t limit = (size == 4u) ? UINT32_MAX
                                      : (((uint64_t)1u << (size * 8u)) - 1u);
        uint32_t v32;
        if (!mon_parse_u32_digits(cursor, (uint32_t)limit, &v32)) {
            return false;
        }
        *out_unsigned = v32;
        return true;
    }

    return mon_parse_u64_digits(cursor, UINT64_MAX, out_unsigned);
}

#if MONITOR_ENABLE_FLOAT_SUPPORT
static bool mon_parse_float64(const char *text, double *value)
{
    char *end = NULL;
    double parsed_value;

    if ((text == NULL) || (value == NULL)) {
        return false;
    }

    errno = 0;
    parsed_value = strtod(text, &end);

    if ((text == end) || (end == NULL) || (*end != '\0') ||
        (errno == ERANGE)) {
        return false;
    }

    *value = parsed_value;
    return true;
}
#endif

static mon_write_result_t mon_write_value(mon_trace_entry_t *entry,
                                          const char *text)
{
    uint8_t type;
    uint8_t size;
    uint8_t flags;

    if ((entry == NULL) || (text == NULL)) {
        return MON_WRITE_RESULT_INVALID;
    }
    if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
        return MON_WRITE_RESULT_READ_ONLY;
    }

    type = entry->type;
    size = mon_type_size(type);
    flags = mon_type_flags(type);

#if MONITOR_ENABLE_FLOAT_SUPPORT
    if ((flags & MON_TYPE_FLAG_FLOAT) != 0u) {
        double parsed;
        if (!mon_parse_float64(text, &parsed)) {
            return MON_WRITE_RESULT_INVALID;
        }
        if (size == 4u) {
            float f;
            if (isfinite(parsed) &&
                ((parsed > (double)FLT_MAX) ||
                 (parsed < -(double)FLT_MAX))) {
                return MON_WRITE_RESULT_INVALID;
            }
            f = (float)parsed;
            (void)memcpy(entry->source_ptr, &f, 4u);
        } else {
            (void)memcpy(entry->source_ptr, &parsed, 8u);
        }
    } else
#endif
    {
        int64_t signed_value = 0;
        uint64_t unsigned_value = 0u;

        if (!mon_parse_integer(text,
                               size,
                               (flags & MON_TYPE_FLAG_SIGNED) != 0u,
                               &signed_value,
                               &unsigned_value)) {
            return MON_WRITE_RESULT_INVALID;
        }

        if ((flags & MON_TYPE_FLAG_SIGNED) != 0u) {
            (void)memcpy(entry->source_ptr, &signed_value, size);
        } else {
            (void)memcpy(entry->source_ptr, &unsigned_value, size);
        }
    }

    (void)memset(entry->last_value, 0, 8u);
    (void)memcpy(entry->last_value, entry->source_ptr, size);
    return MON_WRITE_RESULT_OK;
}

static void mon_sync_writable_traces(void)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];
        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
            continue;
        }
        (void)memset(entry->last_value, 0, 8u);
        (void)memcpy(entry->last_value,
                     entry->source_ptr,
                     mon_type_size(entry->type));
    }
}

static void mon_check_writable_traces(void)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];
        uint8_t current[8];

        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
            continue;
        }

        (void)memset(current, 0, 8u);
        (void)memcpy(current,
                     entry->source_ptr,
                     mon_type_size(entry->type));

        if (memcmp(entry->last_value, current, 8u) != 0) {
            (void)memcpy(entry->last_value, current, 8u);
            mon_queue_entry_value(entry, "trace ");
        }
    }
}

static void mon_handle_help_command(void)
{
    mon_queue_text(g_mon_state.welcome_message);
    mon_queue_text(g_mon_help_text);
}

static void mon_handle_list_command(void)
{
    uint8_t index;

    if (g_mon_state.trace_count == 0u) {
        mon_queue_text("No traces registered.\n");
        return;
    }

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_queue_entry_value(&g_mon_state.traces[index], NULL);
    }
}

static void mon_handle_get_command(const char *name)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);

    if (entry == NULL) {
        mon_queue_named_line("Unknown variable: ", name);
        return;
    }
    mon_queue_entry_value(entry, NULL);
}

static void mon_handle_set_command(const char *name, const char *value)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);
    mon_write_result_t result;

    if (entry == NULL) {
        mon_queue_named_line("Unknown variable: ", name);
        return;
    }

    result = mon_write_value(entry, value);

    if (result == MON_WRITE_RESULT_READ_ONLY) {
        mon_queue_named_line("Variable is read-only: ", name);
        return;
    }

    if (result != MON_WRITE_RESULT_OK) {
        mon_stage_append_n("Invalid value for ", 18u);
        mon_stage_append_text(name);
        mon_stage_append_n(": ", 2u);
        mon_stage_append_text(value);
        mon_stage_finish_line();
        return;
    }

    mon_queue_entry_value(entry, NULL);
}

static void mon_handle_trace_command(const char *argument)
{
    if (argument == NULL) {
        mon_queue_text(mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)
                           ? g_mon_trace_output_on_text
                           : g_mon_trace_output_off_text);
        return;
    }

    if (strcmp(argument, "on") == 0) {
        if (!mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)) {
            mon_sync_writable_traces();
            mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT, true);
        }
        mon_queue_text(g_mon_trace_output_on_text);
        return;
    }

    if (strcmp(argument, "off") == 0) {
        mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT, false);
        mon_queue_text(g_mon_trace_output_off_text);
        return;
    }

    mon_queue_text("Usage: trace [on|off]\n");
}

static char *mon_next_token(char **cursor)
{
    char *start = *cursor;

    while ((*start != '\0') && mon_is_whitespace(*start)) {
        start++;
    }

    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    *cursor = start;
    while ((**cursor != '\0') && !mon_is_whitespace(**cursor)) {
        (*cursor)++;
    }

    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }
    return start;
}

static void mon_process_line(char *line)
{
    char *cursor = line;
    char *command = mon_next_token(&cursor);
    char *argument1;
    char *argument2;

    if (command == NULL) {
        return;
    }

    if ((strcmp(command, "help") == 0) || (strcmp(command, "?") == 0)) {
        if (mon_next_token(&cursor) != NULL) {
            mon_queue_text("Usage: help\n");
            return;
        }
        mon_handle_help_command();
        return;
    }

    if (strcmp(command, "list") == 0) {
        if (mon_next_token(&cursor) != NULL) {
            mon_queue_text("Usage: list\n");
            return;
        }
        mon_handle_list_command();
        return;
    }

    if (strcmp(command, "get") == 0) {
        argument1 = mon_next_token(&cursor);
        if ((argument1 == NULL) || (mon_next_token(&cursor) != NULL)) {
            mon_queue_text("Usage: get <name>\n");
            return;
        }
        mon_handle_get_command(argument1);
        return;
    }

    if (strcmp(command, "set") == 0) {
        argument1 = mon_next_token(&cursor);
        argument2 = mon_next_token(&cursor);
        if ((argument1 == NULL) || (argument2 == NULL) ||
            (mon_next_token(&cursor) != NULL)) {
            mon_queue_text("Usage: set <name> <value>\n");
            return;
        }
        mon_handle_set_command(argument1, argument2);
        return;
    }

    if (strcmp(command, "trace") == 0) {
        argument1 = mon_next_token(&cursor);
        if (mon_next_token(&cursor) != NULL) {
            mon_queue_text("Usage: trace [on|off]\n");
            return;
        }
        mon_handle_trace_command(argument1);
        return;
    }

    mon_queue_named_line("Unknown command: ", command);
    mon_handle_help_command();
}

#if MONITOR_ENABLE_SHELL_FEATURES
static void mon_echo_n(const char *text, size_t length)
{
    if (mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
        return;
    }
    mon_stage_append_n(text, length);
}

static void mon_echo_char(char c)
{
    mon_echo_n(&c, 1u);
}

static void mon_echo_erase_current_line(void)
{
    uint8_t remaining = g_mon_state.input_length;

    while (remaining > 0u) {
        mon_echo_n("\b \b", 3u);
        remaining--;
    }
}

static void mon_history_push(void)
{
    uint8_t length = g_mon_state.input_length;

    if (length == 0u) {
        return;
    }

    (void)memcpy(g_mon_state.history[g_mon_state.history_head],
                 g_mon_state.input_buffer,
                 length);
    g_mon_state.history[g_mon_state.history_head][length] = '\0';

    g_mon_state.history_head++;
    if (g_mon_state.history_head >= MON_HISTORY_DEPTH) {
        g_mon_state.history_head = 0u;
    }
    if (g_mon_state.history_count < MON_HISTORY_DEPTH) {
        g_mon_state.history_count++;
    }
    g_mon_state.history_cursor = 0u;
}

static void mon_history_recall(bool direction_up)
{
    uint8_t slot;
    uint8_t length;

    if (direction_up) {
        if (g_mon_state.history_cursor >= g_mon_state.history_count) {
            return;
        }
    } else {
        if (g_mon_state.history_cursor == 0u) {
            return;
        }
    }

    mon_echo_erase_current_line();
    mon_state_set_flag(MON_STATE_FLAG_INPUT_OVERFLOW, false);

    if (direction_up) {
        g_mon_state.history_cursor++;
    } else {
        g_mon_state.history_cursor--;
    }

    if (g_mon_state.history_cursor == 0u) {
        g_mon_state.input_length = 0u;
        return;
    }

    slot = (uint8_t)((g_mon_state.history_head + MON_HISTORY_DEPTH -
                      g_mon_state.history_cursor) %
                     MON_HISTORY_DEPTH);
    length = (uint8_t)strlen(g_mon_state.history[slot]);

    (void)memcpy(g_mon_state.input_buffer,
                 g_mon_state.history[slot],
                 length);
    g_mon_state.input_length = length;
    mon_echo_n(g_mon_state.history[slot], length);
}

static void mon_tab_complete(void)
{
    uint8_t prefix_start;
    uint8_t prefix_length;
    uint8_t index;
    uint8_t match_count = 0u;
    const mon_trace_entry_t *match = NULL;
    uint8_t append_length;
    uint8_t appended = 0u;

    if (mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
        return;
    }

    prefix_start = g_mon_state.input_length;
    while ((prefix_start > 0u) &&
           !mon_is_whitespace(g_mon_state.input_buffer[prefix_start - 1u])) {
        prefix_start--;
    }
    prefix_length = (uint8_t)(g_mon_state.input_length - prefix_start);
    if (prefix_length == 0u) {
        return;
    }

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        const mon_trace_entry_t *entry = &g_mon_state.traces[index];
        if (entry->name_length <= prefix_length) {
            continue;
        }
        if (memcmp(entry->name_ptr,
                   &g_mon_state.input_buffer[prefix_start],
                   prefix_length) != 0) {
            continue;
        }
        match_count++;
        if (match_count > 1u) {
            return;
        }
        match = entry;
    }

    if (match_count != 1u) {
        return;
    }

    append_length = (uint8_t)(match->name_length - prefix_length);
    while (appended < append_length) {
        char c = match->name_ptr[prefix_length + appended];
        if ((g_mon_state.input_length + 1u) >= MON_MAX_INPUT_LENGTH) {
            mon_state_set_flag(MON_STATE_FLAG_INPUT_OVERFLOW, true);
            return;
        }
        g_mon_state.input_buffer[g_mon_state.input_length] = c;
        g_mon_state.input_length++;
        mon_echo_char(c);
        appended++;
    }
}
#endif

static void mon_process_input(const char *input)
{
    const unsigned char *cursor = (const unsigned char *)(const void *)input;

    while (*cursor != '\0') {
        const unsigned char ch = *cursor;

#if MONITOR_ENABLE_SHELL_FEATURES
        if (g_mon_state.esc_state == 1u) {
            g_mon_state.esc_state = (ch == '[') ? 2u : 0u;
            cursor++;
            continue;
        }
        if (g_mon_state.esc_state == 2u) {
            g_mon_state.esc_state = 0u;
            if (ch == 'A') {
                mon_history_recall(true);
            } else if (ch == 'B') {
                mon_history_recall(false);
            }
            cursor++;
            continue;
        }
        if (ch == 0x1BU) {
            g_mon_state.esc_state = 1u;
            cursor++;
            continue;
        }
#endif

        if ((ch == '\r') || (ch == '\n')) {
            if (mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
                mon_queue_text("[monitor] input line too long\n");
            } else if (g_mon_state.input_length > 0u) {
                g_mon_state.input_buffer[g_mon_state.input_length] = '\0';
#if MONITOR_ENABLE_SHELL_FEATURES
                mon_echo_n("\r\n", 2u);
                mon_stage_commit();
                mon_history_push();
#endif
                mon_process_line(g_mon_state.input_buffer);
            }
#if MONITOR_ENABLE_SHELL_FEATURES
            else if (!mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
                mon_echo_n("\r\n", 2u);
            }
            g_mon_state.history_cursor = 0u;
#endif
            g_mon_state.input_length = 0u;
            mon_state_set_flag(MON_STATE_FLAG_INPUT_OVERFLOW, false);
            cursor++;
            continue;
        }

        if ((ch == '\b') || (ch == 0x7FU)) {
            if (!mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW) &&
                (g_mon_state.input_length > 0u)) {
                g_mon_state.input_length--;
#if MONITOR_ENABLE_SHELL_FEATURES
                mon_echo_n("\b \b", 3u);
#endif
            }
            cursor++;
            continue;
        }

#if MONITOR_ENABLE_SHELL_FEATURES
        if (ch == '\t') {
            mon_tab_complete();
            cursor++;
            continue;
        }
#endif

        if (mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
            cursor++;
            continue;
        }

        if ((ch < 0x20U) && (ch != '\t')) {
            cursor++;
            continue;
        }

        if ((g_mon_state.input_length + 1u) >= MON_MAX_INPUT_LENGTH) {
            mon_state_set_flag(MON_STATE_FLAG_INPUT_OVERFLOW, true);
            cursor++;
            continue;
        }

        g_mon_state.input_buffer[g_mon_state.input_length] =
            (char)((ch == '\t') ? ' ' : ch);
        g_mon_state.input_length++;
#if MONITOR_ENABLE_SHELL_FEATURES
        mon_echo_char((char)ch);
#endif
        cursor++;
    }

#if MONITOR_ENABLE_SHELL_FEATURES
    if (g_mon_state.staging_length > 0u) {
        mon_stage_commit();
    }
#endif
}

static const char *mon_dequeue_message(void)
{
    mon_maybe_inject_drop_notice();

    if (g_mon_state.count == 0u) {
        return NULL;
    }
    mon_state_set_flag(MON_STATE_FLAG_OUTPUT_ACTIVE, true);
    return g_mon_state.messages[g_mon_state.head];
}

static void mon_finish_output_delivery(void)
{
    if (!mon_state_flag_is_set(MON_STATE_FLAG_OUTPUT_ACTIVE)) {
        return;
    }
    g_mon_state.head = mon_next_queue_index(g_mon_state.head);
    g_mon_state.count--;
    mon_state_set_flag(MON_STATE_FLAG_OUTPUT_ACTIVE, false);
}

#if !MONITOR_ENABLE_FLOAT_SUPPORT
static void mon_queue_float_support_disabled(void)
{
    mon_finish_output_delivery();
    mon_queue_text(g_mon_float_support_disabled);
}
#endif

static void mon_register_pointer_trace(void *ptr,
                                       uint8_t type,
                                       const char *human_identifier)
{
    mon_name_view_t name;
    mon_trace_entry_t *entry;
    uint8_t previous_type;

    mon_finish_output_delivery();

    if (ptr == NULL) {
        mon_queue_text("[monitor] null trace pointer ignored\n");
        return;
    }

    name = mon_normalize_identifier(human_identifier);
    if (name.length == 0u) {
        mon_queue_text("[monitor] empty trace name ignored\n");
        return;
    }

    entry = mon_find_trace_by_pointer(ptr);
    if (entry != NULL) {
        previous_type = entry->type;

        if (mon_name_conflicts(entry, name)) {
            mon_queue_duplicate_name(name);
            return;
        }

        entry->source_ptr = ptr;
        entry->name_ptr = name.ptr;
        entry->name_length = name.length;
        entry->type = type;
        entry->flags = MON_TRACE_FLAG_WRITABLE;

        if (previous_type != type) {
            (void)memset(entry->last_value, 0, 8u);
            (void)memcpy(entry->last_value, ptr, mon_type_size(type));
        }
        return;
    }

    if (g_mon_state.trace_count >= MON_MAX_TRACES) {
        mon_queue_text("[monitor] trace registry full\n");
        return;
    }

    if (mon_name_conflicts(NULL, name)) {
        mon_queue_duplicate_name(name);
        return;
    }

    entry = &g_mon_state.traces[g_mon_state.trace_count];
    entry->source_ptr = ptr;
    entry->name_ptr = name.ptr;
    entry->name_length = name.length;
    entry->type = type;
    entry->flags = MON_TRACE_FLAG_WRITABLE;
    (void)memset(entry->last_value, 0, 8u);
    (void)memcpy(entry->last_value, ptr, mon_type_size(type));
    g_mon_state.trace_count++;
    mon_state_set_flag(MON_STATE_FLAG_HAS_WRITABLES, true);
}

static void mon_register_value_trace(const uint8_t value[8],
                                     uint8_t type,
                                     const char *human_identifier)
{
    mon_name_view_t name;
    mon_trace_entry_t *entry;
    uint8_t previous_type;

    mon_finish_output_delivery();

    name = mon_normalize_identifier(human_identifier);
    if (name.length == 0u) {
        mon_queue_text("[monitor] empty trace name ignored\n");
        return;
    }

    entry = mon_find_trace_by_view(name);
    if (entry != NULL) {
        previous_type = entry->type;

        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) != 0u) {
            mon_queue_duplicate_name(name);
            return;
        }

        entry->name_ptr = name.ptr;
        entry->name_length = name.length;
        entry->type = type;

        if ((previous_type != type) ||
            (memcmp(entry->last_value, value, 8u) != 0)) {
            (void)memcpy(entry->last_value, value, 8u);
            if ((previous_type == type) &&
                mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)) {
                mon_queue_entry_value(entry, "trace ");
            }
        }
        return;
    }

    if (g_mon_state.trace_count >= MON_MAX_TRACES) {
        mon_queue_text("[monitor] trace registry full\n");
        return;
    }

    if (mon_name_conflicts(NULL, name)) {
        mon_queue_duplicate_name(name);
        return;
    }

    entry = &g_mon_state.traces[g_mon_state.trace_count];
    entry->source_ptr = NULL;
    entry->name_ptr = name.ptr;
    entry->name_length = name.length;
    entry->type = type;
    entry->flags = 0u;
    (void)memcpy(entry->last_value, value, 8u);
    g_mon_state.trace_count++;
}

void mon_reset(const char *welcome_message)
{
    (void)memset(&g_mon_state, 0, sizeof(g_mon_state));
#ifndef NDEBUG
    mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT, true);
#endif
    g_mon_state.welcome_message =
        (welcome_message != NULL) ? welcome_message : g_mon_default_welcome;
    mon_queue_text(g_mon_state.welcome_message);
}

const char *mon_task(const char *input, int device_ready)
{
    mon_finish_output_delivery();

    if (mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT) &&
        mon_state_flag_is_set(MON_STATE_FLAG_HAS_WRITABLES)) {
        mon_check_writable_traces();
    }

    if (input != NULL) {
        mon_process_input(input);
    }

    if (device_ready == 0) {
        return NULL;
    }

    return mon_dequeue_message();
}

const char *mon_print(const char *fmt, ...)
{
    va_list args;
    int written;
    char *slot;

    mon_finish_output_delivery();

    if (fmt == NULL) {
        return NULL;
    }

    if (g_mon_state.staging_length > 0u) {
        mon_stage_commit();
    }
    mon_maybe_inject_drop_notice();

    if (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES) {
        g_mon_state.dropped_messages++;
        return NULL;
    }

    slot = g_mon_state.messages[g_mon_state.tail];
    va_start(args, fmt);
    written = vsnprintf(slot, MON_MAX_MESSAGE_LENGTH, fmt, args);
    va_end(args);

    if (written < 0) {
        slot[0] = '\0';
        mon_queue_text("[monitor] formatting failure\n");
        return NULL;
    }

    if ((size_t)written >= MON_MAX_MESSAGE_LENGTH) {
        slot[MON_MAX_MESSAGE_LENGTH - 1u] = '\0';
    }
    g_mon_state.tail = mon_next_queue_index(g_mon_state.tail);
    g_mon_state.count++;

    if ((size_t)written >= MON_MAX_FORMAT_LENGTH) {
        mon_queue_text("[monitor] formatted output truncated\n");
    }
    return NULL;
}

#define MON_DEFINE_TRACE_FUNCTION(function_name, value_type, enum_type)       \
    void function_name(value_type *value, const char *human_identifier)       \
    {                                                                         \
        mon_register_pointer_trace((void *)value,                             \
                                   (uint8_t)(enum_type),                      \
                                   human_identifier);                         \
    }

#define MON_DEFINE_VALUE_TRACE_FUNCTION(function_name, value_type, enum_type) \
    void function_name(value_type value, const char *human_identifier)        \
    {                                                                         \
        uint8_t bytes[8];                                                     \
        (void)memset(bytes, 0, 8u);                                           \
        (void)memcpy(bytes, &value, sizeof(value));                           \
        mon_register_value_trace(bytes,                                       \
                                 (uint8_t)(enum_type),                        \
                                 human_identifier);                           \
    }

MON_DEFINE_TRACE_FUNCTION(mon_trace_u8, uint8_t, MON_VALUE_U8)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i8, int8_t, MON_VALUE_I8)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u16, uint16_t, MON_VALUE_U16)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i16, int16_t, MON_VALUE_I16)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u32, uint32_t, MON_VALUE_U32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i32, int32_t, MON_VALUE_I32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u64, uint64_t, MON_VALUE_U64)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i64, int64_t, MON_VALUE_I64)
#if MONITOR_ENABLE_FLOAT_SUPPORT
MON_DEFINE_TRACE_FUNCTION(mon_trace_f32, float, MON_VALUE_F32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_f64, double, MON_VALUE_F64)
#else
void mon_trace_f32(float *value, const char *human_identifier)
{
    (void)value;
    (void)human_identifier;
    mon_queue_float_support_disabled();
}

void mon_trace_f64(double *value, const char *human_identifier)
{
    (void)value;
    (void)human_identifier;
    mon_queue_float_support_disabled();
}
#endif

MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u8_value, uint8_t, MON_VALUE_U8)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i8_value, int8_t, MON_VALUE_I8)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u16_value, uint16_t, MON_VALUE_U16)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i16_value, int16_t, MON_VALUE_I16)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u32_value, uint32_t, MON_VALUE_U32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i32_value, int32_t, MON_VALUE_I32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u64_value, uint64_t, MON_VALUE_U64)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i64_value, int64_t, MON_VALUE_I64)
#if MONITOR_ENABLE_FLOAT_SUPPORT
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_f32_value, float, MON_VALUE_F32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_f64_value, double, MON_VALUE_F64)
#else
void mon_trace_f32_value(float value, const char *human_identifier)
{
    (void)value;
    (void)human_identifier;
    mon_queue_float_support_disabled();
}

void mon_trace_f64_value(double value, const char *human_identifier)
{
    (void)value;
    (void)human_identifier;
    mon_queue_float_support_disabled();
}
#endif
