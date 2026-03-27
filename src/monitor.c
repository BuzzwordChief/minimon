#include "monitor.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MONITOR_ENABLE_FLOAT_SUPPORT
#include <errno.h>
#include <float.h>
#include <math.h>
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
_Static_assert(MON_MAX_QUEUED_MESSAGES > 0u,
               "MON_MAX_QUEUED_MESSAGES must be greater than zero");
_Static_assert(MON_MAX_QUEUED_MESSAGES <= UINT8_MAX,
               "MON_MAX_QUEUED_MESSAGES must fit in uint8_t");
_Static_assert(MON_MAX_FORMAT_LENGTH > 1u,
               "MON_MAX_FORMAT_LENGTH must be greater than one");

static const char g_mon_default_welcome[] =
    "minimon ready. Type 'help' for commands.\n";
static const char g_mon_help_text[] =
    "Commands:\n"
    "  help\n"
    "  list\n"
    "  get <name>\n"
    "  set <name> <value>\n"
    "  trace [on|off]\n";
static const char g_mon_format_failure_text[] =
    "[monitor] formatting failure\n";
static const char g_mon_format_truncated_text[] =
    "[monitor] formatted output truncated\n";
static const char g_mon_input_too_long_text[] =
    "[monitor] input line too long\n";
static const char g_mon_duplicate_name_prefix[] =
    "[monitor] duplicate trace name: ";
static const char g_mon_trace_registry_full_text[] =
    "[monitor] trace registry full\n";
static const char g_mon_null_pointer_text[] =
    "[monitor] null trace pointer ignored\n";
static const char g_mon_empty_name_text[] =
    "[monitor] empty trace name ignored\n";
static const char g_mon_unknown_variable_prefix[] =
    "Unknown variable: ";
static const char g_mon_read_only_prefix[] =
    "Variable is read-only: ";
static const char g_mon_invalid_value_prefix[] =
    "Invalid value for ";
static const char g_mon_invalid_value_separator[] =
    ": ";
static const char g_mon_unknown_command_prefix[] =
    "Unknown command: ";
static const char g_mon_no_traces_text[] =
    "No traces registered.\n";
static const char g_mon_usage_help_text[] =
    "Usage: help\n";
static const char g_mon_usage_list_text[] =
    "Usage: list\n";
static const char g_mon_usage_get_text[] =
    "Usage: get <name>\n";
static const char g_mon_usage_set_text[] =
    "Usage: set <name> <value>\n";
static const char g_mon_usage_trace_text[] =
    "Usage: trace [on|off]\n";
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
    MON_VALUE_F64
#endif
} mon_value_type_t;

typedef enum mon_write_result {
    MON_WRITE_RESULT_OK = 0,
    MON_WRITE_RESULT_INVALID,
    MON_WRITE_RESULT_READ_ONLY
} mon_write_result_t;

typedef union mon_value_snapshot {
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    float f32;
    double f64;
} mon_value_snapshot_t;

typedef struct mon_name_view {
    const char *ptr;
    uint8_t length;
} mon_name_view_t;

typedef struct mon_trace_entry {
    void *source_ptr;
    const char *name_ptr;
    mon_value_snapshot_t last_value;
    uint8_t name_length;
    uint8_t type;
    uint8_t flags;
} mon_trace_entry_t;

typedef struct mon_state {
    char messages[MON_MAX_QUEUED_MESSAGES][MON_MAX_MESSAGE_LENGTH];
    char input_buffer[MON_MAX_INPUT_LENGTH];
    const char *welcome_message;
    uint32_t dropped_messages;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t input_length;
    uint8_t trace_count;
    uint8_t writable_trace_count;
    uint8_t flags;
    mon_trace_entry_t traces[MON_MAX_TRACES];
} mon_state_t;

typedef struct mon_builder {
    char data[MON_MAX_MESSAGE_LENGTH];
    size_t length;
} mon_builder_t;

#if MONITOR_ENABLE_FLOAT_SUPPORT
static size_t mon_bounded_strlen(const char *text, size_t max_len);
#endif

enum {
    MON_TRACE_FLAG_WRITABLE = 0x01u
};

enum {
    MON_STATE_FLAG_INPUT_OVERFLOW = 0x01u,
    MON_STATE_FLAG_TRACE_OUTPUT = 0x02u,
    MON_STATE_FLAG_OUTPUT_ACTIVE = 0x04u
};

static mon_state_t g_mon_state;

static void mon_state_clear(void)
{
    (void)memset(&g_mon_state, 0, sizeof(g_mon_state));
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

static const char *mon_current_welcome_message(void)
{
    if (g_mon_state.welcome_message != NULL) {
        return g_mon_state.welcome_message;
    }

    return g_mon_default_welcome;
}

static bool mon_default_trace_output_enabled(void)
{
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

static const char *mon_trace_output_state_text(bool enabled)
{
    if (enabled) {
        return g_mon_trace_output_on_text;
    }

    return g_mon_trace_output_off_text;
}

#if !MONITOR_ENABLE_FLOAT_SUPPORT
static void mon_finish_output_delivery(void);
static void mon_queue_message(const char *text);

static void mon_queue_float_support_disabled(void)
{
    mon_finish_output_delivery();
    mon_queue_message(g_mon_float_support_disabled);
}
#endif

static mon_name_view_t mon_normalize_identifier(const char *source)
{
    mon_name_view_t view = { "", 0u };
    const char *start = source;
    size_t length;

    if (start == NULL) {
        return view;
    }

    while ((*start != '\0') && (isspace((unsigned char)*start) != 0)) {
        start++;
    }

    if (*start == '&') {
        start++;
        while ((*start != '\0') && (isspace((unsigned char)*start) != 0)) {
            start++;
        }
    }

    length = strlen(start);

    while ((length > 0u) &&
           (isspace((unsigned char)start[length - 1u]) != 0)) {
        length--;
    }

    while ((length >= 2u) && (start[0] == '(') && (start[length - 1u] == ')')) {
        start++;
        length -= 2u;

        while ((length > 0u) && (isspace((unsigned char)*start) != 0)) {
            start++;
            length--;
        }

        while ((length > 0u) &&
               (isspace((unsigned char)start[length - 1u]) != 0)) {
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
    if ((entry == NULL) || (entry->name_length != name.length)) {
        return false;
    }

    if (name.length == 0u) {
        return true;
    }

    return memcmp(entry->name_ptr, name.ptr, name.length) == 0;
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

static mon_trace_entry_t *mon_find_trace_by_name(const char *name)
{
    mon_name_view_t view;
    size_t length;
    uint8_t index;

    if (name == NULL) {
        return NULL;
    }

    length = strlen(name);
    view.ptr = name;
    if ((length == 0u) || (length >= MON_MAX_NAME_LENGTH)) {
        return NULL;
    }
    view.length = (uint8_t)length;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        if (mon_name_equals_view(&g_mon_state.traces[index], view)) {
            return &g_mon_state.traces[index];
        }
    }

    return NULL;
}

static bool mon_name_conflicts(const mon_trace_entry_t *skip, mon_name_view_t name)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        const mon_trace_entry_t *entry = &g_mon_state.traces[index];

        if ((entry == skip) || !mon_name_equals_view(entry, name)) {
            continue;
        }

        return true;
    }

    return false;
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

static uint8_t mon_next_queue_index(uint8_t index)
{
    index++;

    if (index >= MON_MAX_QUEUED_MESSAGES) {
        index = 0u;
    }

    return index;
}

static bool mon_queue_message_direct_n(const char *text, size_t length)
{
    char *slot;

    if (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES) {
        return false;
    }

    if (length >= MON_MAX_MESSAGE_LENGTH) {
        length = MON_MAX_MESSAGE_LENGTH - 1u;
    }

    slot = g_mon_state.messages[g_mon_state.tail];
    (void)memcpy(slot, text, length);
    slot[length] = '\0';

    g_mon_state.tail = mon_next_queue_index(g_mon_state.tail);
    g_mon_state.count++;

    return true;
}

static void mon_append_n_to_buffer(char *buffer,
                                   size_t buffer_size,
                                   size_t *length,
                                   const char *text,
                                   size_t text_length)
{
    size_t available;

    if ((buffer == NULL) || (length == NULL) || (text == NULL) ||
        (buffer_size <= 1u) || (*length >= (buffer_size - 1u))) {
        return;
    }

    available = (buffer_size - 1u) - *length;
    if (text_length > available) {
        text_length = available;
    }

    (void)memcpy(&buffer[*length], text, text_length);
    *length += text_length;
}

static void mon_append_char_to_buffer(char *buffer,
                                      size_t buffer_size,
                                      size_t *length,
                                      char value)
{
    if ((buffer == NULL) || (length == NULL) || (buffer_size <= 1u) ||
        (*length >= (buffer_size - 1u))) {
        return;
    }

    buffer[*length] = value;
    (*length)++;
}

static void mon_append_u64_to_buffer(char *buffer,
                                     size_t buffer_size,
                                     size_t *length,
                                     uint64_t value)
{
    char digits[20];
    size_t count = 0u;
    size_t available;

    if ((buffer == NULL) || (length == NULL) || (buffer_size <= 1u) ||
        (*length >= (buffer_size - 1u))) {
        return;
    }

    do {
        digits[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count++;
    } while ((value != 0u) && (count < sizeof(digits)));

    available = (buffer_size - 1u) - *length;
    while ((count > 0u) && (available > 0u)) {
        count--;
        buffer[*length] = digits[count];
        (*length)++;
        available--;
    }
}

static void mon_flush_drop_notice_if_possible(void)
{
    char notice[MON_MAX_MESSAGE_LENGTH];
    size_t length = 0u;
    uint32_t dropped;

    if ((g_mon_state.dropped_messages == 0u) ||
        (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES)) {
        return;
    }

    dropped = g_mon_state.dropped_messages;
    g_mon_state.dropped_messages = 0u;

    if (length < sizeof(notice)) {
        static const char prefix[] = "[monitor] dropped ";

        mon_append_n_to_buffer(notice,
                               sizeof(notice),
                               &length,
                               prefix,
                               sizeof(prefix) - 1u);
    }

    mon_append_u64_to_buffer(notice, sizeof(notice), &length, dropped);

    mon_append_char_to_buffer(notice, sizeof(notice), &length, ' ');

    if (length < sizeof(notice)) {
        static const char suffix[] = "message(s)\n";

        mon_append_n_to_buffer(notice,
                               sizeof(notice),
                               &length,
                               suffix,
                               sizeof(suffix) - 1u);
    }

    if (!mon_queue_message_direct_n(notice, length)) {
        g_mon_state.dropped_messages += dropped;
    }
}

static void mon_queue_text_n(const char *text, size_t length)
{
    size_t remaining = length;
    const char *cursor = text;

    while (remaining > 0u) {
        size_t chunk_length = remaining;

        if (chunk_length >= MON_MAX_MESSAGE_LENGTH) {
            chunk_length = MON_MAX_MESSAGE_LENGTH - 1u;
        }

        mon_flush_drop_notice_if_possible();

        if (!mon_queue_message_direct_n(cursor, chunk_length)) {
            g_mon_state.dropped_messages++;
        }

        cursor += chunk_length;
        remaining -= chunk_length;
    }
}

static void mon_queue_message(const char *text)
{
    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }

    mon_queue_text_n(text, strlen(text));
}

static void mon_queue_text(const char *text)
{
    if (text == NULL) {
        return;
    }

    mon_queue_message(text);
}

static void mon_builder_init(mon_builder_t *builder)
{
    builder->length = 0u;
}

static void mon_builder_flush(mon_builder_t *builder)
{
    if (builder->length == 0u) {
        return;
    }

    mon_flush_drop_notice_if_possible();

    if (!mon_queue_message_direct_n(builder->data, builder->length)) {
        g_mon_state.dropped_messages++;
    }

    builder->length = 0u;
}

static void mon_builder_append_n(mon_builder_t *builder,
                                 const char *text,
                                 size_t length)
{
    size_t remaining = length;
    const char *cursor = text;

    while (remaining > 0u) {
        const size_t available =
            (MON_MAX_MESSAGE_LENGTH - 1u) - builder->length;
        size_t chunk_length = remaining;

        if (available == 0u) {
            mon_builder_flush(builder);
            continue;
        }

        if (chunk_length > available) {
            chunk_length = available;
        }

        (void)memcpy(&builder->data[builder->length], cursor, chunk_length);
        builder->length += chunk_length;
        cursor += chunk_length;
        remaining -= chunk_length;
    }
}

static void mon_builder_append_text(mon_builder_t *builder, const char *text)
{
    if (text == NULL) {
        return;
    }

    mon_builder_append_n(builder, text, strlen(text));
}

static void mon_builder_append_char(mon_builder_t *builder, char value)
{
    mon_builder_append_n(builder, &value, 1u);
}

static void mon_builder_append_name(mon_builder_t *builder,
                                    const mon_trace_entry_t *entry)
{
    mon_builder_append_n(builder, entry->name_ptr, entry->name_length);
}

static void mon_builder_append_u64(mon_builder_t *builder, uint64_t value)
{
    char digits[20];
    size_t count = 0u;

    do {
        digits[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count++;
    } while ((value != 0u) && (count < sizeof(digits)));

    while (count > 0u) {
        count--;
        mon_builder_append_char(builder, digits[count]);
    }
}

static void mon_builder_append_i64(mon_builder_t *builder, int64_t value)
{
    uint64_t magnitude;

    if (value < 0) {
        mon_builder_append_char(builder, '-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }

    mon_builder_append_u64(builder, magnitude);
}

static void mon_builder_finish_line(mon_builder_t *builder)
{
    mon_builder_append_char(builder, '\n');
    mon_builder_flush(builder);
}

static mon_value_type_t mon_entry_type(const mon_trace_entry_t *entry)
{
    return (mon_value_type_t)entry->type;
}

static const char *mon_type_name(mon_value_type_t type)
{
    switch (type) {
    case MON_VALUE_U8:
        return "u8";
    case MON_VALUE_I8:
        return "i8";
    case MON_VALUE_U16:
        return "u16";
    case MON_VALUE_I16:
        return "i16";
    case MON_VALUE_U32:
        return "u32";
    case MON_VALUE_I32:
        return "i32";
    case MON_VALUE_U64:
        return "u64";
    case MON_VALUE_I64:
        return "i64";
#if MONITOR_ENABLE_FLOAT_SUPPORT
    case MON_VALUE_F32:
        return "f32";
    case MON_VALUE_F64:
        return "f64";
#endif
    default:
        return "?";
    }
}

static void mon_read_value(const mon_trace_entry_t *entry,
                           mon_value_snapshot_t *snapshot)
{
    snapshot->u64 = 0u;

    if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
        *snapshot = entry->last_value;
        return;
    }

    switch (mon_entry_type(entry)) {
    case MON_VALUE_U8:
        snapshot->u8 = *(const uint8_t *)entry->source_ptr;
        break;
    case MON_VALUE_I8:
        snapshot->i8 = *(const int8_t *)entry->source_ptr;
        break;
    case MON_VALUE_U16:
        snapshot->u16 = *(const uint16_t *)entry->source_ptr;
        break;
    case MON_VALUE_I16:
        snapshot->i16 = *(const int16_t *)entry->source_ptr;
        break;
    case MON_VALUE_U32:
        snapshot->u32 = *(const uint32_t *)entry->source_ptr;
        break;
    case MON_VALUE_I32:
        snapshot->i32 = *(const int32_t *)entry->source_ptr;
        break;
    case MON_VALUE_U64:
        snapshot->u64 = *(const uint64_t *)entry->source_ptr;
        break;
    case MON_VALUE_I64:
        snapshot->i64 = *(const int64_t *)entry->source_ptr;
        break;
#if MONITOR_ENABLE_FLOAT_SUPPORT
    case MON_VALUE_F32:
        snapshot->f32 = *(const float *)entry->source_ptr;
        break;
    case MON_VALUE_F64:
        snapshot->f64 = *(const double *)entry->source_ptr;
        break;
#endif
    default:
        break;
    }
}

static bool mon_snapshot_equal(const mon_value_snapshot_t *lhs,
                               const mon_value_snapshot_t *rhs)
{
    return memcmp(lhs, rhs, sizeof(*lhs)) == 0;
}

static void mon_builder_append_snapshot(mon_builder_t *builder,
                                        mon_value_type_t type,
                                        const mon_value_snapshot_t *value)
{
    switch (type) {
    case MON_VALUE_U8:
        mon_builder_append_u64(builder, value->u8);
        return;
    case MON_VALUE_I8:
        mon_builder_append_i64(builder, value->i8);
        return;
    case MON_VALUE_U16:
        mon_builder_append_u64(builder, value->u16);
        return;
    case MON_VALUE_I16:
        mon_builder_append_i64(builder, value->i16);
        return;
    case MON_VALUE_U32:
        mon_builder_append_u64(builder, value->u32);
        return;
    case MON_VALUE_I32:
        mon_builder_append_i64(builder, value->i32);
        return;
    case MON_VALUE_U64:
        mon_builder_append_u64(builder, value->u64);
        return;
    case MON_VALUE_I64:
        mon_builder_append_i64(builder, value->i64);
        return;
#if MONITOR_ENABLE_FLOAT_SUPPORT
    case MON_VALUE_F32: {
        char buffer[32];
        const int written =
            snprintf(buffer, sizeof(buffer), "%.9g", (double)value->f32);

        if (written < 0) {
            mon_builder_append_char(builder, '?');
            return;
        }

        mon_builder_append_n(builder,
                             buffer,
                             mon_bounded_strlen(buffer, sizeof(buffer) - 1u));
        return;
    }
    case MON_VALUE_F64: {
        char buffer[32];
        const int written =
            snprintf(buffer, sizeof(buffer), "%.17g", value->f64);

        if (written < 0) {
            mon_builder_append_char(builder, '?');
            return;
        }

        mon_builder_append_n(builder,
                             buffer,
                             mon_bounded_strlen(buffer, sizeof(buffer) - 1u));
        return;
    }
#endif
    default:
        mon_builder_append_char(builder, '?');
        return;
    }
}

static const char *mon_dequeue_message(void)
{
    mon_flush_drop_notice_if_possible();

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

static char *mon_next_token(char **cursor)
{
    char *start = *cursor;

    while ((*start != '\0') && (isspace((unsigned char)*start) != 0)) {
        start++;
    }

    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    *cursor = start;
    while ((**cursor != '\0') && (isspace((unsigned char)**cursor) == 0)) {
        (*cursor)++;
    }

    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }

    return start;
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

static bool mon_parse_unsigned_digits(const char *text,
                                      uint64_t maximum,
                                      uint64_t *value)
{
    const char *cursor = text;
    uint64_t parsed = 0u;
    uint64_t base = 10u;

    if ((cursor == NULL) || (value == NULL) || (*cursor == '\0')) {
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

static bool mon_parse_unsigned(const char *text,
                               uint64_t maximum,
                               uint64_t *value)
{
    const char *cursor = text;

    if ((cursor == NULL) || (value == NULL)) {
        return false;
    }

    if (*cursor == '+') {
        cursor++;
    } else if (*cursor == '-') {
        return false;
    }

    return mon_parse_unsigned_digits(cursor, maximum, value);
}

static bool mon_parse_signed(const char *text,
                             int64_t minimum,
                             int64_t maximum,
                             int64_t *value)
{
    const char *cursor = text;
    uint64_t magnitude;
    bool negative = false;

    if ((cursor == NULL) || (value == NULL)) {
        return false;
    }

    if (*cursor == '+') {
        cursor++;
    } else if (*cursor == '-') {
        negative = true;
        cursor++;
    }

    if (!negative) {
        if (!mon_parse_unsigned_digits(cursor, (uint64_t)maximum, &magnitude)) {
            return false;
        }

        *value = (int64_t)magnitude;
        return true;
    }

    if (!mon_parse_unsigned_digits(cursor,
                                   (uint64_t)(-(minimum + 1)) + 1u,
                                   &magnitude)) {
        return false;
    }

    if (magnitude == ((uint64_t)(-(minimum + 1)) + 1u)) {
        *value = minimum;
        return true;
    }

    *value = -(int64_t)magnitude;
    return true;
}

#if MONITOR_ENABLE_FLOAT_SUPPORT
static size_t mon_bounded_strlen(const char *text, size_t max_len)
{
    size_t length = 0u;

    while ((length < max_len) && (text[length] != '\0')) {
        length++;
    }

    return length;
}

static bool mon_parse_float64(const char *text, double *value)
{
    char *end = NULL;
    double parsed_value;

    if ((text == NULL) || (value == NULL)) {
        return false;
    }

    errno = 0;
    parsed_value = strtod(text, &end);

    if ((text == end) || (end == NULL) || (*end != '\0') || (errno == ERANGE)) {
        return false;
    }

    *value = parsed_value;
    return true;
}
#endif

static mon_write_result_t mon_write_value(mon_trace_entry_t *entry,
                                          const char *text)
{
    uint64_t unsigned_value;
    int64_t signed_value;
#if MONITOR_ENABLE_FLOAT_SUPPORT
    double float_value;
#endif
    mon_value_type_t type;

    if ((entry == NULL) || (text == NULL)) {
        return MON_WRITE_RESULT_INVALID;
    }

    if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
        return MON_WRITE_RESULT_READ_ONLY;
    }

    type = mon_entry_type(entry);

    switch (type) {
    case MON_VALUE_U8:
        if (!mon_parse_unsigned(text, UINT8_MAX, &unsigned_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(uint8_t *)entry->source_ptr = (uint8_t)unsigned_value;
        break;
    case MON_VALUE_I8:
        if (!mon_parse_signed(text, INT8_MIN, INT8_MAX, &signed_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(int8_t *)entry->source_ptr = (int8_t)signed_value;
        break;
    case MON_VALUE_U16:
        if (!mon_parse_unsigned(text, UINT16_MAX, &unsigned_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(uint16_t *)entry->source_ptr = (uint16_t)unsigned_value;
        break;
    case MON_VALUE_I16:
        if (!mon_parse_signed(text, INT16_MIN, INT16_MAX, &signed_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(int16_t *)entry->source_ptr = (int16_t)signed_value;
        break;
    case MON_VALUE_U32:
        if (!mon_parse_unsigned(text, UINT32_MAX, &unsigned_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(uint32_t *)entry->source_ptr = (uint32_t)unsigned_value;
        break;
    case MON_VALUE_I32:
        if (!mon_parse_signed(text, INT32_MIN, INT32_MAX, &signed_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(int32_t *)entry->source_ptr = (int32_t)signed_value;
        break;
    case MON_VALUE_U64:
        if (!mon_parse_unsigned(text, UINT64_MAX, &unsigned_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(uint64_t *)entry->source_ptr = (uint64_t)unsigned_value;
        break;
    case MON_VALUE_I64:
        if (!mon_parse_signed(text, INT64_MIN, INT64_MAX, &signed_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(int64_t *)entry->source_ptr = (int64_t)signed_value;
        break;
#if MONITOR_ENABLE_FLOAT_SUPPORT
    case MON_VALUE_F32:
        if (!mon_parse_float64(text, &float_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        if (isfinite(float_value) &&
            ((float_value > FLT_MAX) || (float_value < -FLT_MAX))) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(float *)entry->source_ptr = (float)float_value;
        break;
    case MON_VALUE_F64:
        if (!mon_parse_float64(text, &float_value)) {
            return MON_WRITE_RESULT_INVALID;
        }
        *(double *)entry->source_ptr = float_value;
        break;
#endif
    default:
        return MON_WRITE_RESULT_INVALID;
    }

    mon_read_value(entry, &entry->last_value);
    return MON_WRITE_RESULT_OK;
}

static void mon_queue_named_line(const char *prefix,
                                 const char *name,
                                 const char *suffix)
{
    mon_builder_t builder;

    mon_builder_init(&builder);
    mon_builder_append_text(&builder, prefix);
    mon_builder_append_text(&builder, name);
    mon_builder_append_text(&builder, suffix);
    mon_builder_finish_line(&builder);
}

static void mon_queue_entry_value(const mon_trace_entry_t *entry,
                                  const char *prefix)
{
    mon_builder_t builder;
    mon_value_snapshot_t current_value;
    const mon_value_type_t type = mon_entry_type(entry);

    mon_read_value(entry, &current_value);

    mon_builder_init(&builder);
    mon_builder_append_text(&builder, prefix);
    mon_builder_append_name(&builder, entry);
    mon_builder_append_text(&builder, " (");
    mon_builder_append_text(&builder, mon_type_name(type));
    mon_builder_append_text(&builder, ") = ");
    mon_builder_append_snapshot(&builder, type, &current_value);
    mon_builder_finish_line(&builder);
}

static void mon_sync_writable_traces(void)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];

        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
            continue;
        }

        mon_read_value(entry, &entry->last_value);
    }
}

static void mon_check_writable_traces(void)
{
    uint8_t index;

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];
        mon_value_snapshot_t current_value;

        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) == 0u) {
            continue;
        }

        mon_read_value(entry, &current_value);

        if (!mon_snapshot_equal(&entry->last_value, &current_value)) {
            entry->last_value = current_value;
            mon_queue_entry_value(entry, "trace ");
        }
    }
}

static void mon_handle_help_command(void)
{
    mon_queue_text(mon_current_welcome_message());
    mon_queue_message(g_mon_help_text);
}

static void mon_handle_list_command(void)
{
    uint8_t index;

    if (g_mon_state.trace_count == 0u) {
        mon_queue_message(g_mon_no_traces_text);
        return;
    }

    for (index = 0u; index < g_mon_state.trace_count; index++) {
        mon_queue_entry_value(&g_mon_state.traces[index], "");
    }
}

static void mon_handle_get_command(const char *name)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);

    if (entry == NULL) {
        mon_queue_named_line(g_mon_unknown_variable_prefix, name, "");
        return;
    }

    mon_queue_entry_value(entry, "");
}

static void mon_handle_set_command(const char *name, const char *value)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);
    const mon_write_result_t result = mon_write_value(entry, value);

    if (entry == NULL) {
        mon_queue_named_line(g_mon_unknown_variable_prefix, name, "");
        return;
    }

    if (result == MON_WRITE_RESULT_READ_ONLY) {
        mon_queue_named_line(g_mon_read_only_prefix, name, "");
        return;
    }

    if (result != MON_WRITE_RESULT_OK) {
        mon_builder_t builder;

        mon_builder_init(&builder);
        mon_builder_append_text(&builder, g_mon_invalid_value_prefix);
        mon_builder_append_text(&builder, name);
        mon_builder_append_text(&builder, g_mon_invalid_value_separator);
        mon_builder_append_text(&builder, value);
        mon_builder_finish_line(&builder);
        return;
    }

    mon_queue_entry_value(entry, "");
}

static void mon_handle_trace_command(const char *argument)
{
    if (argument == NULL) {
        mon_queue_message(mon_trace_output_state_text(
            mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)));
        return;
    }

    if (strcmp(argument, "on") == 0) {
        if (!mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)) {
            mon_sync_writable_traces();
            mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT, true);
        }

        mon_queue_message(mon_trace_output_state_text(
            mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)));
        return;
    }

    if (strcmp(argument, "off") == 0) {
        mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT, false);
        mon_queue_message(mon_trace_output_state_text(false));
        return;
    }

    mon_queue_message(g_mon_usage_trace_text);
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
            mon_queue_message(g_mon_usage_help_text);
            return;
        }

        mon_handle_help_command();
        return;
    }

    if (strcmp(command, "list") == 0) {
        if (mon_next_token(&cursor) != NULL) {
            mon_queue_message(g_mon_usage_list_text);
            return;
        }

        mon_handle_list_command();
        return;
    }

    if (strcmp(command, "get") == 0) {
        argument1 = mon_next_token(&cursor);
        if ((argument1 == NULL) || (mon_next_token(&cursor) != NULL)) {
            mon_queue_message(g_mon_usage_get_text);
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
            mon_queue_message(g_mon_usage_set_text);
            return;
        }

        mon_handle_set_command(argument1, argument2);
        return;
    }

    if (strcmp(command, "trace") == 0) {
        argument1 = mon_next_token(&cursor);

        if (mon_next_token(&cursor) != NULL) {
            mon_queue_message(g_mon_usage_trace_text);
            return;
        }

        mon_handle_trace_command(argument1);
        return;
    }

    mon_queue_named_line(g_mon_unknown_command_prefix, command, "");
    mon_handle_help_command();
}

static void mon_process_input(const char *input)
{
    const unsigned char *cursor =
        (const unsigned char *)(const void *)input;

    while (*cursor != '\0') {
        const unsigned char ch = *cursor;

        if ((ch == '\r') || (ch == '\n')) {
            if (mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW)) {
                mon_queue_message(g_mon_input_too_long_text);
            } else if (g_mon_state.input_length > 0u) {
                g_mon_state.input_buffer[g_mon_state.input_length] = '\0';
                mon_process_line(g_mon_state.input_buffer);
            }

            g_mon_state.input_length = 0u;
            mon_state_set_flag(MON_STATE_FLAG_INPUT_OVERFLOW, false);
            cursor++;
            continue;
        }

        if ((ch == '\b') || (ch == 0x7FU)) {
            if (!mon_state_flag_is_set(MON_STATE_FLAG_INPUT_OVERFLOW) &&
                (g_mon_state.input_length > 0u)) {
                g_mon_state.input_length--;
            }

            cursor++;
            continue;
        }

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
        cursor++;
    }
}

static void mon_register_pointer_trace(void *ptr,
                                       mon_value_type_t type,
                                       const char *human_identifier)
{
    mon_name_view_t name;
    mon_trace_entry_t *entry;
    mon_value_type_t previous_type;

    mon_finish_output_delivery();

    if (ptr == NULL) {
        mon_queue_message(g_mon_null_pointer_text);
        return;
    }

    name = mon_normalize_identifier(human_identifier);
    if (name.length == 0u) {
        mon_queue_message(g_mon_empty_name_text);
        return;
    }

    entry = mon_find_trace_by_pointer(ptr);
    if (entry != NULL) {
        previous_type = mon_entry_type(entry);

        if (mon_name_conflicts(entry, name)) {
            mon_builder_t builder;

            mon_builder_init(&builder);
            mon_builder_append_text(&builder, g_mon_duplicate_name_prefix);
            mon_builder_append_n(&builder, name.ptr, name.length);
            mon_builder_finish_line(&builder);
            return;
        }

        entry->source_ptr = ptr;
        entry->name_ptr = name.ptr;
        entry->name_length = name.length;
        entry->type = (uint8_t)type;
        entry->flags = MON_TRACE_FLAG_WRITABLE;

        if (previous_type != type) {
            mon_read_value(entry, &entry->last_value);
        }
        return;
    }

    if (g_mon_state.trace_count >= MON_MAX_TRACES) {
        mon_queue_message(g_mon_trace_registry_full_text);
        return;
    }

    if (mon_name_conflicts(NULL, name)) {
        mon_builder_t builder;

        mon_builder_init(&builder);
        mon_builder_append_text(&builder, g_mon_duplicate_name_prefix);
        mon_builder_append_n(&builder, name.ptr, name.length);
        mon_builder_finish_line(&builder);
        return;
    }

    entry = &g_mon_state.traces[g_mon_state.trace_count];
    entry->source_ptr = ptr;
    entry->name_ptr = name.ptr;
    entry->name_length = name.length;
    entry->type = (uint8_t)type;
    entry->flags = MON_TRACE_FLAG_WRITABLE;
    mon_read_value(entry, &entry->last_value);
    g_mon_state.trace_count++;
    g_mon_state.writable_trace_count++;
}

static void mon_register_value_trace(const mon_value_snapshot_t *value,
                                     mon_value_type_t type,
                                     const char *human_identifier)
{
    mon_name_view_t name;
    mon_trace_entry_t *entry;
    mon_value_type_t previous_type;

    mon_finish_output_delivery();

    name = mon_normalize_identifier(human_identifier);
    if (name.length == 0u) {
        mon_queue_message(g_mon_empty_name_text);
        return;
    }

    entry = mon_find_trace_by_view(name);
    if (entry != NULL) {
        previous_type = mon_entry_type(entry);

        if ((entry->flags & MON_TRACE_FLAG_WRITABLE) != 0u) {
            mon_builder_t builder;

            mon_builder_init(&builder);
            mon_builder_append_text(&builder, g_mon_duplicate_name_prefix);
            mon_builder_append_n(&builder, name.ptr, name.length);
            mon_builder_finish_line(&builder);
            return;
        }

        entry->name_ptr = name.ptr;
        entry->name_length = name.length;
        entry->type = (uint8_t)type;

        if ((previous_type != type) || !mon_snapshot_equal(&entry->last_value, value)) {
            entry->last_value = *value;
            if ((previous_type == type) &&
                mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT)) {
                mon_queue_entry_value(entry, "trace ");
            }
        }

        return;
    }

    if (g_mon_state.trace_count >= MON_MAX_TRACES) {
        mon_queue_message(g_mon_trace_registry_full_text);
        return;
    }

    if (mon_name_conflicts(NULL, name)) {
        mon_builder_t builder;

        mon_builder_init(&builder);
        mon_builder_append_text(&builder, g_mon_duplicate_name_prefix);
        mon_builder_append_n(&builder, name.ptr, name.length);
        mon_builder_finish_line(&builder);
        return;
    }

    entry = &g_mon_state.traces[g_mon_state.trace_count];
    entry->source_ptr = NULL;
    entry->name_ptr = name.ptr;
    entry->name_length = name.length;
    entry->type = (uint8_t)type;
    entry->flags = 0u;
    entry->last_value = *value;
    g_mon_state.trace_count++;
}

void mon_reset(const char *welcome_message)
{
    mon_state_clear();
    mon_state_set_flag(MON_STATE_FLAG_TRACE_OUTPUT,
                       mon_default_trace_output_enabled());
    g_mon_state.welcome_message =
        (welcome_message != NULL) ? welcome_message : g_mon_default_welcome;
    mon_queue_text(mon_current_welcome_message());
}

const char *mon_task(const char *input, int device_ready)
{
    mon_finish_output_delivery();

    if (mon_state_flag_is_set(MON_STATE_FLAG_TRACE_OUTPUT) &&
        (g_mon_state.writable_trace_count > 0u)) {
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
    char buffer[MON_MAX_FORMAT_LENGTH];
    int written;
    va_list args;

    mon_finish_output_delivery();

    if (fmt == NULL) {
        return NULL;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        mon_queue_message(g_mon_format_failure_text);
        return NULL;
    }

    mon_queue_text(buffer);

    if ((size_t)written >= sizeof(buffer)) {
        mon_queue_message(g_mon_format_truncated_text);
    }

    return NULL;
}

#define MON_DEFINE_TRACE_FUNCTION(function_name, value_type, enum_type)       \
    void function_name(value_type *value, const char *human_identifier)       \
    {                                                                         \
        mon_register_pointer_trace((void *)value, enum_type,                  \
                                   human_identifier);                         \
    }

#define MON_DEFINE_VALUE_TRACE_FUNCTION(function_name, member_name,            \
                                        value_type, enum_type)                \
    void function_name(value_type value, const char *human_identifier)        \
    {                                                                         \
        mon_value_snapshot_t snapshot;                                        \
        snapshot.u64 = 0u;                                                    \
        snapshot.member_name = value;                                         \
        mon_register_value_trace(&snapshot, enum_type, human_identifier);     \
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

MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u8_value, u8, uint8_t, MON_VALUE_U8)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i8_value, i8, int8_t, MON_VALUE_I8)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u16_value, u16, uint16_t, MON_VALUE_U16)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i16_value, i16, int16_t, MON_VALUE_I16)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u32_value, u32, uint32_t, MON_VALUE_U32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i32_value, i32, int32_t, MON_VALUE_I32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_u64_value, u64, uint64_t, MON_VALUE_U64)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_i64_value, i64, int64_t, MON_VALUE_I64)
#if MONITOR_ENABLE_FLOAT_SUPPORT
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_f32_value, f32, float, MON_VALUE_F32)
MON_DEFINE_VALUE_TRACE_FUNCTION(mon_trace_f64_value, f64, double, MON_VALUE_F64)
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
