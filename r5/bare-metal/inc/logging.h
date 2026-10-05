#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>

/* Blocking output over UART1. Assumes the UART was already configured by
 * whoever booted us (FSBL / Linux). */

/* Prints a NUL-terminated string as is */
void log_string(const char* s);

/* Prints a value as 0xXXXXXXXX followed by \r\n */
void log_value(uint32_t value);

#endif
