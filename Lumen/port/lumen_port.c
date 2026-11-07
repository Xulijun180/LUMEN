/*
 * Lumen port for STM32C532 (CubeMX2 / HAL2), SSD1306 128x64 over I2C,
 * three buttons (UP / DOWN / ENTER, active low with pull-ups).
 *
 * Everything board specific lives in this file:
 *   - frame data          -> LPDMA1 CH0 + I2C1 IRQs, async (see "DMA flush")
 *   - u8x8 byte callback  -> HAL_I2C_MASTER_Transmit on I2C1 (PB8 SCL / PB9 SDA),
 *                            init / contrast / power save only
 *   - u8x8 gpio callback  -> delays only (I2C modules have no RST/DC/CS)
 *   - lm_port_t           -> HAL_GetTick, button pins, (settings storage stub)
 *
 * Bring-up (lumen_run):
 *   1. Bus recovery: SCL is clocked until a panel left mid-transfer by an
 *      MCU-only reset releases SDA (see i2c_bus_recover()).
 *   2. I2C1 is (re)configured at LUMEN_I2C_HZ, overriding CubeMX's TIMINGR.
 *      The SSD1306 datasheet specifies 400 kHz max; faster may work on a
 *      given module but is out of spec.
 *   3. The panel is probed at 0x3C / 0x3D, retrying until it answers.
 *   4. Every step is recorded in `lumen_diag` (watch it in the debugger) and
 *      logged on USART2 (PA2 TX, 115200 8N1).
 *
 * Input: 3-key layout (see lm_input.h)
 *   UP / DOWN       previous / next  (hold to auto-repeat)
 *   ENTER tap       OK
 *   ENTER hold      release after 0.4 s = BACK, keep holding to 1.2 s = home
 *                   (on the home screen: sleep). Any key wakes the screen.
 */
#include "lumen_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "mx_gpio_default.h"
#include "mx_i2c1.h"
#include "mx_usart2.h"
#include "stm32_hal.h"

/* 0 = auto-detect (0x3C, then 0x3D); otherwise a fixed 7-bit address */
#ifndef LUMEN_OLED_ADDR
#define LUMEN_OLED_ADDR 0
#endif
/* I2C1 bus speed used for the panel. Overrides the TIMINGR CubeMX writes in
 * generated/hal/mx_i2c1.c. Set to 0 to keep the CubeMX setting (e.g. after
 * switching I2C1 to Fast mode 400 kHz in CubeMX). SSD1306 max: 400 kHz. */
#ifndef LUMEN_I2C_HZ
#define LUMEN_I2C_HZ 1000000u
#endif
/* per transfer; one is <= 26 bytes (~0.7 ms at 400 kHz), so 5 ms is ample
 * and keeps a misbehaving bus from freezing a frame for long */
#ifndef LUMEN_I2C_TIMEOUT_MS
#define LUMEN_I2C_TIMEOUT_MS 5
#endif
#ifndef LUMEN_FRAME_MS
#define LUMEN_FRAME_MS 10          /* frame period cap (ms) */
#endif
/* 1: frames go out by LPDMA1 CH0 + I2C1 interrupts while the next one renders.
 * 0: blocking u8g2 path (useful to A/B the two on a suspicious panel). */
#ifndef LUMEN_OLED_DMA
#define LUMEN_OLED_DMA 1
#endif
/* a whole frame is ~1.1 KB, ~110 ms even at 100 kHz: beyond this the bus is stuck */
#ifndef LUMEN_FLUSH_TIMEOUT_MS
#define LUMEN_FLUSH_TIMEOUT_MS 150
#endif

lumen_diag_t lumen_diag;           /* inspect in the debugger */

static u8g2_t s_u8g2;
static lm_ui_t s_ui;
static hal_i2c_handle_t *s_i2c;

/* I2C1 pins (as in generated/hal/mx_i2c1.c), used for bus recovery */
#define I2C_SCL_PORT HAL_GPIOB
#define I2C_SCL_PIN  HAL_GPIO_PIN_8
#define I2C_SDA_PORT HAL_GPIOB
#define I2C_SDA_PIN  HAL_GPIO_PIN_9
#define I2C_AF       HAL_GPIO_AF_4

/* printf-style log on USART2. Timeout scales with length (115200 baud =
 * ~87 us/char), so long lines are never cut short. */
static void lumen_log(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    hal_uart_handle_t *u = mx_usart2_uart_gethandle();
    if (u) (void)HAL_UART_Transmit(u, buf, (uint32_t)n, 5u + (uint32_t)n / 8u);
}

/* ================================================================ I2C bring-up */
static void short_delay(void)            /* ~5 us at any core clock: <= 100 kHz bit-bang */
{
    for (volatile uint32_t i = SystemCoreClock / 1000000u; i; i--) {}
}

/* Free a bus left mid-byte. The OLED is not reset with the MCU (debugger
 * reset, re-flash, brown-out), so it can still be driving SDA low, waiting
 * for clocks. Then every transfer stalls for the HAL busy timeout (25 ms),
 * ~100 transfers until the first frame = seconds of black screen. Standard
 * fix: clock SCL up to 9 times until SDA is released, then issue a STOP. */
static void i2c_bus_recover(void)
{
    hal_gpio_config_t g;
    memset(&g, 0, sizeof(g));
    g.mode = HAL_GPIO_MODE_OUTPUT;
    g.output_type = HAL_GPIO_OUTPUT_OPENDRAIN;
    g.pull = HAL_GPIO_PULL_NO;
    g.speed = HAL_GPIO_SPEED_FREQ_LOW;
    g.init_state = HAL_GPIO_PIN_SET;                 /* released (high via pull-ups) */
    HAL_GPIO_Init(I2C_SCL_PORT, I2C_SCL_PIN, &g);
    HAL_GPIO_Init(I2C_SDA_PORT, I2C_SDA_PIN, &g);
    short_delay();
    lumen_diag.sda_stuck = HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) == HAL_GPIO_PIN_RESET;
    lumen_diag.scl_stuck = HAL_GPIO_ReadPin(I2C_SCL_PORT, I2C_SCL_PIN) == HAL_GPIO_PIN_RESET;
    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) == HAL_GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, HAL_GPIO_PIN_RESET); short_delay();
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, HAL_GPIO_PIN_SET);   short_delay();
    }
    /* STOP: SDA rises while SCL is high */
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, HAL_GPIO_PIN_RESET); short_delay();
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, HAL_GPIO_PIN_RESET); short_delay();
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, HAL_GPIO_PIN_SET);   short_delay();
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, HAL_GPIO_PIN_SET);   short_delay();
    g.mode = HAL_GPIO_MODE_ALTERNATE;                /* hand the pins back to I2C1 */
    g.alternate = I2C_AF;
    HAL_GPIO_Init(I2C_SCL_PORT, I2C_SCL_PIN | I2C_SDA_PIN, &g);
}

/* Precomputed TIMINGR values. ST's STM32_UTILS_I2C_ComputeTimingBasic()
 * brute-forces PRESC x SCLDEL x SDADEL x SCLH x SCLL in double precision;
 * the M33 has a single-precision FPU only, so that is soft-float and took
 * seconds at boot. These are its exact results, generated on the host.
 * Add a row if you change the I2C1 kernel clock or the bus speed. */
static const struct { uint32_t kernel_hz, bus_hz, timingr; } I2C_TIMINGS[] = {
    { 144000000u,  100000u, 0x80B25455u },
    { 144000000u,  400000u, 0x20B42D44u },
    { 144000000u, 1000000u, 0x00F03349u },
};

/* (Re)apply the I2C1 config. HAL_I2C_SetConfig() disables and re-enables
 * the peripheral, which also clears any BUSY state left from before. */
static bool i2c_configure(uint32_t hz)
{
    hal_i2c_config_t cfg;
    HAL_I2C_GetConfig(s_i2c, &cfg);
    lumen_diag.i2c_kernel_hz = HAL_I2C_GetClockFreq(s_i2c);
    bool found = (hz == 0);                          /* 0: keep CubeMX's value */
    for (unsigned i = 0; hz && i < sizeof(I2C_TIMINGS) / sizeof(I2C_TIMINGS[0]); i++) {
        if (I2C_TIMINGS[i].kernel_hz == lumen_diag.i2c_kernel_hz && I2C_TIMINGS[i].bus_hz == hz) {
            cfg.timing = I2C_TIMINGS[i].timingr;
            found = true;
        }
    }
    lumen_diag.i2c_timing = cfg.timing;
    /* no table entry: fall back to the CubeMX TIMINGR, report it as a failure */
    return (HAL_I2C_SetConfig(s_i2c, &cfg) == HAL_OK) && found;
}

/* Returns the 7-bit address that ACKs, or 0. HAL2's PollForSlaveReady
 * returns at the first NACK, so retry here: on a cold boot the panel's
 * own reset/boost circuit may need some ms before it answers. Returns as
 * soon as it ACKs, so there is no fixed delay. */
static uint8_t i2c_probe(uint32_t budget_ms)
{
    static const uint8_t cand[] = { 0x3C, 0x3D };
    uint32_t t0 = HAL_GetTick();
    do {
        for (unsigned i = 0; i < sizeof(cand); i++) {
            if (LUMEN_OLED_ADDR && cand[i] != LUMEN_OLED_ADDR) continue;
            if (HAL_I2C_MASTER_PollForSlaveReady(s_i2c, (uint32_t)cand[i] << 1, 2) == HAL_OK)
                return cand[i];
        }
    } while (HAL_GetTick() - t0 < budget_ms);
    return 0;
}

/* ================================================================ DMA flush */
/* Frame data goes out without the CPU: LPDMA1 CH0 feeds I2C1->TXDR and the
 * I2C1 STOP interrupt starts the next transfer, so the next frame renders
 * while this one is on the bus. Register level on purpose: HAL I2C DMA is
 * off in the CubeMX project (USE_HAL_I2C_DMA 0), and generated/ is
 * overwritten on regeneration. Nothing else in the project uses LPDMA1 or
 * the I2C1 IRQs; if CubeMX ever enables them, the handlers below clash at
 * link time instead of misbehaving.
 *
 * Bytes on the wire are the ones u8g2 sends for this driver (ssd1306/sh1106
 * DRAW_TILE): per page, a command transfer [0x00, 0x40, 0x10|x>>4, x&15,
 * 0xB0|page] and a data transfer [0x40, 128 bytes], x = the driver's column
 * offset (0 SSD1306, 2 SH1106). Just in 2 transfers per page instead of 7.
 *
 * Blocking u8g2 commands (contrast, power save) still use byte_cb(), which
 * waits for the chain to finish first, and the I2C1 interrupts are only
 * enabled while a chain runs so the HAL never sees them. */
#define OLED_DMA_CH LPDMA1_CH0

typedef struct {
    uint8_t cmd[5];
    uint8_t data[1 + LM_W];
    uint8_t page;
} oled_pkt_t;

static oled_pkt_t s_pkt[LM_PAGES];
static struct {
    volatile bool busy;
    volatile uint8_t step, steps;        /* 2 transfers per queued page */
    volatile uint8_t failed;             /* pages to resend next flush */
    volatile bool nack;
    uint32_t t0;
} s_fl;

static void dma_stop(void)
{
    DMA_Channel_TypeDef *ch = OLED_DMA_CH;
    if (ch->CCR & DMA_CCR_EN) {
        ch->CCR |= DMA_CCR_SUSP;
        for (uint32_t i = 0; i < 10000u && !(ch->CSR & DMA_CSR_SUSPF); i++) {}
    }
    ch->CCR = DMA_CCR_RESET;
    ch->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF | DMA_CFCR_ULEF |
               DMA_CFCR_USEF | DMA_CFCR_SUSPF | DMA_CFCR_TOF;
}

/* start transfer s_fl.step: DMA first, then START (TXIS requests the bytes) */
static void fl_start_step(void)
{
    const oled_pkt_t *k = &s_pkt[s_fl.step >> 1];
    const uint8_t *src = (s_fl.step & 1) ? k->data : k->cmd;
    uint32_t n = (s_fl.step & 1) ? sizeof(k->data) : sizeof(k->cmd);
    DMA_Channel_TypeDef *ch = OLED_DMA_CH;
    ch->CTR1 = DMA_CTR1_SINC;                         /* bytes, memory++ -> fixed TXDR */
    ch->CTR2 = (uint32_t)LL_LPDMA1_REQUEST_I2C1_TX << DMA_CTR2_REQSEL_Pos;
    ch->CLLR = 0;
    ch->CFCR = DMA_CFCR_TCF;                          /* from the previous transfer */
    ch->CBR1 = n;
    ch->CSAR = (uint32_t)src;
    ch->CDAR = (uint32_t)&I2C1->TXDR;
    ch->CCR = DMA_CCR_EN;
    I2C1->CR2 = ((uint32_t)u8x8_GetI2CAddress(u8g2_GetU8x8(&s_u8g2)) << I2C_CR2_SADD_Pos) |
                (n << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
}

static void fl_finish(void)
{
    I2C1->CR1 &= ~(I2C_CR1_TXDMAEN | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE);
    dma_stop();
    for (uint8_t s = s_fl.step >> 1; s < s_fl.steps >> 1; s++)
        s_fl.failed |= (uint8_t)(1u << s_pkt[s].page);           /* never sent */
    lumen_diag.flush_ms = HAL_GetTick() - s_fl.t0;
    s_fl.busy = false;
}

void I2C1_EV_IRQHandler(void)
{
    uint32_t isr = I2C1->ISR;
    if (isr & I2C_ISR_NACKF) {            /* panel gone: hardware sends STOP itself */
        I2C1->ICR = I2C_ICR_NACKCF;
        s_fl.nack = true;
        dma_stop();
        I2C1->ISR = I2C_ISR_TXE;          /* flush a byte DMA already wrote */
    }
    if (!(isr & I2C_ISR_STOPF)) return;
    I2C1->ICR = I2C_ICR_STOPCF;
    if (s_fl.nack) {
        lumen_diag.i2c_errors++;
        s_fl.nack = false;
        s_fl.failed |= (uint8_t)(1u << s_pkt[s_fl.step >> 1].page);
        s_fl.step = (uint8_t)((s_fl.step | 1u) + 1u);            /* skip the rest of this page */
    } else {
        lumen_diag.i2c_ok++;
        s_fl.step++;
    }
    if (s_fl.step < s_fl.steps) fl_start_step();
    else fl_finish();
}

void I2C1_ERR_IRQHandler(void)            /* bus error / arbitration lost / overrun */
{
    I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF | I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    lumen_diag.i2c_errors++;
    fl_finish();
    I2C1->CR1 &= ~I2C_CR1_PE;             /* software reset: release the bus */
    for (volatile int i = 0; i < 16; i++) {}
    I2C1->CR1 |= I2C_CR1_PE;
}

/* Wait for the running chain. A bus that stops answering mid-transfer (no
 * STOP, no NACK) is abandoned after LUMEN_FLUSH_TIMEOUT_MS, so the UI keeps
 * running on a loose cable. */
static void oled_wait_idle(void)
{
    while (s_fl.busy) {
        if (HAL_GetTick() - s_fl.t0 > LUMEN_FLUSH_TIMEOUT_MS) {
            NVIC_DisableIRQ(I2C1_EV_IRQn);
            NVIC_DisableIRQ(I2C1_ERR_IRQn);
            if (s_fl.busy) {
                lumen_diag.i2c_errors++;
                fl_finish();
                I2C1->CR1 &= ~I2C_CR1_PE;
                for (volatile int i = 0; i < 16; i++) {}
                I2C1->CR1 |= I2C_CR1_PE;
            }
            NVIC_EnableIRQ(I2C1_EV_IRQn);
            NVIC_EnableIRQ(I2C1_ERR_IRQn);
        }
    }
}

/* lm_port_t.flush: copy the dirty pages, start the chain, return */
static void port_flush(void *ctx, const uint8_t *fb, uint8_t pages)
{
    (void)ctx;
    oled_wait_idle();                     /* previous frame still on the bus */
    pages |= s_fl.failed;
    s_fl.failed = 0;
    uint8_t x = u8g2_GetU8x8(&s_u8g2)->x_offset;
    uint8_t n = 0;
    for (uint8_t p = 0; p < LM_PAGES; p++) {
        if (!(pages & (1u << p))) continue;
        oled_pkt_t *k = &s_pkt[n++];
        k->page = p;
        k->cmd[0] = 0x00;                 /* control byte: commands follow */
        k->cmd[1] = 0x40;                 /* display start line 0 */
        k->cmd[2] = (uint8_t)(0x10 | (x >> 4));
        k->cmd[3] = (uint8_t)(x & 15);
        k->cmd[4] = (uint8_t)(0xB0 | p);
        k->data[0] = 0x40;                /* control byte: data follow */
        memcpy(&k->data[1], fb + p * LM_W, LM_W);
    }
    if (!n) return;
    s_fl.step = 0;
    s_fl.steps = (uint8_t)(2 * n);
    s_fl.nack = false;
    s_fl.t0 = HAL_GetTick();
    s_fl.busy = true;
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    I2C1->CR1 |= I2C_CR1_TXDMAEN | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
    fl_start_step();
}

static void oled_dma_init(void)
{
    HAL_RCC_LPDMA1_EnableClock();
    dma_stop();
    NVIC_SetPriority(I2C1_EV_IRQn, 8);    /* handlers are a few us; SysTick is 15 */
    NVIC_SetPriority(I2C1_ERR_IRQn, 8);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ERR_IRQn);
}

/* ================================================================ u8x8 transport */
/* u8g2's SSD13xx I2C layer sends at most 1 control byte + 24 data bytes per
 * transfer; buffer the bytes between START/END and send them in one go. */
static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buf[64];
    static uint8_t n;
    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        break;                               /* I2C1 is set up in lumen_run() */
    case U8X8_MSG_BYTE_START_TRANSFER:
        n = 0;
        break;
    case U8X8_MSG_BYTE_SEND: {
        const uint8_t *p = (const uint8_t *)arg_ptr;
        while (arg_int--) {
            if (n < sizeof(buf)) buf[n++] = *p;
            p++;
        }
        break;
    }
    case U8X8_MSG_BYTE_END_TRANSFER:
        oled_wait_idle();                    /* never interleave with a DMA flush */
        if (HAL_I2C_MASTER_Transmit(s_i2c, u8x8_GetI2CAddress(u8x8), buf, n, LUMEN_I2C_TIMEOUT_MS) == HAL_OK)
            lumen_diag.i2c_ok++;
        else
            lumen_diag.i2c_errors++;         /* keep running: a loose cable must not hang the UI */
        break;
    case U8X8_MSG_BYTE_SET_DC:               /* I2C: D/C is the control byte, nothing to do */
        break;
    default:
        return 0;
    }
    return 1;
}

/* True while u8g2_InitDisplay() runs. The SSD1306 init sequence itself has
 * no delays; the only ones u8g2 issues there are for pulsing a RESET pin
 * (100 + 100 + 100 ms). I2C modules have no such pin, so they are skipped. */
static bool s_skip_reset_delays;

static uint8_t gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8; (void)arg_ptr;
    if (msg == U8X8_MSG_DELAY_MILLI && !s_skip_reset_delays) HAL_Delay(arg_int);
    return 1;                                /* no reset/CS/DC pins on I2C modules */
}

/* ================================================================ lm_port_t */
static uint32_t port_millis(void *ctx) { (void)ctx; return HAL_GetTick(); }

static inline bool pressed(hal_gpio_t port, uint32_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == HAL_GPIO_PIN_RESET;   /* active low */
}

static uint8_t port_keys(void *ctx)
{
    (void)ctx;
    uint8_t m = 0;
    if (pressed(UP_PORT, UP_PIN))       m |= (uint8_t)LM_KEYBIT(LM_KEY_UP);
    if (pressed(DOWN_PORT, DOWN_PIN))   m |= (uint8_t)LM_KEYBIT(LM_KEY_DOWN);
    if (pressed(ENTER_PORT, ENTER_PIN)) m |= (uint8_t)LM_KEYBIT(LM_KEY_ENTER);
    lumen_diag.keys = m;
    return m;
}

/* TODO(settings): persist to the last Flash page. The UI already debounces
 * writes to >= 1 s after the last change, so wear stays low. */
static bool port_load(void *ctx, int32_t *v, int n) { (void)ctx; (void)v; (void)n; return false; }
static void port_save(void *ctx, const int32_t *v, int n) { (void)ctx; (void)v; (void)n; }

/* ================================================================ entry */
void lumen_run(void)
{
    lumen_diag.t_run_ms = HAL_GetTick();             /* = time spent in mx_system_init() */
    s_i2c = mx_i2c1_i2c_gethandle();
    /* First line: if this is garbled, the UART clock/baud is wrong, not Lumen */
    lumen_log("\r\n[lumen] start t=%lu ms  core=%lu Hz  pclk1=%lu Hz  uart2=%lu Hz\r\n",
         (unsigned long)lumen_diag.t_run_ms, (unsigned long)SystemCoreClock,
         (unsigned long)HAL_RCC_GetPCLK1Freq(), (unsigned long)HAL_UART_GetClockFreq(mx_usart2_uart_gethandle()));

    lumen_diag.stage = LUMEN_STAGE_I2C;
    i2c_bus_recover();
    bool cfg_ok = i2c_configure(LUMEN_I2C_HZ);
    uint32_t tp = HAL_GetTick();
    lumen_diag.addr = i2c_probe(300);
    lumen_diag.probe_ms = HAL_GetTick() - tp;
    lumen_log("[lumen] i2c1 %s%lu Hz TIMINGR=0x%08lX  bus %s  oled %s 0x%02X after %lu ms\r\n",
         cfg_ok ? "" : "CONFIG FAILED ", (unsigned long)LUMEN_I2C_HZ, (unsigned long)lumen_diag.i2c_timing,
         lumen_diag.scl_stuck ? "SCL STUCK LOW" : lumen_diag.sda_stuck ? "SDA was stuck, recovered" : "idle",
         lumen_diag.addr ? "ACK" : "NO ACK (check VCC/GND, SCL=PB8, SDA=PB9, pull-ups)",
         lumen_diag.addr, (unsigned long)lumen_diag.probe_ms);
    if (!lumen_diag.addr) lumen_diag.addr = 0x3C;   /* still run the UI; diag shows the failure */

    lumen_diag.stage = LUMEN_STAGE_DISPLAY;
    u8g2_Setup_sh1106_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, byte_cb, gpio_cb);
    u8g2_SetI2CAddress(&s_u8g2, (uint8_t)(lumen_diag.addr << 1));
    s_skip_reset_delays = true;
    u8g2_InitDisplay(&s_u8g2);
    s_skip_reset_delays = false;
    lumen_diag.t_display_ms = HAL_GetTick();
    lumen_log("[lumen] ssd1306 init done t=%lu ms  i2c %lu ok / %lu failed\r\n",
         (unsigned long)lumen_diag.t_display_ms, (unsigned long)lumen_diag.i2c_ok,
         (unsigned long)lumen_diag.i2c_errors);

    lm_port_t port = {0};
    port.u8g2 = &s_u8g2;
    port.millis = port_millis;
    port.read_keys = port_keys;
    port.settings_load = port_load;
    port.settings_save = port_save;
#if LUMEN_OLED_DMA
    oled_dma_init();
    port.flush = port_flush;
#endif
    port.key_layout = LM_LAYOUT_3KEY;
    lm_ui_init(&s_ui, &port, app_defaults, S_COUNT);
    app_start(&s_ui);

    lumen_diag.stage = LUMEN_STAGE_RUNNING;
    for (;;) {
        uint32_t t0 = HAL_GetTick();
        lm_ui_tick(&s_ui);
        uint32_t ft = HAL_GetTick() - t0;
        lumen_diag.frame_ms = ft;
        if (ft > lumen_diag.frame_ms_max) lumen_diag.frame_ms_max = ft;
        uint32_t n = ++lumen_diag.frames;
        if (n == 1) {
            lumen_diag.t_first_frame_ms = HAL_GetTick();
            lumen_log("[lumen] first frame t=%lu ms (took %lu ms)  i2c %lu ok / %lu failed\r\n",
                 (unsigned long)lumen_diag.t_first_frame_ms, (unsigned long)ft,
                 (unsigned long)lumen_diag.i2c_ok, (unsigned long)lumen_diag.i2c_errors);
        } else if (n == 200) {                       /* boot animation is over by now */
            uint32_t span = HAL_GetTick() - lumen_diag.t_first_frame_ms;
            lumen_log("[lumen] 200 frames in %lu ms (avg %lu ms, worst %lu ms, last flush %lu ms)  i2c %lu ok / %lu failed\r\n",
                 (unsigned long)span, (unsigned long)(span / 199u), (unsigned long)lumen_diag.frame_ms_max,
                 (unsigned long)lumen_diag.flush_ms,
                 (unsigned long)lumen_diag.i2c_ok, (unsigned long)lumen_diag.i2c_errors);
        }
        while (HAL_GetTick() - t0 < LUMEN_FRAME_MS) {}   /* frame cap */
    }
}
