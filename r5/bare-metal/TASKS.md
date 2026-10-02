# ZynqMP R5 bare-metal lab

Learning-by-doing repository: drive the Cortex-R5 peripherals of a Kria KR260
(Zynq UltraScale+) through raw register accesses, without the Xilinx drivers and
without FreeRTOS, until the underlying hardware is actually understood.

## Ground rules

- **Bare metal, no RTOS.** The FreeRTOS port owns the GIC, the vector table and
  TTC0 channel 0. Owning all of that is the point of this lab.
- **Write the register map by hand.** Add each `#define` only after finding the
  register in UG1087. Do not include the Xilinx headers.
- **Read back every write.** Compare against what was expected. Reserved bits,
  read-only fields and registers that require the peripheral to be stopped are
  the usual source of lost hours.
- **Keep a logbook.** One entry per register touched: address, value written,
  value read back, value expected. When something misbehaves, the logbook tells
  whether the bug is in the code or in the mental model.
- **Read the Xilinx driver last.** After the exercise works, not before.

## References

| Document                                                        | Used for                                                                      |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| **UG1085** — Zynq UltraScale+ Device TRM                        | How each block works: TTC, IPI, system counter, RPU interrupts                |
| **UG1087** — Register Reference                                 | Exact addresses, bit fields, reset values. A lookup table, not a read-through |
| **Arm Cortex-R5 TRM** (DDI 0460)                                | The core: exception modes, MPU, interrupt latency, caches                     |
| **ARMv7-R Architecture Reference Manual** (DDI 0406)            | Architecture: CPSR, processor modes, vector table, memory barriers            |
| **Arm Generic Interrupt Controller Architecture Specification** | GIC: distributor, CPU interface, priorities, EOI                              |

## Rungs

Each rung is independently verifiable. Do not start the next one until the
current one is understood, not merely working.

### 0. Console

Write a `putchar` straight to the UART registers, then a minimal `print_hex` /
`print_dec`. Everything else depends on being able to see something.

- **Learn:** UART register map, the TX-full flag, why polling the status
  register matters.
- **Verify:** characters appear on the serial console.
- **Watch out:** Linux uses one of the two UARTs (`ttyPS1` at `0xFF010000` on
  this board). Pick the other one, or output into a memory buffer read from
  Linux, otherwise both sides interleave their output.

### 1. System counter

Read `0xFF260008` / `0xFF26000C` in a loop and measure how far it advances
during a busy wait.

- **Learn:** why a 64-bit value read through two 32-bit accesses needs the
  `hi / lo / hi` pattern, and why the pointer must be `volatile`.
- **Verify:** time 10 seconds with a stopwatch; the delta should be about 1e9
  ticks.
- **Read:** UG1085, system counter (IOU_SCNTRS) section; UG1087, IOU_SCNTRS
  registers.

### 2. TTC, polled

Configure TTC0 channel 2 by hand: `CLK_CNTRL` (prescaler, clock source),
`CNT_CNTRL` (mode, start), then read `COUNT_VALUE` in a loop.

- **Learn:** the interleaved channel layout (base + 4·n), interval versus
  overflow mode, the effect of the prescaler.
- **Verify:** count how many TTC ticks elapse during 1e8 system counter ticks.
  That ratio is exactly what the PPS work needs.
- **Read:** UG1085, TTC chapter; UG1087, TTC module.

### 3. Match event, still polled

Write `MATCH_0`, enable match mode in `CNT_CNTRL`, enable `MATCH_0` in `IER`,
and poll `ISR` until the bit shows up.

- **Learn:** the *clear on read* semantics of `ISR`; the difference between "the
  event happened" (`ISR`) and "the event interrupts" (`IER`); 32-bit rollover
  and why the comparison must be modular.
- **Verify:** program a match one second ahead and measure the actual elapsed
  time with the system counter.

### 4. Interrupts, with the GIC by hand

The big one, and the one that only makes sense once written.

1. Own vector table at `0x00000000` (ATCM), branching to the IRQ handler.
2. RPU GIC setup: in the distributor, enable ID 70 (TTC0 channel 2), set its
   priority and target CPU; in the CPU interface, set the priority mask and the
   enable bit.
3. In the handler: read `IAR` to identify the interrupt, service it, write
   `EOIR`.
4. Enable interrupts in the core (`cpsie i`).

- **Learn:** why the GIC is split into distributor and CPU interface; what the
  priority mask does; why forgetting `EOIR` yields exactly one interrupt and
  never another; level versus edge triggering — which is the crux of routing a
  TTC match to the PL as a PPS.
- **Verify:** a counter incremented in the handler, printed from the main loop.
- **Read:** GIC specification (distributor and CPU interface programming);
  UG1085, RPU interrupt section; Cortex-R5 TRM, exceptions chapter.

### 5. IPI

Trigger an IPI from the A53 with `devmem` and detect it on the R5 by polling
the IPI `ISR`. Then the other direction. Only then, with an interrupt.

- **Learn:** the agent/bit-mask map (one bit per processor), the roles of
  `TRIG`, `OBS`, `ISR`, `IMR`, `IER`, `IDR`, and that the message buffers are
  optional — any shared memory works.
- **Verify:** read the status registers from both sides while triggering from
  Linux.
- **Read:** UG1085, IPI chapter (agent and channel tables); UG1087, IPI modules.

### 6. Compare against the drivers

Now read `xttcps.c`, `xscugic.c` and `xipipsu.c` from `embeddedsw`. Every line
should be readable, and the edge cases they handle are the interesting part.

## Loading the ELF

Two options, pick one deliberately:

- **remoteproc** — reuse the `run_app_r5_0.sh` script from the main project.
  Requires Linux running and a resource table in the ELF.
- **JTAG / XSDB** — better for learning: load, halt the core, inspect registers
  and memory live, set breakpoints. The cost is that the PMUFW hands the RPU to
  the debugger, so Linux cannot boot it through remoteproc until the board is
  power-cycled (it fails with `Unable to request node 7`).

## Suggested layout

```text
src/
  start.S        vector table, stack setup, branch to main
  uart.c         raw putchar / print helpers
  regs.h         hand-written register map, one block per peripheral
  main.c         the exercise under way
linker/
  lscript.ld     ATCM at 0x00000000, code/data at 0x3ED00000
Makefile         three files do not need CMake
logbook.md       one entry per register touched
```
