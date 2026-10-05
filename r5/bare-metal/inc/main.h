#ifndef MAIN_H
#define MAIN_H

#include "registers.h"

#define DO_NOT_REORDER_GUARD()                \
    do                                        \
    {                                         \
        __asm__ volatile("dsb" ::: "memory"); \
    } while (0)

#endif
