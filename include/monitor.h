#pragma once

// Function to for cyclic calling. Input is 
// input provided by the user. device_ready is a bool
// indicating if the peripheral used to output the message
// is capable of accepting new output. If not the module
// must buffer.
//
// Returns a c string to output via some peripheral
// Memory valid until next call of minimon_task()
const char *mon_task(const char *input, int device_ready);

// Print something to the user
const char *mon_print(const char *fmt, ...);

// Register a u8 variable for introspection (reading/writing) by the user
// ptr must be valid util call of mon_task()
#define MON_TRACE_U8(ptr) mon_trace_u8((ptr), ##ptr)
void mon_trace_u8(uint8_t *v, const char *human_identifer);

// TODO: add signed/unsigned 16, 32, 64 bit and float versions
