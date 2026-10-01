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

/* Read a CP15 register: MRC p15, <op1>, <Rt>, <CRn>, <CRm>, <op2> */
#define READ_CP15(op1, crn, crm, op2)                                       \
    ({                                                                      \
        uint32_t _v;                                                        \
        __asm__ volatile("mrc p15, " #op1 ", %0, " #crn ", " #crm ", " #op2 \
                         : "=r"(_v));                                       \
        _v;                                                                 \
    })

static inline uint32_t read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile("mrs %0, cpsr" : "=r"(v));
    return v;
}

static inline uint32_t read_sp(void)
{
    uint32_t v;
    __asm__ volatile("mov %0, sp" : "=r"(v));
    return v;
}

static inline uint32_t read_fpexc(void)
{
    uint32_t v;
    __asm__ volatile("vmrs %0, fpexc" : "=r"(v));
    return v;
}

static inline uint32_t read_reg32(uint32_t addr)
{
    return *(volatile uint32_t*)addr;
}

static void print_reg(const char* name, uint32_t value)
{
    uart_print_string(name);
    print_hex(value);
}

int main(void)
{
    uart_print_string("\r\n--- R5 register dump ---\r\n");

    /* Core state */
    print_reg("CPSR         = ", read_cpsr()); /* mode [4:0], I/F masks [7:6] */
    print_reg("SP (System)  = ", read_sp());

    /* CP15 identification (Cortex-R5 TRM, ch. 4) */
    print_reg("MIDR         = ", READ_CP15(0, c0, c0, 0));
    print_reg("MPIDR        = ", READ_CP15(0, c0, c0, 5));

    /* CP15 configuration */
    print_reg("SCTLR        = ", READ_CP15(0, c1, c0, 0)); /* M[0] C[2] I[12]
                                                              V[13] */
    print_reg("CPACR        = ", READ_CP15(0, c1, c0, 2)); /* CP10/CP11 access
                                                            */
    print_reg("FPEXC        = ", read_fpexc());            /* EN[30] */
    print_reg("BTCM region  = ", READ_CP15(0, c9, c1, 0));
    print_reg("ATCM region  = ", READ_CP15(0, c9, c1, 1));

    /* SoC registers (UG1087) */
    print_reg("RPU_GLBL_CNTL= ", read_reg32(0xFF9A0000)); /* split/lockstep */
    print_reg("RPU_0_CFG    = ", read_reg32(0xFF9A0100)); /* VINITHI */

    /* System counter */
    uint64_t ticks = read_system_clock_counter();
    print_reg("SYSCNT hi    = ", (uint32_t)(ticks >> 32));
    print_reg("SYSCNT lo    = ", (uint32_t)ticks);

    return 0;
}
