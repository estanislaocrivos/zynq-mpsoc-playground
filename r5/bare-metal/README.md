# Bare metal application for R5 core

## Build the application

```bash
./scripts/build.sh
```

## Running the application

### Running through `xsdb`

To run the application through `xsdb`, follow these steps:

1. Open `xsdb` and connect to the target:

    ```bash
    xsdb
    connect
    ```

2. Load the application onto the R5:

    ```bash
    dow build/<application>.elf
    ```

    <!-- If TCM bank is powered down, the debugger can't write the .vectors section to address 0x0. In your XSDB session, before dow, you need to power on the TCM banks. Try this sequence:

    ```bash
    targets -set -nocase -filter {name =~ "PSU"}
    mwr 0xFF9A0108 0x80000000
    targets -set -nocase -filter {name =~ "*R5*0"}
    rst -proc
    ```

    0xFF9A0108 is the PMU_GLOBAL_RAM_RETAIN_DP register — writing 0x80000000 enables the RPU TCM power domain. Then rst -proc resets just the R5 core (not the whole system) so it's in a clean state for the download. If that register write isn't enough, the full recovery path is:

    ```bash
    targets -set -nocase -filter {name =~ "PSU"}
    rst -system
    source <path-to>/psu_init.tcl
    psu_init
    targets -set -nocase -filter {name =~ "*R5*0"}
    rst -proc
    dow <elf>
    con
    ``` -->

3. Run the application:

    ```bash
    con
    ```

4. To stop the application, use the following command:

    ```bash
    stop
    ```
