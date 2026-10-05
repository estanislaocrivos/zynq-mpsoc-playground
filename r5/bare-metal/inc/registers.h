#ifndef REGISTERS_H
#define REGISTERS_H

/* ========================================================================== */

#include <stdint.h>

/* ========================================================================== */

#define SHARED_MEM_BASE_ADDR             0x3ee00000UL
#define SHARED_MEM_SIZE                  0x100000UL

#define SHARED_MEM_A2R_SEQ_OFF           0x00U
#define SHARED_MEM_R2A_SEQ_OFF           0x04U
#define SHARED_MEM_NTP_TIMESTAMP_OFF     0x08U
#define SHARED_MEM_COUNTER_TIMESTAMP_OFF 0x10U
#define SHARED_MEM_WINDOW_OFF            0x18U
#define SHARED_MEM_RTT_OFF               0x1CU

/* ========================================================================== */

/* IPI (UG1085 ch. 13, UG1087 IPI module) */

/* Channel register bases. IPI1 = Ch 1, owned by R5_0 (this core). IPI2 = Ch 2,
 * used by the A53 application through UIO (ipi_amp@ff320000 in the dtsi). */
#define IPI1_BASEADDR                    0xFF310000UL
#define IPI2_BASEADDR                    0xFF320000UL

/* Register offsets, identical for every channel */
#define IPI_TRIG_OFFSET                  0x00U /* wo: bit = destination   */
#define IPI_OBS_OFFSET                   0x04U /* ro: pending at dest     */
#define IPI_ISR_OFFSET                   0x10U /* w1c: bit = source       */
#define IPI_IMR_OFFSET                   0x14U /* ro: 1 = masked          */
#define IPI_IER_OFFSET                   0x18U /* wo: 1 = unmask          */
#define IPI_IDR_OFFSET                   0x1CU /* wo: 1 = mask            */

/* Agent bit masks, indexed by channel (same layout in TRIG/OBS/ISR/IMR) */
#define IPI1_0_IPI_BITMASK               0x00000001U /* Ch 0:  APU (Linux <-> PMUFW) */
#define IPI1_1_IPI_BITMASK               0x00000100U /* Ch 1:  RPU0 (this core)      */
#define IPI1_2_IPI_BITMASK               0x00000200U /* Ch 2:  RPU1 default, A53 app */
#define IPI1_3_IPI_BITMASK               0x00010000U /* Ch 3:  PMU0 (PMU only)       */
#define IPI1_4_IPI_BITMASK               0x00020000U /* Ch 4:  PMU1 (PMU only)       */
#define IPI1_5_IPI_BITMASK               0x00040000U /* Ch 5:  PMU2 (PMU only)       */
#define IPI1_6_IPI_BITMASK               0x00080000U /* Ch 6:  PMU3 (PMU only)       */
#define IPI1_7_IPI_BITMASK               0x01000000U /* Ch 7:  PL0 default, free     */
#define IPI1_8_IPI_BITMASK               0x02000000U /* Ch 8:  PL1 default, free     */
#define IPI1_9_IPI_BITMASK               0x04000000U /* Ch 9:  PL2 default, free     */
#define IPI1_10_IPI_BITMASK              0x08000000U /* Ch 10: PL3 default, free     */

/* Channel assignment for this application. To move the A53 app to Ch 7,
 * change IPI_A53_BITMASK to IPI1_7_IPI_BITMASK, IPI_A53_BUF_INDEX to
 * IPI_CH7_BUF_INDEX (and the dtsi node). */
#define IPI_SELF_BASEADDR                IPI1_BASEADDR
#define IPI_A53_BITMASK                  IPI1_2_IPI_BITMASK
#define IPI_SELF_BITMASK                 IPI1_1_IPI_BITMASK

/* ========================================================================== */

/* IPI message buffers (UG1085 ch. 13, xipipsu_hw.h) */

/* 8 blocks (one per buffer index) x 8 slots (one per destination index).
 * Each slot holds a 32-byte request followed by a 32-byte response. Both
 * live in the initiator's block, in the responder's slot. */
#define IPI_MSG_BUF_BASEADDR             0xFF990000UL
#define IPI_MSG_BUF_BLOCK_SIZE           0x200U
#define IPI_MSG_BUF_SLOT_SIZE            0x40U
#define IPI_MSG_BUF_RESPONSE_OFFSET      0x20U

/* Buffer index per channel (not the channel number) */
#define IPI_CH1_BUF_INDEX                0U
#define IPI_CH2_BUF_INDEX                1U
#define IPI_CH7_BUF_INDEX                3U

#define IPI_SELF_BUF_INDEX               IPI_CH1_BUF_INDEX
#define IPI_A53_BUF_INDEX                IPI_CH2_BUF_INDEX

/* R5 initiates: request written by R5, response written by A53 */
#define IPI_R2A_REQUEST_ADDR                                            \
    (IPI_MSG_BUF_BASEADDR + IPI_SELF_BUF_INDEX * IPI_MSG_BUF_BLOCK_SIZE \
     + IPI_A53_BUF_INDEX * IPI_MSG_BUF_SLOT_SIZE)
#define IPI_A2R_RESPONSE_ADDR \
    (IPI_R2A_REQUEST_ADDR + IPI_MSG_BUF_RESPONSE_OFFSET)

/* Field offsets inside each buffer */
#define IPI_REQUEST_SEQ_OFF  0x00U
#define IPI_RESPONSE_SEQ_OFF 0x00U

/* ========================================================================== */

static inline void write_memory(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t*)address = value;
}

static inline uint32_t read_memory(uintptr_t address)
{
    return *(volatile uint32_t*)address;
}

/* ========================================================================== */

#endif
