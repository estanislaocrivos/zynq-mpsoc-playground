# Cortex-R5 (ZynqMP) - Notas

## Regiones de memoria disponibles para el procesador

El core R5 puede acceder a tres tipos de memoria:

| Tipo de memoria              | Dirección (vista R5)                   | Tamaño                                                              | Cacheable                               | Descripción                                                                                                                                                                    |
| ---------------------------- | -------------------------------------- | ------------------------------------------------------------------- | --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| TCM (Tightly Coupled Memory) | ATCM `0x0000_0000`, BTCM `0x0002_0000` | 64 KB por banco y por core (128 KB por banco en lockstep)           | No (no lo necesita: acceso en un ciclo) | SRAM conectada por puertos dedicados del core, sin interconnect ni caché: tiempo determinista. El acceso rápido es exclusivo de su core; el A53 y JTAG la ven en `0xFFE0_0000` |
| OCM (On-Chip Memory)         | `0xFFFC_0000`                          | 256 KB                                                              | Sí, configurable (MPU + `SCTLR.C`/`I`)  | SRAM compartida en el interconnect del LPD (A53, R5, PMU, DMA, PL). La usan el boot ROM, el FSBL y el ATF: no pisarla                                                          |
| DDR                          | `0x3ED0_0000` / `0x3EE0_0000`          | 1 MB de código/datos + 1 MB de `shm` (reservados en el device tree) | Sí, configurable (MPU + `SCTLR.C`/`I`)  | Memoria de programa y de propósito general; `shm` para compartir datos con el A53                                                                                              |

Hoy `start.S` no habilita cachés ni MPU, así que no se cachea nada.

- `RPU_0_CFG.VINITHI = 1` por defecto en reset, lo que implica que el core va a arrancar leyendo los vectores altos en la OCM (`0xFFFF_0000`) y no la TCM (`0x0000_0000`).
- Con `VINITHI = 0` al arrancar, el core lee la dirección `0x0000_0000`, donde espera encontrar una instrucción de salto que lo lleve al código de arranque. Es una instrucción de 32 bits (como todas en ARM).

### `VINITHI` vs. `SCTLR.V`

- `VINITHI` es un **pin** del core, manejado por el SoC a través de `RPU_0_CFG` (SLCR). El core lo muestrea **solo en el reset** y copia su valor en `SCTLR.V`.
- `SCTLR.V` (bit 13 del registro CP15 `SCTLR`) es lo que el core consulta **en cada excepción**. El software puede cambiarlo después del reset.
- Leer `RPU_0_CFG` dice qué va a tomar el core en el **próximo** reset, no qué está usando ahora. Para eso hay que leer `SCTLR`: `mrc p15, 0, <Rt>, c1, c0, 0`.
- AMD recomienda quedarse en LOVEC (`VINITHI = 0`, `SCTLR.V = 0`): con HIVEC cada excepción busca su vector en la OCM, con más latencia y jitter.

## Cachés, MPU y comunicación con el A53

Verificado con XSDB (`rrd cp15 c1`), corriendo este firmware por `remoteproc` tras un power-cycle:

| Firmware                | `SCTLR`      | `M` (MPU) | `C` (caché datos) | `I` (caché instr.) | `V` (vectores) |
| ----------------------- | ------------ | --------- | ----------------- | ------------------ | -------------- |
| Este (`start.S` propio) | `0x00E50878` | 0         | 0                 | 0                  | 0 (LOVEC)      |

Qué implica tenerlos apagados:

1. **No hay datos viejos en caché.** El R5 lee y escribe directo en memoria, y del lado del A53 UIO mapea la `shm` sin caché. En el proyecto FreeRTOS había que marcar la `shm` como no cacheable en el MPU (`Xil_SetMPURegion(..., NORM_SHARED_NCACHE ...)`) justamente porque el BSP prendía las cachés.
2. **Sin caché no significa sin reordenamiento.** Una escritura puede quedar un rato en un buffer del camino a memoria. Si el R5 escribe un mensaje en `shm` y enseguida dispara la IPI, el A53 podría leer el mensaje incompleto. Regla: poner una barrera (`dsb`) entre "escribir los datos" y "avisar" (antes lo hacía libmetal).
3. **MPU apagado = sin restricciones del R5, pero no del SoC.** Corriendo en modo System (privilegiado) no hay regiones que bloqueen. Aun así, el ZynqMP tiene protección a nivel sistema, fuera del core: **XMPU** (DDR y OCM) y **XPPU** (periféricos), configurados por el FSBL/PMUFW. Si bloquean un acceso, aparece un Data Abort igual.
4. **Accesos desalineados.** En memoria de tipo *device* / *strongly-ordered* un acceso desalineado genera fault aunque `SCTLR.A = 0`. Pendiente: verificar qué tipo de memoria usa el R5 para datos con `SCTLR.M = 0` (ARMv7 ARM, B5). Del lado del A53, un `memcpy` sobre memoria UIO sin caché puede dar `SIGBUS` por lo mismo.
5. **Costo: velocidad.** Sin caché, cada acceso a DDR cruza el interconnect. Para el lab no importa; si un handler necesita baja latencia, se mueve a la TCM.

## Secuencia de boot del ZynqMP (PMU → CSU → FSBL → ATF → U-Boot → Linux)

El SoC tiene más procesadores que los A53 y los R5:

| Procesador            | Qué es                                                   | Qué firmware corre                    |
| --------------------- | -------------------------------------------------------- | ------------------------------------- |
| PMU                   | MicroBlaze triplicado (redundancia), en el LPD           | PMU ROM, y después el **PMUFW**       |
| CSU                   | MicroBlaze triplicado, unidad de configuración/seguridad | CSU BootROM (fijo en silicio)         |
| A53_0 (habitual) o R5 | Core de aplicación                                       | **FSBL**, después ATF, U-Boot y Linux |

```text
1. Encendido   → PMU ROM (en el PMU): inicializa lo mínimo y libera la CSU.
2. CSU BootROM → lee los pines de boot mode, encuentra BOOT.BIN (SD/QSPI),
                 carga el PMUFW en la RAM del PMU y el FSBL en la OCM,
                 y libera el A53_0.
3. FSBL        → (en el A53_0, ejecutando desde la OCM) inicializa el PS:
                 clocks, MIO, DDR (psu_init). Carga el bitstream en la PL,
                 el ATF, U-Boot y, si hay, apps para el R5. Configura VINITHI.
4. ATF → U-Boot → Linux (en los A53).
   PMUFW       → queda residente en el PMU, atendiendo pedidos EEMI.
```

- El FSBL corre desde la **OCM** porque la DDR todavía no está inicializada: el controlador y el PHY necesitan configuración y *training*, que hace `psu_init`. La OCM es SRAM on-chip y funciona sin configurar nada. Por eso la OCM no se pisa.
- El `psu_init.tcl` que se usa desde XSDB hace por JTAG lo mismo que `psu_init` en el FSBL.

### Energía del RPU: PMUFW, remoteproc y XSDB

- El **PMUFW** es el dueño de la energía: los demás masters le **piden** nodos (EEMI `request_node` / `release_node`). IDs en el dtsi: `7` = RPU_0, `15` = ATCM0, `16` = BTCM0.
- Sin firmware corriendo, el RPU queda apagado (`targets` en XSDB muestra `Cortex-R5 #0 (No Power)` y `dow` falla con `The core is powered down`). Dos mecanismos lo apagan:
  1. El PMUFW en su init, si el `BOOT.BIN` no trae app para el R5 (opción de compilación en `xpfw_config.h`).
  2. Linux llama a `PM_INIT_FINALIZE` al final del boot y el PMUFW apaga los nodos que ningún master pidió.
- `remoteproc` pide los nodos al arrancar un firmware y los libera al pararlo. XSDB no participa de EEMI: no puede prender el core. Si XSDB toma el RPU, después remoteproc falla con `Unable to request node 7` hasta un power-cycle.
- Para inspeccionar con XSDB sin pelear con el PMUFW: arrancar el ELF con `remoteproc` y después, en XSDB, solo `stop` + `rrd` (sin `rst`).
- Las cachés del R5 (`SCTLR.C`/`I`) y el MPU (`SCTLR.M`) solo los puede cambiar el propio R5: Linux y el device tree no los tocan. Del lado del A53, `no-map` y el `mmap` de `generic-uio` dejan la `shm` sin caché.

## Secuencia de boot (ARM R5)

- Al compilar, el código de startup `start.S` se convierte en bytes crudos. El **linker**, siguiendo `lscript.ld`, asigna cada sección a una región de memoria, y el `.elf` guarda el resultado. En este caso, los vectores de arranque, que le indican al core adónde dirigirse para bootear, se ubican en la ATCM (`0x00000000`):

```text
MEMORY
{
   psu_r5_0_atcm_MEM_0 : ORIGIN = 0x00000000,   LENGTH = 0x10000
   psu_r5_ddr_0_MEM_0  : ORIGIN = 0x3ed00000,   LENGTH = 0x100000
   psu_r5_shm_MEM_0    : ORIGIN = 0x3ee00000,   LENGTH = 0x100000
}
```

```text
.vectors : {
   KEEP (*(.vectors))
   *(.boot)
} > psu_r5_0_atcm_MEM_0

.bootdata : {
   *(.bootdata)
} > psu_r5_0_atcm_MEM_0
```

El loader (`remoteproc` o `xsdb`) lee el `.elf` y, antes de liberar el core, escribe en cada región de memoria **los bytes que el ELF trae**: la tabla de vectores y `_boot` en la ATCM, y `.text`, `.rodata` y `.data` en la DDR. Las secciones que son solo espacio reservado (`.bss`, heap, stack) no tienen bytes en el ELF. Se ve en los siguientes outputs:

```bash
arm-none-eabi-objdump -h build/firmware-r5.elf
```

```text
build/firmware-r5.elf:     file format elf32-littlearm

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .vectors      000000c8  00000000  00000000  00001000  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .text         00000078  3ed00000  3ed00000  00002000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .rodata       00000009  3ed00078  3ed00078  00002078  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .ARM.attributes 00000031  3ed00081  3ed00081  00002081  2**0
                  CONTENTS, READONLY
  4 .bss          00000003  3ed00081  3ed00081  00002081  2**0
                  ALLOC
  5 .heap         0000200c  3ed00084  3ed00084  00002081  2**0
                  ALLOC
  6 .stack        00003800  3ed02090  3ed02090  00002081  2**0
                  ALLOC
  7 .shm          00000000  3ee00000  3ee00000  000020b2  2**0
                  CONTENTS
  8 .comment      00000026  00000000  00000000  000020b2  2**0
                  CONTENTS, READONLY
```

```bash
arm-none-eabi-readelf -l build/firmware-r5.elf
```

```text
Elf file type is EXEC (Executable file)
Entry point 0x20
There are 2 program headers, starting at offset 52

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00000000 0x00000000 0x000c8 0x000c8 R E 0x1000
  LOAD           0x002000 0x3ed00000 0x3ed00000 0x00081 0x05890 RWE 0x1000

 Section to Segment mapping:
  Segment Sections...
   00     .vectors
   01     .text .rodata .bss .heap .stack
```

En el segmento `01`, `FileSiz` (`0x81`) es lo que el loader copia del archivo (`.text` + `.rodata`), y `MemSiz` (`0x5890`) es lo que ocupa en memoria. La diferencia es `.bss`, heap y stack. `remoteproc` rellena esa diferencia con ceros; `xsdb dow` no lo hace por defecto (tiene la opción `-clear`). Por eso `_boot` pone `.bss` en cero siempre, sin depender del loader.

Al finalizar estas escrituras, se libera el reset del core R5. El core ejecuta primero la entrada `0x00` de la tabla (`b _boot`), que lo lleva a `0x20`, y desde ahí ejecuta secuencialmente las instrucciones de `_boot`:

```text
ATCM
0x00  b _boot          ┐
0x04  b undef_handler  │ tabla de vectores (8 instrucciones)
...                    │
0x1C  b fiq_handler    ┘
0x20  cps #19          ┐ _boot: empieza donde termina la tabla
0x24  ldr sp, [...]    │   paso 1: SP de cada modo
...                    │
0x48  cps #31          │   paso 2: modo System + _stack
0x50  mrc/mcr/vmsr     │   FPU
0x68  ldr r0/r1...     │   paso 3: .bss en cero
0x74  bss_loop         │
0x80  bl __main_veneer │   paso 4: saltar a main
0x84  b hang           ┘   paso 5: por si main retorna
0x88  .word 0x3ED04C90   literal pool: direcciones de los stacks
...
0xC0  __main_veneer      ldr pc, =0x3ED00000  (lo agregó el linker)
```

Estas instrucciones se encargan, por ejemplo, de poner en cero las variables no inicializadas (`.bss`) y de cargar los stack pointers de cada modo del procesador. Cada modo (System, IRQ, Undefined, etc.) tiene un sub-stack dentro de la región de stack, donde guarda variables locales y registros salvados. Por ejemplo, el modo IRQ se activa cuando ocurre una interrupción de algún periférico. Al entrar al handler, el core guarda la dirección de retorno en `LR_irq` (un registro *banked*, no el stack), y el handler usa el sub-stack de IRQ para sus variables locales y para salvar registros (incluido `LR_irq`, si llama a otra función).

> **Nota:** a diferencia de lo que sucede en un Cortex-M4 (un STM32, por ejemplo), acá el loader escribe el programa directamente en RAM (TCM y DDR, a partir de `0x3ED00000`; dirección y tamaño reservados en el device tree `system-user.dtsi` de PetaLinux) con los valores finales. En el M4 el programa se graba en la flash no volátil antes de liberar el core; como las variables tienen que poder modificarse, el startup debe copiar los valores iniciales de `.data` de flash a RAM en cada encendido. Acá eso no es necesario, porque el loader ya escribió `.data` en RAM.

### Detalles del arranque (ARM R5)

- **Alcance de `B`:** el offset es un inmediato de 24 bits con signo, multiplicado por 4 (instrucciones alineadas), así que el rango es ±32 MB alrededor de la instrucción. Desde `0x0` no alcanza `0x3ED0_0000` (~1005 MB), así que para `bl main` el linker insertó automáticamente `__main_veneer` en la ATCM (`ldr pc, =0x3ED00000`, dirección absoluta de 32 bits).
- **`--gc-sections` y `ENTRY`:** el linker descarta el código que no es alcanzable desde el símbolo de entrada (`ENTRY(_boot)`) ni está marcado con `KEEP`. Sin un `_boot` definido, nada llegaba a `main` y `.text` quedaba vacía.
- **Traducción de direcciones en `remoteproc`:** el ELF usa direcciones vistas por el R5. El driver, que corre en el A53, las traduce a la vista global: `0x0` (ATCM) se escribe en `0xFFE0_0000`; la DDR (`0x3ED0_0000`) se ve igual desde ambos. Además, `remoteproc` elige LOVEC o HIVEC según el *entry point* del ELF y se lo pide al PMUFW.

## Información sobre registros del core ARM versus registros del SoC

- Tiene dirección de memoria (como `0xFF9A_0100`): es del SoC y está en el UG1087.
- Se accede con `mrc`/`mcr` (CP15) o es un registro del core (`CPSR`, `r0`–`r15`): es de ARM y está en los manuales de ARM.

## Esquema de memoria implementado en este caso

| Region                  | Start         | End           | Size    | Notes                                                         |
| ----------------------- | ------------- | ------------- | ------- | ------------------------------------------------------------- |
| OCM                     | `0xFFFC_0000` | `0xFFFF_FFFF` | 256 KB  | High vectors at `0xFFFF_0000`                                 |
| TCM (global view)       | `0xFFE0_0000` | `0xFFFB_FFFF` | 1.75 MB | TCM banks only use `0xFFE0_0000`–`0xFFEB_FFFF`; rest reserved |
| LPD peripherals         | `0xFF00_0000` | `0xFFDF_FFFF` | 14 MB   | UART, TTC, SCNTRS, IPI, RPU control                           |
| FPD peripherals / other | `0xFD00_0000` | `0xFEFF_FFFF` | 32 MB   |                                                               |
| RPU GIC                 | `0xF900_0000` | `0xFCFF_FFFF` | 64 MB   | The GIC only uses the start of this window                    |
| QSPI, PCIe, CoreSight   | `0xC000_0000` | `0xF8FF_FFFF` | 912 MB  |                                                               |
| PL                      | `0x8000_0000` | `0xBFFF_FFFF` | 1 GB    |                                                               |
| DDR low                 | `0x0000_0000` | `0x7FFF_FFFF` | 2 GB    | Hidden below `0x0001_0000` by the ATCM (R5 local view)        |
| ↳ shm                   | `0x3EE0_0000` | `0x3EEF_FFFF` | 1 MB    | `.shm`                                                        |
| ↳ R5 code/data          | `0x3ED0_0000` | `0x3EDF_FFFF` | 1 MB    | `.text`, `.data`, `.bss`, heap, stacks                        |
| ATCM (local view)       | `0x0000_0000` | `0x0000_FFFF` | 64 KB   | Low vectors, `.vectors` + `.boot`, `.bootdata`                |

Addresses written from memory; verify against UG1085 (System Address Map) and UG1087.

<!-- ```text
   0xFFFF_FFFF ┌──────────────────────────────┐
               │ OCM (256 KB)                 │ ← 0xFFFF_0000: vectores altos (VINITHI=1)
   0xFFFC_0000 ├──────────────────────────────┤
               │ TCM, vista global            │   R5_0 ATCM en 0xFFE0_0000
   0xFFE0_0000 │ (para el A53 / JTAG)         │   (misma RAM que 0x0 local)
               ├──────────────────────────────┤
               │ Periféricos LPD              │   UART0  0xFF00_0000
               │                              │   UART1  0xFF01_0000  (Linux)
               │                              │   TTC0   0xFF11_0000
               │                              │   SCNTRS 0xFF26_0000
               │                              │   IPI    0xFF30_0000
               │                              │   RPU    0xFF9A_0000  (RPU_0_CFG)
   0xFF00_0000 ├──────────────────────────────┤
               │ Periféricos FPD / otros      │
   0xFD00_0000 ├──────────────────────────────┤
               │ GIC del RPU                  │   0xF900_0000
   0xF900_0000 ├──────────────────────────────┤
               │ QSPI, PCIe, CoreSight...     │
   0xC000_0000 ├──────────────────────────────┤
               │ PL (lógica programable)      │
   0x8000_0000 ├──────────────────────────────┤
               │ DDR baja (hasta 2 GB)        │
               │  ┌────────────────────────┐  │
               │  │ 0x3EE0_0000  shm (1MB) │  │ ← .shm
               │  ├────────────────────────┤  │
               │  │ 0x3ED0_0000  R5 (1MB)  │  │ ← .text .data .bss stack heap
               │  └────────────────────────┘  │   (reservado en el device tree de Linux)
               │                              │
               ├──────────────────────────────┤
               │ ATCM (64 KB), vista local    │ ← .vectors (+ .boot), .bootdata
   0x0000_0000 └──────────────────────────────┘   0x0: vectores bajos (VINITHI=0)
``` -->

> **Ojo:** el diagrama mezcla dos vistas. En el mapa global, `0x0` es DDR. La ATCM en `0x0` existe solo para el R5: el core resuelve esos accesos por sus puertos TCM privados, sin salir al bus, así que para él los primeros 64 KB de DDR quedan tapados.

## Secciones de memoria de un programa en C compilado para arquitectura ARM

| Sección   | Qué contiene                                   | ¿Está en el ELF?             | Permisos      |
| --------- | ---------------------------------------------- | ---------------------------- | ------------- |
| `.text`   | Código (instrucciones)                         | Sí                           | lectura/ejec. |
| `.rodata` | Constantes: `const`, strings literales         | Sí                           | lectura       |
| `.data`   | Globales/`static` **con** valor inicial ≠ 0    | Sí (los valores iniciales)   | lectura/escr. |
| `.bss`    | Globales/`static` **sin** valor inicial o en 0 | **No**: solo inicio y tamaño | lectura/escr. |
| heap      | Memoria de `malloc`                            | No (reserva de espacio)      | lectura/escr. |
| stack     | Locales, argumentos, direcciones de retorno    | No (reserva de espacio)      | lectura/escr. |

- `.bss` no guarda los ceros en el ELF: alguien tiene que escribirlos en RAM antes de `main` (en bare metal, `_boot`).
- El stack es solo espacio reservado: hasta que `_boot` carga `SP`, el core no sabe que existe.
- En `objdump -h`, `.bss`, `.heap` y `.stack` aparecen como `ALLOC` sin `CONTENTS`.

## Secuencia de `_boot` de `start.S`

1. Cargar el `SP` de cada modo de excepción (`cps` + `ldr sp`): SVC, IRQ, FIQ, ABT, UND.
2. Pasar a modo System y cargar `_stack` (el stack de `main`).
3. Poner `.bss` en cero, de `__bss_start__` a `__bss_end__`.
4. `bl main`.
5. `b .` (o un loop con `wfi`) por si `main` retorna.

- `mov`/`ldr sp` solo escribe el `SP` del modo actual (`SP` está *banked*): hay que estar en el modo para cargar su stack.
- Los símbolos de stack del linker script son la dirección **alta**: el stack crece hacia abajo.
- `main` corre en System y no en SVC: una excepción `svc` pisaría `LR_svc`, que sería el `LR` de `main`.
- Aun sin interrupciones pueden activarse SVC, Abort y Undefined, así que sus stacks tienen que estar cargados.

## Herramientas para analizar el binario ELF

| Pregunta                                       | Comando                                                          |
| ---------------------------------------------- | ---------------------------------------------------------------- |
| ¿Qué tipo de archivo es y dónde arranca?       | `arm-none-eabi-readelf -h build/firmware-r5.elf` (`Entry point`) |
| ¿Qué secciones hay, dónde van y cuánto ocupan? | `arm-none-eabi-objdump -h build/firmware-r5.elf`                 |
| ¿Qué bloques copia el loader?                  | `arm-none-eabi-readelf -l build/firmware-r5.elf` (filas `LOAD`)  |
| ¿Dónde quedó cada función/variable?            | `arm-none-eabi-nm -n build/firmware-r5.elf`                      |
| ¿Qué es lo que más ocupa?                      | `arm-none-eabi-nm -S --size-sort build/firmware-r5.elf`          |
| ¿Qué instrucciones hay en una sección?         | `arm-none-eabi-objdump -d -j .vectors build/firmware-r5.elf`     |
| ¿Qué bytes hay en una sección de datos?        | `arm-none-eabi-objdump -s -j .rodata build/firmware-r5.elf`      |
| Resumen de tamaños                             | `arm-none-eabi-size -A build/firmware-r5.elf`                    |

- `build/firmware-r5.map` (generado por `-Map`): qué puso el linker en cada lugar, de qué `.o` vino y qué descartó `--gc-sections`.
- `objdump -h` muestra **secciones** (vista del linker); `readelf -l` muestra **segmentos** (vista del loader).
