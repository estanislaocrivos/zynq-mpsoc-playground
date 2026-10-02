# Zynq UltraScale+ MPSoC Playground 🛝

Learning-by-doing repository for the AMD Zynq UltraScale+ MPSoC, using the
**Kria KR260** starter kit (K26 SoM). The goal is to understand the hardware
underneath the vendor tooling: boot flow, memory map, processor modes and
peripherals, driven through raw register accesses instead of HAL drivers.

## Contents

| Path                                               | What it is                                                                                      |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| [`r5/bare-metal/`](r5/bare-metal/)                 | Bare-metal firmware for the Cortex-R5F (RPU core 0): own startup code, no BSP, no RTOS          |
| [`r5/bare-metal/TASKS.md`](r5/bare-metal/TASKS.md) | Lab plan: console, system counter, TTC, GIC, IPI                                                |
| [`r5/bare-metal/NOTES.md`](r5/bare-metal/NOTES.md) | Study notes (Spanish): boot sequence, memory map, caches/MPU, remoteproc, IPI                   |
| `r5/bare-metal/petalinux/`                         | PetaLinux project files used on the board (device tree with the R5 memory reservations and IPI) |

## Current status

- R5 firmware boots from its own vector table and `_boot` routine in ATCM,
  sets up per-mode stacks, enables the VFP and jumps to `main` in DDR.
- Console output through UART1 by polling (`uart_print_string`, `print_hex`).
- Register dump of the core and SoC state (CP15, CPSR, RPU configuration).
- IPI round trip with a Linux application on the A53 (IPI channel 1 ↔ 2,
  shared memory at `0x3EE0_0000`), with the R5 polling its IPI status register.

## Requirements

- KR260 running PetaLinux 2025.1 with the device tree from `r5/bare-metal/petalinux/`
  (reserved memory for the R5 and the shared buffer, IPI exposed through UIO).
- `arm-none-eabi-gcc` toolchain and CMake ≥ 3.20 on the host.
- Optional: XSDB (Vitis) for JTAG debugging.

## Quick start

```bash
cd r5/bare-metal
./scripts/build.sh # produces build/firmware-r5.elf
```

See [`r5/bare-metal/README.md`](r5/bare-metal/README.md) for loading and
debugging through XSDB.

## References

- UG1085 — Zynq UltraScale+ Device Technical Reference Manual
- UG1087 — Zynq UltraScale+ Register Reference
- DDI 0460 — Arm Cortex-R5 Technical Reference Manual
- DDI 0406 — ARMv7-A/R Architecture Reference Manual
- UG1092 — KR260 Robotics Starter Kit User Guide

## License

[MIT](LICENSE)
