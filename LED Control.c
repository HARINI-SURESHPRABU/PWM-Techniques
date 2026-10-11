#include <stdio.h>
#include "xparameters.h"
#include "xgpio.h"
#include "xtmrctr.h"
#include "xil_printf.h"
#include "sleep.h"
#include "platform.h"
/*==================================================
 * VERIFIED from xparameters.h:
 * GPIO  BASEADDR = 0x40000000
 * TIMER BASEADDR = 0x41c00000
 * CLOCK FREQ     = 100MHz (0x5f5e100)
 * GPIO WIDTH     = 5 bits (push_buttons_5bits)
 *==================================================*/
/* Button masks - Nexys4 DDR push_buttons_5bits
 * Verify these by pressing buttons and checking
 * values if LED behavior is unexpected          */
#define BTN_C   0x01U   /* BTNC - Center - N17 */
#define BTN_U   0x02U   /* BTNU - Up     - M18 */
#define BTN_D   0x010U   /* BTND - Down   - P18 */
#define BTN_MASK 0x1FU  /* All 5 buttons mask  */
/* PWM Settings
 * Clock = 100MHz
 * PwmConfigure takes values in nanoseconds
 * Period = 100,000 ns = 10kHz PWM frequency    */
#define PWM_PERIOD_NS    100000U  /* 10kHz       */
#define DUTY_STEP_NS       5000U  /* 5% step     */
#define DUTY_DEFAULT_NS   50000U  /* 50% default */
#define DUTY_MAX_NS       95000U  /* 95% max     */
#define DUTY_MIN_NS        5000U  /* 5%  min     */
/* Global driver instances */
static XGpio   GpioInst;
static XTmrCtr TimerInst;
/* Current duty cycle in nanoseconds */
static u32 duty_ns = DUTY_DEFAULT_NS;
/*--------------------------------------------------
 * set_pwm()
 * Reconfigures PWM hardware with new duty cycle
 *--------------------------------------------------*/
void set_pwm(u32 new_duty_ns)
{
    /* Clamp to valid range */
    if (new_duty_ns > DUTY_MAX_NS) new_duty_ns = DUTY_MAX_NS;
    if (new_duty_ns < DUTY_MIN_NS) new_duty_ns = DUTY_MIN_NS;
    duty_ns = new_duty_ns;
    /* Disable → Reconfigure → Enable */
    XTmrCtr_PwmDisable(&TimerInst);
    XTmrCtr_PwmConfigure(&TimerInst, PWM_PERIOD_NS, duty_ns);
    XTmrCtr_PwmEnable(&TimerInst);
    /* Print current duty cycle percentage */
    u32 percent = (duty_ns * 100U) / PWM_PERIOD_NS;
    xil_printf("PWM Duty Cycle = %d%%\r\n", percent);
}
/*--------------------------------------------------
 * main()
 *--------------------------------------------------*/
int main()
{
    init_platform();
    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("  Nexys4 DDR PWM LED Brightness Control \r\n");
    xil_printf("  Board  : Nexys4 DDR                  \r\n");
    xil_printf("  LED    : H17 (LD0) via pwm0_0        \r\n");
    xil_printf("  PWM    : 10kHz Hardware PWM           \r\n");
    xil_printf("========================================\r\n");
    xil_printf("  BTNU (M18) = Increase Brightness +5%% \r\n");
    xil_printf("  BTND (P18) = Decrease Brightness -5%% \r\n");
    xil_printf("  BTNC (N17) = Reset to 50%%            \r\n");
    xil_printf("========================================\r\n\r\n");
    /*--- Initialize GPIO (buttons) ---*/
    xil_printf("[INIT] Initializing GPIO...\r\n");
    if (XGpio_Initialize(&GpioInst,
                         XPAR_XGPIO_0_BASEADDR) != XST_SUCCESS)
    {
        xil_printf("[ERROR] GPIO Init Failed!\r\n");
        cleanup_platform();
        return -1;
    }
    /* Channel 1 = all inputs (5-bit buttons) */
    XGpio_SetDataDirection(&GpioInst, 1, BTN_MASK);
    xil_printf("[INIT] GPIO OK! Buttons Ready.\r\n");
    /*--- Initialize AXI Timer ---*/
    xil_printf("[INIT] Initializing Timer...\r\n");
    if (XTmrCtr_Initialize(&TimerInst,
                           XPAR_XTMRCTR_0_BASEADDR) != XST_SUCCESS)
    {
        xil_printf("[ERROR] Timer Init Failed!\r\n");
        cleanup_platform();
        return -1;
    }
    xil_printf("[INIT] Timer OK!\r\n");
    /*--- Start PWM at 50% ---*/
    xil_printf("[INIT] Starting PWM at 50%%...\r\n");
    set_pwm(DUTY_DEFAULT_NS);
    xil_printf("[INIT] PWM Started!\r\n");
    xil_printf("\r\n");
    xil_printf("[READY] System Ready! Press buttons.\r\n");
    xil_printf("========================================\r\n\r\n");
    /*--- Button edge detection variables ---*/
    u32 btn_prev = 0U;
    u32 btn_now  = 0U;
    u32 btn_edge = 0U;
    /*--- Main control loop ---*/
    while(1)
    {
        /* Read all 5 buttons */
        btn_now  = XGpio_DiscreteRead(&GpioInst, 1) & BTN_MASK;
        /* Detect rising edge (new press only) */
        btn_edge = btn_now & (~btn_prev);
        /* BTNU - Increase brightness */
        if (btn_edge & BTN_U)
        {
            xil_printf("\r\n[BTN] BTNU Pressed! (M18)\r\n");
            xil_printf("[ACT] Increasing Brightness +5%%\r\n");
            set_pwm(duty_ns + DUTY_STEP_NS);
            xil_printf("[OK]  Brightness Increased!\r\n");
        }
        /* BTND - Decrease brightness */
        if (btn_edge & BTN_D)
        {
            xil_printf("\r\n[BTN] BTND Pressed! (P18)\r\n");
            xil_printf("[ACT] Decreasing Brightness -5%%\r\n");
            set_pwm(duty_ns - DUTY_STEP_NS);
            xil_printf("[OK]  Brightness Decreased!\r\n");
        }
        /* BTNC - Reset to 50% */
        if (btn_edge & BTN_C)
        {
            xil_printf("\r\n[BTN] BTNC Pressed! (N17)\r\n");
            xil_printf("[ACT] Resetting to 50%%\r\n");
            set_pwm(DUTY_DEFAULT_NS);
            xil_printf("[OK]  Brightness Reset to 50%%!\r\n");
        } /* Save previous state */
        btn_prev = btn_now;     /* 20ms debounce delay */
        usleep(20000U);    }
   cleanup_platform();    
  return 0; }
