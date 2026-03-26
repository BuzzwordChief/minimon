#include "monitor.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MON_MAX_TRACES > 0u, "MON_MAX_TRACES must be greater than zero");
_Static_assert(MON_MAX_NAME_LENGTH > 1u,
               "MON_MAX_NAME_LENGTH must be greater than one");
_Static_assert(MON_MAX_INPUT_LENGTH > 1u,
               "MON_MAX_INPUT_LENGTH must be greater than one");
_Static_assert(MON_MAX_MESSAGE_LENGTH > 1u,
               "MON_MAX_MESSAGE_LENGTH must be greater than one");
_Static_assert(MON_MAX_QUEUED_MESSAGES > 0u,
               "MON_MAX_QUEUED_MESSAGES must be greater than zero");
_Static_assert(MON_MAX_FORMAT_LENGTH > 1u,
               "MON_MAX_FORMAT_LENGTH must be greater than one");

static const char g_mon_default_welcome[] =
    "minimon ready. Type 'help' for commands.\n";

typedef enum mon_value_type {
    MON_VALUE_U8 = 0,
    MON_VALUE_I8,
    MON_VALUE_U16,
    MON_VALUE_I16,
    MON_VALUE_U32,
    MON_VALUE_I32,
    MON_VALUE_U64,
    MON_VALUE_I64,
    MON_VALUE_F32,
    MON_VALUE_F64
} mon_value_type_t;

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

typedef struct mon_trace_entry {
    bool in_use;
    void *ptr;
    mon_value_type_t type;
    char name[MON_MAX_NAME_LENGTH];
    mon_value_snapshot_t last_value;
} mon_trace_entry_t;

typedef struct mon_state {
    char messages[MON_MAX_QUEUED_MESSAGES][MON_MAX_MESSAGE_LENGTH];
    size_t head;
    size_t tail;
    size_t count;
    size_t dropped_messages;
    char current_output[MON_MAX_MESSAGE_LENGTH];
    char input_buffer[MON_MAX_INPUT_LENGTH];
    size_t input_length;
    bool input_overflow;
    mon_trace_entry_t traces[MON_MAX_TRACES];
} mon_state_t;

static mon_state_t g_mon_state;

static size_t mon_bounded_strlen(const char *text, size_t max_len)
{
    size_t length = 0u;

    while ((length < max_len) && (text[length] != '\0')) {
        length++;
    }

    return length;
}

static void mon_state_clear(void)
{
    (void)memset(&g_mon_state, 0, sizeof(g_mon_state));
}

static size_t mon_type_size(mon_value_type_t type)
{
    switch (type) {
    case MON_VALUE_U8:
        return sizeof(uint8_t);
    case MON_VALUE_I8:
        return sizeof(int8_t);
    case MON_VALUE_U16:
        return sizeof(uint16_t);
    case MON_VALUE_I16:
        return sizeof(int16_t);
    case MON_VALUE_U32:
        return sizeof(uint32_t);
    case MON_VALUE_I32:
        return sizeof(int32_t);
    case MON_VALUE_U64:
        return sizeof(uint64_t);
    case MON_VALUE_I64:
        return sizeof(int64_t);
    case MON_VALUE_F32:
        return sizeof(float);
    case MON_VALUE_F64:
        return sizeof(double);
    default:
        return 0u;
    }
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
    case MON_VALUE_F32:
        return "f32";
    case MON_VALUE_F64:
        return "f64";
    default:
        return "unknown";
    }
}

static void mon_read_value(const mon_trace_entry_t *entry,
                           mon_value_snapshot_t *snapshot)
{
    switch (entry->type) {
    case MON_VALUE_U8:
        snapshot->u8 = *(const uint8_t *)entry->ptr;
        break;
    case MON_VALUE_I8:
        snapshot->i8 = *(const int8_t *)entry->ptr;
        break;
    case MON_VALUE_U16:
        snapshot->u16 = *(const uint16_t *)entry->ptr;
        break;
    case MON_VALUE_I16:
        snapshot->i16 = *(const int16_t *)entry->ptr;
        break;
    case MON_VALUE_U32:
        snapshot->u32 = *(const uint32_t *)entry->ptr;
        break;
    case MON_VALUE_I32:
        snapshot->i32 = *(const int32_t *)entry->ptr;
        break;
    case MON_VALUE_U64:
        snapshot->u64 = *(const uint64_t *)entry->ptr;
        break;
    case MON_VALUE_I64:
        snapshot->i64 = *(const int64_t *)entry->ptr;
        break;
    case MON_VALUE_F32:
        snapshot->f32 = *(const float *)entry->ptr;
        break;
    case MON_VALUE_F64:
        snapshot->f64 = *(const double *)entry->ptr;
        break;
    default:
        break;
    }
}

static bool mon_snapshot_equal(mon_value_type_t type,
                               const mon_value_snapshot_t *lhs,
                               const mon_value_snapshot_t *rhs)
{
    const size_t size = mon_type_size(type);

    return (size > 0u) && (memcmp(lhs, rhs, size) == 0);
}

static int mon_format_snapshot(mon_value_type_t type,
                               const mon_value_snapshot_t *value,
                               char *buffer,
                               size_t buffer_size)
{
    switch (type) {
    case MON_VALUE_U8:
        return snprintf(buffer, buffer_size, "%" PRIu8, value->u8);
    case MON_VALUE_I8:
        return snprintf(buffer, buffer_size, "%" PRId8, value->i8);
    case MON_VALUE_U16:
        return snprintf(buffer, buffer_size, "%" PRIu16, value->u16);
    case MON_VALUE_I16:
        return snprintf(buffer, buffer_size, "%" PRId16, value->i16);
    case MON_VALUE_U32:
        return snprintf(buffer, buffer_size, "%" PRIu32, value->u32);
    case MON_VALUE_I32:
        return snprintf(buffer, buffer_size, "%" PRId32, value->i32);
    case MON_VALUE_U64:
        return snprintf(buffer, buffer_size, "%" PRIu64, value->u64);
    case MON_VALUE_I64:
        return snprintf(buffer, buffer_size, "%" PRId64, value->i64);
    case MON_VALUE_F32:
        return snprintf(buffer, buffer_size, "%.9g", (double)value->f32);
    case MON_VALUE_F64:
        return snprintf(buffer, buffer_size, "%.17g", value->f64);
    default:
        return snprintf(buffer, buffer_size, "?");
    }
}

static void mon_copy_identifier(char *destination,
                                size_t destination_size,
                                const char *source,
                                size_t fallback_index)
{
    const char *start = source;
    size_t length;
    size_t copy_length;

    if (destination_size == 0u) {
        return;
    }

    if (start == NULL) {
        start = "";
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

    if (length == 0u) {
        (void)snprintf(destination,
                       destination_size,
                       "trace_%zu",
                       fallback_index + 1u);
        return;
    }

    copy_length = length;
    if (copy_length >= destination_size) {
        copy_length = destination_size - 1u;
    }

    (void)memcpy(destination, start, copy_length);
    destination[copy_length] = '\0';
}

static mon_trace_entry_t *mon_find_trace_by_pointer(const void *ptr)
{
    size_t index;

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        if (g_mon_state.traces[index].in_use &&
            (g_mon_state.traces[index].ptr == ptr)) {
            return &g_mon_state.traces[index];
        }
    }

    return NULL;
}

static mon_trace_entry_t *mon_find_trace_by_name(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        if (g_mon_state.traces[index].in_use &&
            (strcmp(g_mon_state.traces[index].name, name) == 0)) {
            return &g_mon_state.traces[index];
        }
    }

    return NULL;
}

static bool mon_name_conflicts(const mon_trace_entry_t *skip, const char *name)
{
    size_t index;

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        const mon_trace_entry_t *entry = &g_mon_state.traces[index];

        if (!entry->in_use || (entry == skip)) {
            continue;
        }

        if (strcmp(entry->name, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool mon_queue_message_direct(const char *text)
{
    char *slot;
    size_t length;

    if (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES) {
        return false;
    }

    slot = g_mon_state.messages[g_mon_state.tail];
    length = mon_bounded_strlen(text, MON_MAX_MESSAGE_LENGTH - 1u);

    (void)memcpy(slot, text, length);
    slot[length] = '\0';

    g_mon_state.tail = (g_mon_state.tail + 1u) % MON_MAX_QUEUED_MESSAGES;
    g_mon_state.count++;

    return true;
}

static void mon_flush_drop_notice_if_possible(void)
{
    char notice[MON_MAX_MESSAGE_LENGTH];
    size_t dropped;

    if ((g_mon_state.dropped_messages == 0u) ||
        (g_mon_state.count >= MON_MAX_QUEUED_MESSAGES)) {
        return;
    }

    dropped = g_mon_state.dropped_messages;
    g_mon_state.dropped_messages = 0u;

    (void)snprintf(notice,
                   sizeof(notice),
                   "[monitor] dropped %zu message(s)\n",
                   dropped);

    if (!mon_queue_message_direct(notice)) {
        g_mon_state.dropped_messages += dropped;
    }
}

static void mon_queue_message(const char *text)
{
    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }

    mon_flush_drop_notice_if_possible();

    if (!mon_queue_message_direct(text)) {
        g_mon_state.dropped_messages++;
    }
}

static void mon_queue_text(const char *text)
{
    char chunk[MON_MAX_MESSAGE_LENGTH];
    const char *cursor = text;

    if (cursor == NULL) {
        return;
    }

    while (*cursor != '\0') {
        size_t chunk_length = 0u;

        while ((cursor[chunk_length] != '\0') &&
               (chunk_length < (MON_MAX_MESSAGE_LENGTH - 1u))) {
            chunk_length++;
        }

        (void)memcpy(chunk, cursor, chunk_length);
        chunk[chunk_length] = '\0';
        mon_queue_message(chunk);

        cursor += chunk_length;
    }
}

static void mon_queue_textf(const char *fmt, ...)
{
    char buffer[MON_MAX_FORMAT_LENGTH];
    int written;
    va_list args;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        mon_queue_message("[monitor] formatting failure\n");
        return;
    }

    mon_queue_text(buffer);

    if ((size_t)written >= sizeof(buffer)) {
        mon_queue_message("[monitor] formatted output truncated\n");
    }
}

static const char *mon_dequeue_message(void)
{
    size_t length;

    mon_flush_drop_notice_if_possible();

    if (g_mon_state.count == 0u) {
        return NULL;
    }

    length = mon_bounded_strlen(g_mon_state.messages[g_mon_state.head],
                                MON_MAX_MESSAGE_LENGTH - 1u);
    (void)memcpy(g_mon_state.current_output,
                 g_mon_state.messages[g_mon_state.head],
                 length);
    g_mon_state.current_output[length] = '\0';

    g_mon_state.head = (g_mon_state.head + 1u) % MON_MAX_QUEUED_MESSAGES;
    g_mon_state.count--;

    mon_flush_drop_notice_if_possible();

    return g_mon_state.current_output;
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

static bool mon_parse_unsigned(const char *text,
                               uint64_t maximum,
                               uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed_value;

    if ((text == NULL) || (value == NULL) || (text[0] == '-')) {
        return false;
    }

    errno = 0;
    parsed_value = strtoull(text, &end, 0);

    if ((text == end) || (end == NULL) || (*end != '\0') || (errno == ERANGE)) {
        return false;
    }

    if ((uint64_t)parsed_value > maximum) {
        return false;
    }

    *value = (uint64_t)parsed_value;
    return true;
}

static bool mon_parse_signed(const char *text,
                             int64_t minimum,
                             int64_t maximum,
                             int64_t *value)
{
    char *end = NULL;
    long long parsed_value;

    if ((text == NULL) || (value == NULL)) {
        return false;
    }

    errno = 0;
    parsed_value = strtoll(text, &end, 0);

    if ((text == end) || (end == NULL) || (*end != '\0') || (errno == ERANGE)) {
        return false;
    }

    if (((int64_t)parsed_value < minimum) || ((int64_t)parsed_value > maximum)) {
        return false;
    }

    *value = (int64_t)parsed_value;
    return true;
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

static bool mon_write_value(mon_trace_entry_t *entry, const char *text)
{
    uint64_t unsigned_value;
    int64_t signed_value;
    double float_value;

    if ((entry == NULL) || (text == NULL)) {
        return false;
    }

    switch (entry->type) {
    case MON_VALUE_U8:
        if (!mon_parse_unsigned(text, UINT8_MAX, &unsigned_value)) {
            return false;
        }
        *(uint8_t *)entry->ptr = (uint8_t)unsigned_value;
        break;
    case MON_VALUE_I8:
        if (!mon_parse_signed(text, INT8_MIN, INT8_MAX, &signed_value)) {
            return false;
        }
        *(int8_t *)entry->ptr = (int8_t)signed_value;
        break;
    case MON_VALUE_U16:
        if (!mon_parse_unsigned(text, UINT16_MAX, &unsigned_value)) {
            return false;
        }
        *(uint16_t *)entry->ptr = (uint16_t)unsigned_value;
        break;
    case MON_VALUE_I16:
        if (!mon_parse_signed(text, INT16_MIN, INT16_MAX, &signed_value)) {
            return false;
        }
        *(int16_t *)entry->ptr = (int16_t)signed_value;
        break;
    case MON_VALUE_U32:
        if (!mon_parse_unsigned(text, UINT32_MAX, &unsigned_value)) {
            return false;
        }
        *(uint32_t *)entry->ptr = (uint32_t)unsigned_value;
        break;
    case MON_VALUE_I32:
        if (!mon_parse_signed(text, INT32_MIN, INT32_MAX, &signed_value)) {
            return false;
        }
        *(int32_t *)entry->ptr = (int32_t)signed_value;
        break;
    case MON_VALUE_U64:
        if (!mon_parse_unsigned(text, UINT64_MAX, &unsigned_value)) {
            return false;
        }
        *(uint64_t *)entry->ptr = (uint64_t)unsigned_value;
        break;
    case MON_VALUE_I64:
        if (!mon_parse_signed(text, INT64_MIN, INT64_MAX, &signed_value)) {
            return false;
        }
        *(int64_t *)entry->ptr = (int64_t)signed_value;
        break;
    case MON_VALUE_F32:
        if (!mon_parse_float64(text, &float_value)) {
            return false;
        }
        if (isfinite(float_value) &&
            ((float_value > FLT_MAX) || (float_value < -FLT_MAX))) {
            return false;
        }
        *(float *)entry->ptr = (float)float_value;
        break;
    case MON_VALUE_F64:
        if (!mon_parse_float64(text, &float_value)) {
            return false;
        }
        *(double *)entry->ptr = float_value;
        break;
    default:
        return false;
    }

    mon_read_value(entry, &entry->last_value);
    return true;
}

static void mon_queue_entry_value(const mon_trace_entry_t *entry,
                                  const char *prefix)
{
    char value_buffer[64];
    mon_value_snapshot_t current_value;

    mon_read_value(entry, &current_value);
    (void)mon_format_snapshot(entry->type,
                              &current_value,
                              value_buffer,
                              sizeof(value_buffer));

    mon_queue_textf("%s%s (%s) = %s\n",
                    prefix,
                    entry->name,
                    mon_type_name(entry->type),
                    value_buffer);
}

static void mon_check_traces(void)
{
    size_t index;

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        mon_trace_entry_t *entry = &g_mon_state.traces[index];

        if (entry->in_use) {
            mon_value_snapshot_t current_value;

            mon_read_value(entry, &current_value);

            if (!mon_snapshot_equal(entry->type, &entry->last_value, &current_value)) {
                (void)memcpy(&entry->last_value,
                             &current_value,
                             sizeof(entry->last_value));
                mon_queue_entry_value(entry, "trace ");
            }
        }
    }
}

static void mon_handle_help_command(void)
{
    mon_queue_text(
        "Commands:\n"
        "  help\n"
        "  list\n"
        "  get <name>\n"
        "  set <name> <value>\n");
}

static void mon_handle_list_command(void)
{
    size_t index;
    bool any_registered = false;

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        if (g_mon_state.traces[index].in_use) {
            any_registered = true;
            mon_queue_entry_value(&g_mon_state.traces[index], "");
        }
    }

    if (!any_registered) {
        mon_queue_message("No traces registered.\n");
    }
}

static void mon_handle_get_command(const char *name)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);

    if (entry == NULL) {
        mon_queue_textf("Unknown variable: %s\n", name);
        return;
    }

    mon_queue_entry_value(entry, "");
}

static void mon_handle_set_command(const char *name, const char *value)
{
    mon_trace_entry_t *entry = mon_find_trace_by_name(name);

    if (entry == NULL) {
        mon_queue_textf("Unknown variable: %s\n", name);
        return;
    }

    if (!mon_write_value(entry, value)) {
        mon_queue_textf("Invalid value for %s: %s\n", name, value);
        return;
    }

    mon_queue_entry_value(entry, "");
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
            mon_queue_message("Usage: help\n");
            return;
        }

        mon_handle_help_command();
        return;
    }

    if (strcmp(command, "list") == 0) {
        if (mon_next_token(&cursor) != NULL) {
            mon_queue_message("Usage: list\n");
            return;
        }

        mon_handle_list_command();
        return;
    }

    if (strcmp(command, "get") == 0) {
        argument1 = mon_next_token(&cursor);
        if ((argument1 == NULL) || (mon_next_token(&cursor) != NULL)) {
            mon_queue_message("Usage: get <name>\n");
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
            mon_queue_message("Usage: set <name> <value>\n");
            return;
        }

        mon_handle_set_command(argument1, argument2);
        return;
    }

    mon_queue_textf("Unknown command: %s\n", command);
}

static void mon_process_input(const char *input)
{
    const unsigned char *cursor =
        (const unsigned char *)(const void *)input;

    while (*cursor != '\0') {
        const unsigned char ch = *cursor;

        if ((ch == '\r') || (ch == '\n')) {
            if (g_mon_state.input_overflow) {
                mon_queue_message("[monitor] input line too long\n");
            } else if (g_mon_state.input_length > 0u) {
                g_mon_state.input_buffer[g_mon_state.input_length] = '\0';
                mon_process_line(g_mon_state.input_buffer);
            }

            g_mon_state.input_length = 0u;
            g_mon_state.input_overflow = false;
            cursor++;
            continue;
        }

        if ((ch == '\b') || (ch == 0x7FU)) {
            if (!g_mon_state.input_overflow && (g_mon_state.input_length > 0u)) {
                g_mon_state.input_length--;
            }

            cursor++;
            continue;
        }

        if (g_mon_state.input_overflow) {
            cursor++;
            continue;
        }

        if ((ch < 0x20U) && (ch != '\t')) {
            cursor++;
            continue;
        }

        if ((g_mon_state.input_length + 1u) >= MON_MAX_INPUT_LENGTH) {
            g_mon_state.input_overflow = true;
            cursor++;
            continue;
        }

        g_mon_state.input_buffer[g_mon_state.input_length] =
            (char)((ch == '\t') ? ' ' : ch);
        g_mon_state.input_length++;
        cursor++;
    }
}

static void mon_register_trace(void *ptr,
                               mon_value_type_t type,
                               const char *human_identifier)
{
    char identifier[MON_MAX_NAME_LENGTH];
    mon_trace_entry_t *entry;
    size_t index;

    if (ptr == NULL) {
        mon_queue_message("[monitor] null trace pointer ignored\n");
        return;
    }

    entry = mon_find_trace_by_pointer(ptr);
    if (entry != NULL) {
        const mon_value_type_t previous_type = entry->type;

        mon_copy_identifier(identifier,
                            sizeof(identifier),
                            human_identifier,
                            (size_t)(entry - &g_mon_state.traces[0]));

        if (mon_name_conflicts(entry, identifier)) {
            mon_queue_textf("[monitor] duplicate trace name: %s\n", identifier);
            return;
        }

        entry->type = type;
        (void)snprintf(entry->name, sizeof(entry->name), "%s", identifier);
        if (previous_type != type) {
            mon_read_value(entry, &entry->last_value);
        }
        return;
    }

    for (index = 0u; index < MON_MAX_TRACES; index++) {
        if (!g_mon_state.traces[index].in_use) {
            mon_copy_identifier(identifier,
                                sizeof(identifier),
                                human_identifier,
                                index);

            if (mon_name_conflicts(NULL, identifier)) {
                mon_queue_textf("[monitor] duplicate trace name: %s\n", identifier);
                return;
            }

            g_mon_state.traces[index].in_use = true;
            g_mon_state.traces[index].ptr = ptr;
            g_mon_state.traces[index].type = type;
            (void)snprintf(g_mon_state.traces[index].name,
                           sizeof(g_mon_state.traces[index].name),
                           "%s",
                           identifier);
            mon_read_value(&g_mon_state.traces[index],
                           &g_mon_state.traces[index].last_value);
            return;
        }
    }

    mon_queue_message("[monitor] trace registry full\n");
}

void mon_reset(const char *welcome_message)
{
    mon_state_clear();
    mon_queue_text((welcome_message != NULL) ? welcome_message
                                             : g_mon_default_welcome);
}

const char *mon_task(const char *input, int device_ready)
{
    g_mon_state.current_output[0] = '\0';

    mon_check_traces();

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

    if (fmt == NULL) {
        return NULL;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        mon_queue_message("[monitor] formatting failure\n");
        return NULL;
    }

    mon_queue_text(buffer);

    if ((size_t)written >= sizeof(buffer)) {
        mon_queue_message("[monitor] formatted output truncated\n");
    }

    return NULL;
}

#define MON_DEFINE_TRACE_FUNCTION(function_name, value_type, enum_type)       \
    void function_name(value_type *value, const char *human_identifier)       \
    {                                                                         \
        mon_register_trace((void *)value, enum_type, human_identifier);       \
    }

MON_DEFINE_TRACE_FUNCTION(mon_trace_u8, uint8_t, MON_VALUE_U8)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i8, int8_t, MON_VALUE_I8)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u16, uint16_t, MON_VALUE_U16)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i16, int16_t, MON_VALUE_I16)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u32, uint32_t, MON_VALUE_U32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i32, int32_t, MON_VALUE_I32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_u64, uint64_t, MON_VALUE_U64)
MON_DEFINE_TRACE_FUNCTION(mon_trace_i64, int64_t, MON_VALUE_I64)
MON_DEFINE_TRACE_FUNCTION(mon_trace_f32, float, MON_VALUE_F32)
MON_DEFINE_TRACE_FUNCTION(mon_trace_f64, double, MON_VALUE_F64)
