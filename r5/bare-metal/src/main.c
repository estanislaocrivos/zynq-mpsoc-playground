#include "main.h"

#include "logging.h"

#include <stdint.h>

#define SYSCNT_FREQ_HZ          100000000UL
#define TIME_BETWEEN_QUERIES_MS 1000U

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

/* Busy-wait on the system counter, independent of the CPU and fetch speed */
static void delay_ms(uint32_t ms)
{
    uint64_t target = read_system_clock_counter()
                      + (uint64_t)ms * (SYSCNT_FREQ_HZ / 1000U);

    while ((int64_t)(read_system_clock_counter() - target) < 0)
    {
        /* Wait */
    }
}

int main(void)
{
    log_string("Hello, from R5...\r\n");

    uint32_t r2a_seq = 1;

    while (1)
    {
        /* Clear any IPI left pending by a previous run */
        write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

        /* Write seq. number */
        write_memory(SHARED_MEM_BASE_ADDR + SHARED_MEM_R2A_SEQ_OFF, r2a_seq);

        /* The seq. number must reach the shm before the A53 is notified */
        DO_NOT_REORDER_GUARD();

        /* Trigger interrupt for A53 */
        write_memory(IPI_SELF_BASEADDR + IPI_TRIG_OFFSET, IPI_A53_BITMASK);

        while (!(
            read_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET) & IPI_A53_BITMASK))
        {
            /* Poll interrupt */
        }

        /* Clear */
        write_memory(IPI_SELF_BASEADDR + IPI_ISR_OFFSET, IPI_A53_BITMASK);

        log_string("Interrupt arrived.\r\n");

        /* A53 echoes the seq. number it processed */
        uint32_t a2r_seq
            = read_memory(SHARED_MEM_BASE_ADDR + SHARED_MEM_A2R_SEQ_OFF);

        /* Check seq. number */
        if (a2r_seq != r2a_seq)
        {
            log_string("Seq. number mismatch, a2r_seq: ");
            log_value(a2r_seq);
        }

        /* NTP 32.32: seconds word first, then fraction */
        uint32_t ntp_ts_int
            = read_memory(SHARED_MEM_BASE_ADDR + SHARED_MEM_NTP_TIMESTAMP_OFF);
        uint32_t ntp_ts_frac = read_memory(
            SHARED_MEM_BASE_ADDR + SHARED_MEM_NTP_TIMESTAMP_OFF + 4);

        log_string("NTP seconds: ");
        log_value(ntp_ts_int);
        log_string("NTP fraction: ");
        log_value(ntp_ts_frac);

        r2a_seq += 1;

        delay_ms(TIME_BETWEEN_QUERIES_MS);
    }

    return 0;
}
