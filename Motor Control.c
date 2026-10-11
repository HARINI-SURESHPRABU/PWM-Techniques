 #include <stdio.h>
#include "xparameters.h"
#include "xgpio.h"
#include "xtmrctr.h"
#include "xil_printf.h"
#include "sleep.h"
#include "platform.h"
/*==================================================
 * VERIFIED from xparameters.h:
 * GPIO 0 = 0x40000000 → buttons  (5-bit)
 * GPIO 1 = 0x40010000 → switches (16-bit)
 * GPIO 2 = 0x40020000 → dir_out  (4-bit)
 * TMR 0  = 0x41c00000 → Motor1 PWM
 * TMR 1  = 0x41c10000 → Motor2 PWM
 *==================================================*/
/*--- Button masks ---*/
#define BTN_C    0x01U   /* BTNC N17 - Reset    */
#define BTN_U    0x02U   /* BTNU M18 - Duty+    */
#define BTN_L    0x04U   /* BTNL P17 - Freq+    */
#define BTN_R    0x08U   /* BTNR M17 - Freq-    */
#define BTN_D    0x10U   /* BTND P18 - Duty-    */
#define BTN_MASK 0x1FU
/*--- Switch masks ---*/
#define SW0      0x0001U /* Forward/Reverse     */
#define SW1      0x0002U /* Turn Left           */
#define SW2      0x0004U /* Turn Right          */
#define SW_MASK  0xFFFFU /* All 16 switches     */
/*--- Direction GPIO bits ---*/
#define IN1      0x01U   /* Motor1 forward A    */
#define IN2      0x02U   /* Motor1 forward B    */
#define IN3      0x04U   /* Motor2 forward A    */
#define IN4      0x08U   /* Motor2 forward B    */
/*--- PWM frequency presets (nanoseconds) ---*/
#define FREQ_COUNT 7U
static const u32 FREQ_NS[FREQ_COUNT] = {
    1000000U,   /* 0:   1kHz  */
     500000U,   /* 1:   2kHz  */
     200000U,   /* 2:   5kHz  */
     100000U,   /* 3:  10kHz  default */
      50000U,   /* 4:  20kHz  */
      20000U,   /* 5:  50kHz  */
      10000U    /* 6: 100kHz  */
};
static const char* FREQ_LBL[FREQ_COUNT] = {
    "1kHz","2kHz","5kHz","10kHz",
    "20kHz","50kHz","100kHz"
};
/*--- Duty cycle settings ---*/
#define DUTY_STEP    10U
#define DUTY_DEFAULT 50U
#define DUTY_MAX     100U
#define DUTY_MIN     0U
#define DIFF_STEP    20U
#define FREQ_DEFAULT 3U
/*--- Global driver instances ---*/
static XGpio   GpioBtn;  /* GPIO0 - buttons  */
static XGpio   GpioSw;   /* GPIO1 - switches */
static XGpio   GpioDir;  /* GPIO2 - dir_out  */
static XTmrCtr Timer0;   /* TMR0  - Motor1   */
static XTmrCtr Timer1;   /* TMR1  - Motor2   */
/*--- Global state ---*/
static u32 base_duty = DUTY_DEFAULT;
static u32 freq_idx  = FREQ_DEFAULT;
/*--------------------------------------------------
 * clamp()
 *--------------------------------------------------*/
static u32 clamp(u32 val, u32 mn, u32 mx)
{
    if (val > mx) return mx;
    if (val < mn) return mn;
    return val;
}
/*--------------------------------------------------
 * apply_pwm() - sets PWM for both motors
 *--------------------------------------------------*/
static void apply_pwm(u32 duty1, u32 duty2)
{
    u32 period = FREQ_NS[freq_idx]/10;
    u32 high1  = (duty1 == 0U)   ? 1U        :
                 (duty1 >= 100U) ? period-1U  :
                 (period * duty1) / 100U;
    u32 high2  = (duty2 == 0U)   ? 1U        :
                 (duty2 >= 100U) ? period-1U  :
                 (period * duty2) / 100U;
    XTmrCtr_PwmDisable(&Timer0);
    XTmrCtr_PwmConfigure(&Timer0, period, high1);
    XTmrCtr_PwmEnable(&Timer0);

    XTmrCtr_PwmDisable(&Timer1);
    XTmrCtr_PwmConfigure(&Timer1, period, high2);
    XTmrCtr_PwmEnable(&Timer1);
}
/*--------------------------------------------------
 * set_direction() - sets IN1-IN4
 *--------------------------------------------------*/
static void set_direction(u8 forward)
{
    u32 dir = forward ?
        (IN1 | IN3) :  /* Forward */
        (IN2 | IN4);   /* Reverse */
    XGpio_DiscreteWrite(&GpioDir, 1, dir);
}
/*--------------------------------------------------
 * update_motors() - applies duty + direction
 *--------------------------------------------------*/
static void update_motors(u32 sw)
{
    u32 duty1 = base_duty;
    u32 duty2 = base_duty;
    u8  fwd   = (sw & SW0) ? 0U : 1U;

    if (sw & SW1) {
        duty1 = clamp(base_duty - DIFF_STEP,
                      DUTY_MIN, DUTY_MAX);
        duty2 = clamp(base_duty + DIFF_STEP,
                      DUTY_MIN, DUTY_MAX);
        xil_printf("[MODE] Turn Left\r\n");
    }
    else if (sw & SW2) {
        duty1 = clamp(base_duty + DIFF_STEP,
                      DUTY_MIN, DUTY_MAX);
        duty2 = clamp(base_duty - DIFF_STEP,
                      DUTY_MIN, DUTY_MAX);
        xil_printf("[MODE] Turn Right\r\n");
    }
    else {
        xil_printf("[MODE] %s\r\n",
                   fwd ? "Forward" : "Reverse");
    }
    set_direction(fwd);
    apply_pwm(duty1, duty2);
    xil_printf("[PWM]  M1=%d%% M2=%d%%\r\n",
               duty1, duty2);
    xil_printf("[FREQ] %s\r\n",
               FREQ_LBL[freq_idx]);
    xil_printf("----------------------------\r\n");
}
/*--------------------------------------------------
 * main()
 *--------------------------------------------------*/
int main(void)
{
    init_platform();

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("  Nexys4 DDR Motor Control System      \r\n");
    xil_printf("  BTNU/BTND = Duty    +/-10%%           \r\n");
    xil_printf("  BTNL/BTNR = Freq    up/down          \r\n");
    xil_printf("  BTNC      = Reset                    \r\n");
    xil_printf("  SW0       = Forward/Reverse          \r\n");
    xil_printf("  SW1       = Turn Left                \r\n");
    xil_printf("  SW2       = Turn Right               \r\n");
    xil_printf("========================================\r\n\r\n");
    /*--- Init GPIO Buttons ---*/
    if (XGpio_Initialize(&GpioBtn,
        XPAR_XGPIO_0_BASEADDR) != XST_SUCCESS) {
        xil_printf("[ERROR] Button GPIO Failed!\r\n");
        cleanup_platform();
        return -1;
    }
    XGpio_SetDataDirection(&GpioBtn, 1, BTN_MASK);
    xil_printf("[INIT] Buttons OK!\r\n");
    /*--- Init GPIO Switches ---*/
    if (XGpio_Initialize(&GpioSw,
        XPAR_XGPIO_1_BASEADDR) != XST_SUCCESS) {
        xil_printf("[ERROR] Switch GPIO Failed!\r\n");
        cleanup_platform();
        return -1;
    }
    XGpio_SetDataDirection(&GpioSw, 1, SW_MASK);
    xil_printf("[INIT] Switches OK!\r\n");
    /*--- Init GPIO Direction ---*/
    if (XGpio_Initialize(&GpioDir,
        XPAR_XGPIO_2_BASEADDR) != XST_SUCCESS) {
        xil_printf("[ERROR] Direction GPIO Failed!\r\n");
        cleanup_platform();
        return -1;
    }
    XGpio_SetDataDirection(&GpioDir, 1, 0x00);
    xil_printf("[INIT] Direction GPIO OK!\r\n");
    
   /*--- Init Timer0 Motor1 ---*/
if (XTmrCtr_Initialize(&Timer0,
    XPAR_XTMRCTR_0_BASEADDR) != XST_SUCCESS) {
    xil_printf("[ERROR] Timer0 Failed!\r\n");
    cleanup_platform();
    return -1;
}
xil_printf("[INIT] Timer0 Motor1 OK!\r\n");

XTmrCtr_SetOptions(&Timer0, 0,
    XTC_AUTO_RELOAD_OPTION |
    XTC_DOWN_COUNT_OPTION);

XTmrCtr_SetOptions(&Timer0, 1,
    XTC_AUTO_RELOAD_OPTION |
    XTC_DOWN_COUNT_OPTION |
    XTC_EXT_COMPARE_OPTION);

/*--- Init Timer1 Motor2 ---*/
if (XTmrCtr_Initialize(&Timer1,
    XPAR_XTMRCTR_1_BASEADDR) != XST_SUCCESS) {
    xil_printf("[ERROR] Timer1 Failed!\r\n");
    cleanup_platform();
    return -1;
}
xil_printf("[INIT] Timer1 Motor2 OK!\r\n");

XTmrCtr_SetOptions(&Timer1, 0,
    XTC_AUTO_RELOAD_OPTION |
    XTC_DOWN_COUNT_OPTION);

XTmrCtr_SetOptions(&Timer1, 1,
    XTC_AUTO_RELOAD_OPTION |
    XTC_DOWN_COUNT_OPTION |
    XTC_EXT_COMPARE_OPTION);

/*--- START TIMERS (CRITICAL FIX) ---*/
XTmrCtr_Start(&Timer0, 0);
XTmrCtr_Start(&Timer0, 1);

XTmrCtr_Start(&Timer1, 0);
XTmrCtr_Start(&Timer1, 1);
    
    xil_printf("[INIT] Timer1 Motor2 OK!\r\n");
    /*--- Apply default PWM ---*/
    xil_printf("[INIT] Starting PWM 10kHz 50%%...\r\n");
    update_motors(0U);
    xil_printf("[READY] System Ready!\r\n\r\n");
    /*--- Edge detection variables ---*/
    u32 btn_prev = 0U;
    u32 btn_now  = 0U;
    u32 btn_edge = 0U;
    u32 sw_now   = 0U;
    static u32 sw_prev = 0U;
    /*--- Main loop ---*/
    while(1)
    {
        sw_now   = XGpio_DiscreteRead(&GpioSw,  1)
                   & SW_MASK;
        btn_now  = XGpio_DiscreteRead(&GpioBtn, 1)
                   & BTN_MASK;
        btn_edge = btn_now & (~btn_prev);

        /*--- BTNU duty+ ---*/
        if (btn_edge & BTN_U) {
            base_duty = clamp(
                base_duty + DUTY_STEP,
                DUTY_MIN, DUTY_MAX);
            xil_printf("[BTN] BTNU Duty+\r\n");
            update_motors(sw_now);
        }
        /*--- BTND duty- ---*/
        if (btn_edge & BTN_D) {
            base_duty = clamp(
                base_duty - DUTY_STEP,
                DUTY_MIN, DUTY_MAX);
            xil_printf("[BTN] BTND Duty-\r\n");
            update_motors(sw_now);
        }
        /*--- BTNL freq+ ---*/
        if (btn_edge & BTN_L) {
            if (freq_idx < FREQ_COUNT - 1U)
                freq_idx++;
            xil_printf("[BTN] BTNL Freq+\r\n");
            update_motors(sw_now);
        }
        /*--- BTNR freq- ---*/
        if (btn_edge & BTN_R) {
            if (freq_idx > 0U)
                freq_idx--;
            xil_printf("[BTN] BTNR Freq-\r\n");
            update_motors(sw_now);
        }
        /*--- BTNC reset ---*/
        if (btn_edge & BTN_C) {
            base_duty = DUTY_DEFAULT;
            freq_idx  = FREQ_DEFAULT;
            xil_printf("[BTN] BTNC Reset!\r\n");
            update_motors(sw_now);
        }
        /*--- Switch state change ---*/
        if (sw_now != sw_prev) {
            xil_printf("[SW] Switch Changed!\r\n");
            update_motors(sw_now);
            sw_prev = sw_now;
        }
        btn_prev = btn_now;
        usleep(20000U);
    }
    cleanup_platform();
    return 0;
}

