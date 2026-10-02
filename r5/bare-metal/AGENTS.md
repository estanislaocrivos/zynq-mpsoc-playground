# Agent notes

Working notes for this lab. The plan lives in `TASKS.md`.

## Findings

- **Reset vector location.** `RPU_0_CFG.VINITHI` (UG1087, RPU module) resets
  high: the R5 fetches its first instruction from `0xFFFF0000` (OCM), not from
  `0x00000000` (ATCM). Something must clear it before releasing the core for
  the vector table in ATCM to be used. Per UG1085, the FSBL does it: it clears
  `VINITHI` through the SLCR (`RPU_0_CFG`) and sets `SCTLR.V` to LOVEC. The pin
  only sets the reset value of `SCTLR.V`; software can change `SCTLR.V` later.
  AMD recommends staying on LOVEC (HIVEC in OCM adds latency and jitter, and a
  non-secure R5 cannot reach it if the OCM is secured). Pending: confirm the
  state of both after a JTAG `rst -proc` and in the remoteproc flow.
