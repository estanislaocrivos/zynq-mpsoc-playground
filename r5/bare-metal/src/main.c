#include "main.h"

#include "logging.h"

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

int main(void)
{
    log_string("Hello, from R5...\r\n");

    /* Clear any IPI left pending by a previous run */
    write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

    /* Write seq. number into the request buffer */
    write_memory(IPI_R2A_REQUEST_ADDR + IPI_REQUEST_SEQ_OFF, 0x1234);

    /* The seq. number must reach the buffer before the A53 is notified */
    DO_NOT_REORDER_GUARD();

    /* Trigger interrupt for A53 */
    write_memory(IPI_SELF_BASEADDR + IPI_TRIG_OFFSET, IPI_A53_BITMASK);

    /* OBS: bit set while the A53 has not cleared its ISR */
    log_string("OBS after trigger: ");
    log_value(read_memory(IPI_SELF_BASEADDR + IPI_OBS_OFFSET));

    while (!(read_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET) & IPI_A53_BITMASK))
    {
        /* Poll interrupt */
    }

    /* Clear */
    write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

    log_string("Interrupt arrived.\r\n");

    /* A53 echoes the seq. number it processed in the response buffer */
    log_string("a2r_seq: ");
    log_value(read_memory(IPI_A2R_RESPONSE_ADDR + IPI_RESPONSE_SEQ_OFF));

    return 0;
}
