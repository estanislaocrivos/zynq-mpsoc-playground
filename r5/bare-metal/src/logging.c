#include "logging.h"

static void uart_print_char(const char c)
{
    volatile uint32_t* uart1_channel_sts = (volatile uint32_t*)0x00FF01002C;
    volatile uint32_t* uart1_tx_rx_fifo  = (volatile uint32_t*)0x00FF010030;

    while (*uart1_channel_sts & (1 << 4))
    {
        /* Wait */
    }
    *uart1_tx_rx_fifo = c;
}

static void uart_print_string(const char* s)
{
    while (*s != '\0')
    {
        uart_print_char(*s);
        s += 1;
    }
}

static void print_hex(uint32_t value)
{
    uint8_t k = 0;
    char    number[13];
    uint8_t num = 0;
    for (k = 0; k < 8; k += 1)
    {
        num           = (value >> (28 - 4 * k)) & 0xF;
        number[2 + k] = num < 10 ? '0' + num : 'A' + (num - 10);
    }
    number[1]  = 'x';
    number[0]  = '0';
    number[10] = '\r';
    number[11] = '\n';
    number[12] = '\0';
    uart_print_string(number);
}

void log_string(const char* s)
{
    uart_print_string(s);
}

void log_value(uint32_t value)
{
    print_hex(value);
}
