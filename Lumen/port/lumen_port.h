#ifndef LUMEN_PORT_H
#define LUMEN_PORT_H

#include <stdint.h>

/* Bring-up state, readable in the debugger (Live Watch: lumen_diag). */
typedef enum {
    LUMEN_STAGE_NONE = 0,        /* lumen_run() not reached: mx_system_init() failed? */
    LUMEN_STAGE_SYSINIT_FAILED,  /* set by main() when mx_system_init() fails        */
    LUMEN_STAGE_I2C,             /* re-timing / probing I2C1                          */
    LUMEN_STAGE_DISPLAY,         /* sending the SSD1306 init sequence                 */
    LUMEN_STAGE_RUNNING          /* UI loop running; `frames` should keep increasing  */
} lumen_stage_t;

typedef struct {
    volatile uint32_t stage;         /* lumen_stage_t                                 */
    volatile uint32_t sysinit_err;   /* system_status_t from mx_system_init()         */
    volatile uint32_t i2c_kernel_hz; /* I2C1 kernel clock                             */
    volatile uint32_t i2c_timing;    /* TIMINGR value applied                         */
    volatile uint8_t  addr;          /* 7-bit OLED address that ACKed (0 = none)      */
    volatile uint8_t  keys;          /* raw key mask: bit0 UP, bit1 DOWN, bit2 ENTER  */
    volatile uint32_t i2c_ok;        /* successful I2C transfers                      */
    volatile uint32_t i2c_errors;    /* failed I2C transfers (NACK / timeout)         */
    volatile uint32_t frames;        /* UI frames run                                 */
    volatile uint32_t t_run_ms;      /* HAL_GetTick() when lumen_run() started        */
    volatile uint32_t t_display_ms;  /* ... after the SSD1306 init sequence           */
    volatile uint32_t t_first_frame_ms; /* ... after the first UI frame was sent      */
    volatile uint32_t frame_ms;      /* duration of the last frame (render + I2C)     */
    volatile uint32_t frame_ms_max;  /* worst frame so far                            */
    volatile uint8_t  sda_stuck;     /* SDA was held low at boot (recovered)          */
    volatile uint8_t  scl_stuck;     /* SCL held low: wiring / pull-up problem        */
    volatile uint32_t probe_ms;      /* time until the panel ACKed                    */
} lumen_diag_t;

extern lumen_diag_t lumen_diag;

/* Start the Lumen UI. Call after mx_system_init(); never returns. */
void lumen_run(void);

#endif
