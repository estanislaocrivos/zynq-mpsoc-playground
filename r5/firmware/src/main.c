#include <stdint.h>

#define SYSCNT_FREQ_HZ 100000000UL

static inline uint64_t read_system_clock_counter(void)
{
    volatile uint32_t* cntcv_l = (volatile uint32_t*)0xFF260008UL;
    volatile uint32_t* cntcv_h = (volatile uint32_t*)0xFF26000CUL;

    uint32_t hi, lo;
    do
    {
        hi = *cntcv_h;
        lo = *cntcv_l;
    } while (*cntcv_h != hi);

    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t ticks_to_ns(uint64_t ticks)
{
    return (uint32_t)((ticks * 1000000000ULL) / SYSCNT_FREQ_HZ);
}

void uart_print_char(const char c)
{
    volatile uint32_t* uart1_channel_sts = (volatile uint32_t*)0x00FF01002C;
    volatile uint32_t* uart1_tx_rx_fifo  = (volatile uint32_t*)0x00FF010030;

    while (*uart1_channel_sts & (1 << 4))
    {
        /* Wait */
    }
    *uart1_tx_rx_fifo = c;
}

void uart_print_string(const char* s)
{
    while (*s != '\0')
    {
        uart_print_char(*s);
        s += 1;
    }
}

int main(void)
{
    ticks_to_ns(read_system_clock_counter());
    uart_print_string("Hello!\r\n");
    return 0;
}
