# Cortex-R5 (ZynqMP) - Notas

El Cortex-R5 es un procesador ARMv7 de doble núcleo que forma parte del Kria SoM K26. En este archivo se recogen notas sobre los detalles de más bajo nivel en cuanto al arranque del SoC y cada uno de los cores, los bancos de memoria disponibles, entre otros.

## Regiones de memoria disponibles para el procesador

El core R5 puede acceder a tres tipos de memoria:

| Tipo de memoria              | Dirección (vista R5)                   | Tamaño                                                                                                                                                                                                                                                            | Cacheable                               | Descripción                                                                                                                                                                    |
| ---------------------------- | -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| TCM (Tightly Coupled Memory) | ATCM `0x0000_0000`, BTCM `0x0002_0000` | 64 KB por banco y por core (128 KB por banco en lockstep)                                                                                                                                                                                                         | No (no lo necesita: acceso en un ciclo) | SRAM conectada por puertos dedicados del core, sin interconnect ni caché: tiempo determinista. El acceso rápido es exclusivo de su core; el A53 y JTAG la ven en `0xFFE0_0000` |
| OCM (On-Chip Memory)         | `0xFFFC_0000`–`0xFFFF_FFFF`            | 256 KB                                                                                                                                                                                                                                                            | Sí, configurable (MPU + `SCTLR.C`/`I`)  | SRAM compartida en el interconnect del LPD (A53, R5, PMU, DMA, PL). La usan el boot ROM, el FSBL y el ATF: no pisarla                                                          |
| DDR (baja)                   | `0x0000_0000`–`0x7FFF_FFFF`            | 2 GB visibles para el R5 (el SOM K26 tiene 4 GB: la otra mitad está en la DDR alta, `0x8_0000_0000`, fuera del alcance de 32 bits del R5). En este proyecto: 1 MB de código/datos en `0x3ED0_0000` + 1 MB de `shm` en `0x3EE0_0000`, reservados en el device tree | Sí, configurable (MPU + `SCTLR.C`/`I`)  | Memoria de programa y de propósito general; `shm` para compartir datos con el A53. Misma dirección física para todos los masters; para el R5, los rangos de su TCM la tapan    |

Hoy `start.S` no habilita cachés ni MPU, así que no se cachea nada. Las direcciones de la tabla son **físicas** (las del bus del SoC), salvo la TCM, que el R5 ve en su vista local. Las direcciones que ve Linux son direcciones virtuales, traducidas por la MMU del A53.

### Ubicación de los vectores de excepción

El bit `VINITHI` del registro `RPU0_CFG` (SLCR) determinan dónde busca el core los vectores de excepción, es decir, si el core va a usar la OCM (`VINITHI = 1`, HIVEC) o la ATCM (`VINITHI = 0`, LOVEC) para almacenar los vectores (direcciones) de las excepciones (reset, IRQ, FIQ, etc.). El core copia el valor de `VINITHI` en el bit `V` del registro `SCTLR` del core R5. La diferencia es que `VINITHI` solo se lee en el reset, mientras que `SCTLR.V` se consulta en cada excepción.

- `VINITHI` es un **pin** del core, manejado por el SoC a través de `RPU0_CFG` (SLCR). El core lo muestrea **solo en el reset** y copia su valor en `SCTLR.V`.
- `SCTLR.V` (bit 13 del registro CP15 `SCTLR`) es lo que el core consulta **en cada excepción**. El software puede cambiarlo después del reset.
- AMD recomienda quedarse en LOVEC (`VINITHI = 0`, `SCTLR.V = 0`) ya que de esta forma los vectores de excepción se almacenan en TCM. Con HIVEC cada excepción busca su vector en la OCM, con más latencia y jitter.

## Cachés, MPU y comunicación con el A53

Verificado con XSDB (`rrd cp15 c1`), corriendo este firmware por `remoteproc` tras un power-cycle:

| Firmware                | `SCTLR`      | `M` (MPU) | `C` (caché datos) | `I` (caché instr.) | `V` (vectores) |
| ----------------------- | ------------ | --------- | ----------------- | ------------------ | -------------- |
| Este (`start.S` propio) | `0x00E50878` | 0         | 0                 | 0                  | 0 (LOVEC)      |

Qué implica tenerlos apagados:

1. **No hay datos viejos en caché.** El R5 lee y escribe directo en memoria, y del lado del A53 UIO mapea la `shm` sin caché.
2. **Sin caché no significa sin reordenamiento.** Una escritura puede quedar un rato en un buffer del camino a memoria. Si el R5 escribe un mensaje en `shm` y enseguida dispara la IPI, el A53 podría leer el mensaje incompleto. Regla: poner una barrera (`dsb`) entre "escribir los datos" y "avisar".
3. **MPU apagado = sin restricciones del R5, pero no del SoC.** Corriendo en modo System (privilegiado) no hay regiones que bloqueen. Aun así, el ZynqMP tiene protección a nivel sistema, fuera del core.
4. **Accesos desalineados.** En memoria de tipo *device* / *strongly-ordered* un acceso desalineado genera fault aunque `SCTLR.A = 0`. Pendiente: verificar qué tipo de memoria usa el R5 para datos con `SCTLR.M = 0` (ARMv7 ARM, B5). Del lado del A53, un `memcpy` sobre memoria UIO sin caché puede dar `SIGBUS` por lo mismo.
5. **Costo: velocidad.** Sin caché, cada acceso a DDR cruza el interconnect.

## IPI: canales y message buffers

### Qué impone el hardware

- El IPI tiene **11 canales** (0 a 10). El canal es la identidad: el hardware no sabe qué core está detrás, solo qué canal escribe.
- Cada canal tiene su bloque de registros (`TRIG`, `OBS`, `ISR`, `IMR`, `IER`, `IDR`) y **un bit** en las máscaras de `TRIG`/`OBS`/`ISR` de todos los canales.
- La RAM de message buffers es una SRAM en el LPD: `0xFF99_0000`–`0xFF99_0FFF` (4 KB). Se organiza en 8 bloques (uno por *índice de buffer*) × 8 slots (uno por índice de destino) × 0x40 (32 B request + 32 B response).
- El índice de buffer **no es** el número de canal. Los canales 3 a 6 (PMU) comparten el índice 7.
- El hardware no restringe quién escribe dónde dentro de esa RAM, salvo que la XPPU esté configurada para hacerlo.

| Canal | Agente por defecto | Registros      | Bit           | Índice buffer | Bloque buffers | En este diseño |
| ----- | ------------------ | -------------- | ------------- | ------------- | -------------- | -------------- |
| 0     | APU                | `0xFF30_0000`  | `0x00000001`  | 2             | `0xFF99_0400`  | Linux ↔ PMUFW  |
| 1     | RPU0               | `0xFF31_0000`  | `0x00000100`  | 0             | `0xFF99_0000`  | este R5        |
| 2     | RPU1               | `0xFF32_0000`  | `0x00000200`  | 1             | `0xFF99_0200`  | app A53 (UIO)  |
| 3–6   | PMU                | `0xFF33_0000`… | `0x00010000`… | 7             | `0xFF99_0E00`  | PMUFW          |
| 7     | PL0                | `0xFF34_0000`  | `0x01000000`  | 3             | `0xFF99_0600`  | libre          |
| 8     | PL1                | `0xFF35_0000`  | `0x02000000`  | 4             | `0xFF99_0800`  | libre          |
| 9     | PL2                | `0xFF36_0000`  | `0x04000000`  | 5             | `0xFF99_0A00`  | libre          |
| 10    | PL3                | `0xFF37_0000`  | `0x08000000`  | 6             | `0xFF99_0C00`  | libre          |

"Agente por defecto" es el nombre que usa UG1085. No es una restricción: la app del A53 usa el canal "de RPU1".

### Qué es convención (XIpiPsu, Linux mailbox)

- Request y response de un intercambio viven en el **bloque del que inicia**, en el **slot del que responde**:

  ```
  request  = 0xFF99_0000 + idx(inicia) * 0x200 + idx(responde) * 0x40
  response = request + 0x20
  ```

- El que inicia escribe el request y el que responde escribe el response. Cada buffer tiene un solo escritor.
- Por eso hay **dos slots por par de canales**: uno en cada bloque. Si los dos inician a la vez, cada request queda en un lugar distinto y no se pisan.
- El slot propio (bloque *i*, slot *i*) no se usa: nadie se manda un request a sí mismo.
- En bare-metal nada impide escribir en otro lugar (por ejemplo en `0xFF99_0000`), pero cualquier código que siga la convención (`XIpiPsu_GetBufferAddress()`, el driver de mailbox de Linux) va a buscar el mensaje en otra dirección.

## Secuencia de boot del ZynqMP (PMU → CSU → FSBL → ATF → U-Boot → Linux)

El SoC tiene más procesadores que los A53 y los R5:

| Procesador            | Qué es                                                   | Qué firmware corre                    |
| --------------------- | -------------------------------------------------------- | ------------------------------------- |
| PMU                   | MicroBlaze triplicado (redundancia), en el LPD           | PMU ROM, y después el **PMUFW**       |
| CSU                   | MicroBlaze triplicado, unidad de configuración/seguridad | CSU BootROM (fijo en silicio)         |
| A53_0 (habitual) o R5 | Core de aplicación                                       | **FSBL**, después ATF, U-Boot y Linux |

1. Encendido   → PMU ROM (en el PMU): inicializa lo mínimo y libera la CSU.
2. CSU BootROM → lee los pines de boot mode, encuentra BOOT.BIN (SD/QSPI),
                 carga el PMUFW en la RAM del PMU y el FSBL en la OCM,
                 y libera el A53_0.
3. FSBL        → (en el A53_0, ejecutando desde la OCM) inicializa el PS:
                 clocks, MIO, DDR (psu_init). Carga el bitstream en la PL,
                 el ATF, U-Boot y, si hay, apps para el R5. Configura VINITHI.
4. ATF → U-Boot → Linux (en los A53).
   PMUFW       → queda residente en el PMU, atendiendo pedidos EEMI.

El FSBL corre desde la **OCM** porque la DDR todavía no está inicializada. La OCM es SRAM on-chip y funciona sin configurar nada (por esto es que se prefiere la parte baja (TCM) para el firmware del R5, evitando pisar la OCM).

### Pre-FSBL: del POR a la CSU

Todo lo previo al FSBL es código **fijo en el silicio** (ROM): no se configura ni se actualiza.

1. **POR (Power-On Reset):** al encender, el PMU sale de reset y ejecuta el **PMU ROM**.
2. **PMU ROM** (tareas pre-boot, UG1085 cap. 11): inicializa el MicroBlaze, limpia LPD/FPD, inicializa el System Monitor, configura y valida PLLs, pone en cero la RAM del PMU, valida la alimentación, repara memorias del FPD si hace falta, corre el self-test de memorias, apaga los IPs deshabilitados y **libera la CSU** (o entra en estado de error). Después entra en *service mode*, esperando pedidos.
3. **CSU BootROM** (en la CSU): inicializa la OCM, lee el **boot mode** capturado de los pines en el POR (`CRL_APB.BOOT_MODE_USER`, `0xFF5E0200`), busca el `BOOT.BIN`, lo autentica/descifra si el boot es seguro y carga el **FSBL en la OCM** y el **PMUFW en la RAM del PMU**. Libera el core indicado en el boot header (`destination_cpu` en `bootgen.bif`).
4. Con el PMUFW cargado, el PMU pasa del PMU ROM al **PMUFW**, que queda residente.

| Programa    | Dónde corre | ¿Se puede cambiar?                       | Rol                                             |
| ----------- | ----------- | ---------------------------------------- | ----------------------------------------------- |
| PMU ROM     | PMU         | No (silicio)                             | Arranque mínimo, liberar la CSU                 |
| CSU BootROM | CSU         | No (silicio)                             | Elegir boot mode, cargar y verificar `BOOT.BIN` |
| PMUFW       | PMU         | Sí (`BOOT.BIN`, compilado por PetaLinux) | Energía, resets y nodos en runtime (EEMI)       |
| FSBL        | A53_0 o R5  | Sí (`BOOT.BIN`, compilado por PetaLinux) | `psu_init`, cargar el resto de `BOOT.BIN`       |

La **CSU** (*Configuration Security Unit*) es, además de su MicroBlaze, el bloque de seguridad (AES-GCM, RSA, SHA-3, claves en eFuse/BBRAM, anti-tamper) y el **PCAP**, la puerta para programar la PL. Después del boot sigue como servicio de cripto y de carga de bitstreams. No aparece en `targets` de XSDB.

**Por qué hay dos etapas (FSBL y U-Boot):** el FSBL vive en la OCM (256 KB) y solo sabe leer `BOOT.BIN`. U-Boot corre en DDR, entiende sistemas de archivos, red y USB, y tiene consola y scripts; así el kernel se puede cambiar sin tocar `BOOT.BIN`.

### Boot firmware del Kria (QSPI A/B)

- El SOM K26 tiene los pines de boot mode fijos en **QSPI**: `BOOT_MODE_USER[3:0] = 0x2` (QSPI32). Códigos: `0x0` JTAG, `0x1` QSPI24, `0x2` QSPI32, `0x3` SD0, `0x5` SD1, `0x6` eMMC, `0xE` SD1-LS.
- El arranque está dividido:

| Qué                                                           | Dónde vive                                  | Cómo se actualiza                    |
| ------------------------------------------------------------- | ------------------------------------------- | ------------------------------------ |
| `BOOT.BIN` (FSBL, PMUFW, ATF, `system.dtb` de U-Boot, U-Boot) | **QSPI del SOM**, dos copias: **A** y **B** | `xmutil bootfw_update` (ver abajo)   |
| `boot.scr`, `image.ub` (kernel + DT + ramdisk), rootfs        | **SD**                                      | Copiar archivos / regrabar la imagen |

- Cambios en el device tree o el kernel se ven con solo actualizar la SD. Cambios en FSBL o PMUFW **requieren grabar `BOOT.BIN` en la QSPI**.
- Imagen activa en esta placa: **B** (actualizada con un `BOOT.BIN` de PetaLinux 2025.1).

Cómo actualizar el boot firmware desde PetaLinux:

```bash
sudo xmutil bootfw_status               # Imágenes A/B, cuál está activa y cuál se pidió
sudo xmutil bootfw_update -i ~/BOOT.BIN # Graba la imagen inactiva y la marca para el próximo boot
sudo reboot

# Solo correr este comando si bootea correctamente. Sino, apagar y prender la placa para que bootee de la otra imagen anterior
sudo xmutil bootfw_update -v
```

### Energía del RPU: PMUFW, remoteproc y XSDB

- El **PMUFW** es el dueño de la energía: los demás masters le **piden** nodos (EEMI `request_node` / `release_node`). IDs en el dtsi: `7` = RPU_0, `15` = ATCM0, `16` = BTCM0.
- Sin firmware corriendo, el RPU queda apagado (`targets` en XSDB muestra `Cortex-R5 #0 (No Power)` y `dow` falla con `The core is powered down`). Dos mecanismos lo apagan:
  1. El PMUFW en su init, si el `BOOT.BIN` no trae app para el R5 (opción de compilación en `xpfw_config.h`).
  2. Linux llama a `PM_INIT_FINALIZE` al final del boot y el PMUFW apaga los nodos que ningún master pidió.
- `remoteproc` pide los nodos al arrancar un firmware y los libera al pararlo. XSDB no puede prender el core. Si XSDB toma el RPU, después remoteproc falla con `Unable to request node 7` hasta un power-cycle.
- Para inspeccionar con XSDB sin pelear con el PMUFW: arrancar el ELF con `remoteproc` y después, en XSDB, solo `stop` + `rrd` (sin `rst`).
- Las cachés del R5 (`SCTLR.C`/`I`) y el MPU (`SCTLR.M`) solo pueden configurarse desde el propio R5: Linux y el device tree no los tocan. Del lado del A53, `no-map` y el `mmap` de `generic-uio` dejan la `shm` sin caché.

## De Linux al R5: qué hace `remoteproc`

Al ejecutar `echo start > /sys/class/remoteproc/remoteproc0/state` (con el nombre del ELF en `.../firmware` y el archivo en `/lib/firmware/`), en orden:

| #   | Quién                          | Qué hace                                                                                                                                                       | Evidencia                           |
| --- | ------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| 1   | Linux (driver) → PMUFW         | Pide los nodos RPU_0 (`7`), ATCM (`15`) y BTCM (`16`). El PMUFW los enciende; el core queda **en reset**. La TCM tiene que estar encendida antes de escribirla | `power-domains` en el dtsi          |
| 2   | Linux → PMUFW                  | Configura el modo del cluster: split o lockstep *(verificar en qué momento exacto)*                                                                            | `cluster-mode = <0>`, `SLSPLIT = 1` |
| 3   | Linux                          | Mapea las memorias del R5: TCM por su vista global y la DDR reservada (`memory-region`)                                                                        | `ranges` del dtsi (`0xFFE0_0000`)   |
| 4   | Linux                          | Lee el ELF y copia cada segmento `LOAD`, traduciendo direcciones (`0x0` → `0xFFE0_0000`). Rellena con ceros `MemSiz − FileSiz`                                 | `readelf -l`                        |
| 5   | Linux                          | Mira el *entry point* del ELF (`0x20`) y elige LOVEC (si fuera ≥ `0xFFFC_0000`, HIVEC)                                                                         | `e_entry`                           |
| 6   | Linux → PMUFW (`request_wake`) | El PMUFW pone `VINITHI` según lo pedido y **libera el reset** del R5 (`nCPUHALT = 1` y salida de reset en `CRL_APB`) *(verificar el registro exacto)*          | `RPU0_CFG = 0x00000001`             |
| 7   | R5                             | Sale de reset (ver abajo) y busca su primera instrucción en `0x0`: `b _boot`                                                                                   | `SCTLR = 0x00E50878`                |

El core se mantiene en reset durante toda la escritura: si corriera antes, ejecutaría código a medio copiar.

Para verificar los pasos marcados: `zynqmp_r5_rproc_prepare` y `zynqmp_r5_rproc_start` en [`drivers/remoteproc/xlnx_r5_remoteproc.c`](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/xlnx_r5_remoteproc.c), y la sección de resets del RPU en el UG1085.

### Estado del core al salir de reset

| Qué                     | Valor                                           | Verificado con                        |
| ----------------------- | ----------------------------------------------- | ------------------------------------- |
| Modo                    | Supervisor (SVC)                                | (`_boot` lo cambia a System)          |
| IRQ / FIQ / abort asín. | Enmascaradas (`CPSR` bits 7, 6 y 8 en 1)        | `CPSR = 0x600001DF` (ya en System)    |
| Estado de instrucciones | ARM (no Thumb; `SCTLR.TE = 0`)                  | `SCTLR`                               |
| MPU / cachés            | Apagados (`SCTLR.M`, `C`, `I` en 0)             | `SCTLR = 0x00E50878`                  |
| Vectores                | Según `VINITHI` (LOVEC en este flujo)           | `SCTLR.V = 0`, `RPU0_CFG.VINITHI = 0` |
| `SP` y demás registros  | Indefinidos: `_boot` tiene que cargar cada `SP` | —                                     |

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

```asm
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

Estas instrucciones se encargan, por ejemplo, de poner en cero las variables no inicializadas (`.bss`) y de cargar los stack pointers de cada modo del procesador. Cada modo (System, IRQ, Undefined, etc.) tiene un sub-stack dentro de la región de stack, donde guarda variables locales y registros. Por ejemplo, el modo IRQ se activa cuando ocurre una interrupción de algún periférico. Al entrar a su handler, el core guarda la dirección de retorno en `LR_irq` (un registro *banked*, no el stack), y el handler usa el sub-stack de IRQ para sus variables locales y para salvar registros (incluido `LR_irq`, si llama a otra función).

> **Nota:** a diferencia de lo que sucede en un Cortex-M4 (un STM32, por ejemplo), acá el loader escribe el programa directamente en RAM (TCM y DDR, a partir de `0x3ED00000`; dirección y tamaño reservados en el device tree `system-user.dtsi` de PetaLinux) con los valores finales. En el M4 el programa se graba en la flash no volátil antes de liberar el core; como las variables tienen que poder modificarse, el startup debe copiar los valores iniciales de `.data` de flash a RAM en cada encendido. Acá eso no es necesario, porque el loader ya escribió `.data` en RAM.

### Detalles del arranque (ARM R5)

- **Alcance de la instrucción `b`:** el offset es un inmediato de 24 bits con signo, multiplicado por 4 (instrucciones alineadas), así que el rango es ±32 MB alrededor de la instrucción. Desde `0x0` no alcanza `0x3ED0_0000` (~1005 MB), así que para `bl main` el linker insertó automáticamente `__main_veneer` en la ATCM (`ldr pc, =0x3ED00000`, dirección absoluta de 32 bits).
- **`--gc-sections` y `ENTRY`:** el linker descarta el código que no es alcanzable desde el símbolo de entrada (`ENTRY(_boot)`) ni está marcado con `KEEP`. Sin un `_boot` definido, nada llegaba a `main` y `.text` quedaba vacía.
- **Traducción de direcciones en `remoteproc`:** el ELF usa direcciones vistas por el R5. El driver, que corre en el A53, las traduce a la vista global: `0x0` (ATCM) se escribe en `0xFFE0_0000`; la DDR (`0x3ED0_0000`) se ve igual desde ambos. Además, `remoteproc` elige LOVEC o HIVEC según el *entry point* del ELF y se lo pide al PMUFW.

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
| DDR low                 | `0x0000_0000` | `0x7FFF_FFFF` | 2 GB    | R5 local view: ATCM/BTCM windows hide the DDR underneath      |
| ↳ shm                   | `0x3EE0_0000` | `0x3EEF_FFFF` | 1 MB    | `.shm`                                                        |
| ↳ R5 code/data          | `0x3ED0_0000` | `0x3EDF_FFFF` | 1 MB    | `.text`, `.data`, `.bss`, heap, stacks                        |
| ATCM (local view)       | `0x0000_0000` | `0x0000_FFFF` | 64 KB   | Low vectors, `.vectors` + `.boot`, `.bootdata`                |

Las memorias físicas separadas por tipo (cada bloque es una memoria distinta; las direcciones no tienen por qué ser contiguas entre bloques):

```text
┌─ RPU (dentro del cluster del R5) ──────────────────────────────────────────┐
│  TCM: SRAM privada de cada core, acceso en 1 ciclo, sin caché              │
│                                                                            │
│  ATCM0  64 KB   local R5: 0x0000_0000   global: 0xFFE0_0000                │
│         uso: .vectors + .boot (_boot), literal pool, __main_veneer         │
│                                                                            │
│  BTCM0  64 KB   local R5: 0x0002_0000   global: 0xFFE2_0000                │
│         uso: (libre; habilitada pero sin secciones asignadas)              │
│                                                                            │
│  (R5_1 tiene sus propias ATCM1/BTCM1, globales en 0xFFE9_0000/0xFFEB_0000) │
└────────────────────────────────────────────────────────────────────────────┘

┌─ LPD (on-chip) ────────────────────────────────────────────────────────────┐
│  OCM  256 KB    0xFFFC_0000 – 0xFFFF_FFFF  (misma dirección para todos)    │
│       SRAM compartida (A53, R5, PMU, DMA, PL)                              │
│       uso: FSBL durante el boot, ATF después. Vectores altos (HIVEC) en    │
│            0xFFFF_0000. Este firmware no la usa: no pisarla.               │
└────────────────────────────────────────────────────────────────────────────┘

┌─ DDR4 externa (chips del SOM K26, 4 GB) ───────────────────────────────────┐
│  DDR baja  2 GB   0x0_0000_0000 – 0x0_7FFF_FFFF   (visible para el R5)     │
│    ├─ Linux: kernel, procesos, CMA ("System RAM")                          │
│    ├─ 0x3ED0_0000  1 MB  firmware R5: .text .rodata .data .bss heap stacks │
│    │                     (reserved-memory "ddrboot", no-map)               │
│    └─ 0x3EE0_0000  1 MB  shm A53↔R5                                        │
│                          (reserved-memory "shm_0", no-map; UIO en el A53)  │
│                                                                            │
│  DDR alta  2 GB   0x8_0000_0000 – 0x8_7FFF_FFFF   (solo A53: Linux)        │
└────────────────────────────────────────────────────────────────────────────┘
```

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

## Información sobre registros del core ARM versus registros del SoC

- Si el registro tiene dirección de memoria (como `0xFF9A_0100`): es del SoC y está en el UG1087.
- Si el registro se accede con `mrc`/`mcr` (CP15) o es un registro del core (`CPSR`, `r0`–`r15`): es de ARM y está en los manuales de ARM.


## Referencias

- [UG1085: Zynq UltraScale+ MPSoC Technical Reference Manual](https://docs.amd.com/v/u/en-US/ug1085-zynq-ultrascale-trm)
- [UG1087: Zynq UltraScale+ MPSoC Register Reference Guide](https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/Overview)
- [ARMv7 Architecture Reference Manual](https://developer.arm.com/documentation/ddi0406/latest)
- [embeddedsw: driver `ipipsu`](https://github.com/Xilinx/embeddedsw/tree/master/XilinxProcessorIPLib/drivers/ipipsu/src) (`xipipsu_hw.h`, `xipipsu_buf.c`): layout y cálculo de direcciones de los message buffers.
