#include "main.h"

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

void print_hex(uint32_t value)
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

static inline void write_memory(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t*)address = value;
}

static inline uint32_t read_memory(uintptr_t address)
{
    return *(volatile uint32_t*)address;
}

int main(void)
{
    uart_print_string("Hello, from R5...\r\n");

    /* Clear any IPI left pending by a previous run */
    write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

    /* Write seq. number */
    write_memory(SHARED_MEM_BASE_ADDR + SHARED_MEM_R2A_SEQ_OFF, 0x1234);

    /* The seq. number must reach the shm before the A53 is notified */
    DO_NOT_REORDER_GUARD();

    /* Trigger interrupt for A53 */
    write_memory(IPI_SELF_BASEADDR + IPI_TRIG_OFFSET, IPI_A53_BITMASK);

    /* OBS: bit set while the A53 has not cleared its ISR */
    uart_print_string("OBS after trigger: ");
    print_hex(read_memory(IPI_SELF_BASEADDR + IPI_OBS_OFFSET));

    while (!(read_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET) & IPI_A53_BITMASK))
    {
        /* Poll interrupt */
    }

    /* Clear */
    write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

    uart_print_string("Interrupt arrived.\r\n");

    /* A53 echoes the seq. number it processed */
    uart_print_string("a2r_seq: ");
    print_hex(read_memory(SHARED_MEM_BASE_ADDR + SHARED_MEM_A2R_SEQ_OFF));

    return 0;
}
