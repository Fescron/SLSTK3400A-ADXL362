/***************************************************************************//**
 * @file main.c
 * @brief The main file for the program to interface to the accelerometer.
 * @version 3.2
 * @author Brecht Van Eeckhoudt
 *
 * ******************************************************************************
 *
 * @section Pinout
 *
 *   ADXL:
 *     PE10: MOSI
 *     PE11: MISO
 *     PE12: CLK
 *     PD04: NCS
 *     PD05: VCC
 *     PD07: INT1
 *
 *   LED's:
 *     PF04: LED0
 *     PF05: LED1
 *
 *   BUTTONS:
 *     PC09: PB0
 *     PC10: PB1
 *
 ******************************************************************************/


#include <stdint.h>    /* (u)intXX_t */
#include <stdbool.h>   /* "bool", "true", "false" */
#include "em_device.h" /* Include necessary MCU-specific header file */
#include "em_chip.h"   /* Chip Initialization */
#include "em_cmu.h"    /* Clock management unit */
#include "em_gpio.h"   /* General Purpose IO */
#include "em_usart.h"  /* Universal synchronous/asynchronous receiver/transmitter */
#include "em_emu.h"    /* Energy Management Unit */
#include "em_rtc.h"    /* Real Time Counter (RTC) */

#include "../inc/accel.h"    	/* Functions related to the accelerometer */
#include "../inc/util.h"    	/* Utility functions */
#include "../inc/handlers.h" 	/* Interrupt handlers */
#include "../inc/pin_mapping.h" /* PORT and PIN definitions */

#include "../inc/debugging.h" /* Enable or disable printing to UART for debugging */


/* Definitions for RTC compare interrupts */
#define DELAY_RTC 60.0 /* seconds */
#define LFXOFREQ 32768
#define COMPARE_RTC (DELAY_RTC * LFXOFREQ)


/**************************************************************************//**
 * @brief
 *   Initialize GPIO wakeup functionality.
 *
 * @details
 *   Initialize buttons PB0 and PB1 on falling-edge interrupts and
 *   ADXL_INT1 on rising-edge interrupts.
 *****************************************************************************/
void initGPIOwakeup (void)
{
	/* Enable necessary clock */
	CMU_ClockEnable(cmuClock_GPIO, true);

	/* Configure PB0 and PB1 as input with glitch filter enabled */
	GPIO_PinModeSet(PB0_PORT, PB0_PIN, gpioModeInputPullFilter, 1);
	GPIO_PinModeSet(PB1_PORT, PB1_PIN, gpioModeInputPullFilter, 1);

	/* Configure ADXL_INT1 as input */
	GPIO_PinModeSet(ADXL_INT1_PORT, ADXL_INT1_PIN, gpioModeInput, 1);

	/* Enable IRQ for even numbered GPIO pins */
	NVIC_EnableIRQ(GPIO_EVEN_IRQn);

	/* Enable IRQ for odd numbered GPIO pins */
	NVIC_EnableIRQ(GPIO_ODD_IRQn);

	/* Enable falling-edge interrupts for PB pins */
	GPIO_IntConfig(PB0_PORT, PB0_PIN, 0, 1, true);
	GPIO_IntConfig(PB1_PORT, PB1_PIN, 0, 1, true);

	/* Enable rising-edge interrupts for ADXL_INT1 */
	GPIO_IntConfig(ADXL_INT1_PORT, ADXL_INT1_PIN, 1, 0, true);
}


/**************************************************************************//**
 * @brief RTCC initialization
 *****************************************************************************/
void initRTCcomp (void)
{
	/* Enable the low-frequency crystal oscillator for the RTC */
	CMU_OscillatorEnable(cmuOsc_LFXO, true, true);

	/* Enable the clock to the interface of the low energy modules */
	CMU_ClockEnable(cmuClock_HFLE, true); // cmuClock_CORELE = cmuClock_HFLE

	/* Route the LFXO clock to the RTC */
	CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO);

	/* Turn on the RTC clock */
	CMU_ClockEnable(cmuClock_RTC, true);

	/* Set RTC compare value for RTC compare register 0 */
	RTC_CompareSet(0, COMPARE_RTC);

	/* Allow channel 0 to cause an interrupt */
	RTC_IntEnable(RTC_IEN_COMP0);
	NVIC_ClearPendingIRQ(RTC_IRQn);
	NVIC_EnableIRQ(RTC_IRQn);

	/* Configure the RTC settings */
	RTC_Init_TypeDef rtc = RTC_INIT_DEFAULT;

	/* Initialize RTC with pre-defined settings */
	RTC_Init(&rtc);
}


/**************************************************************************//**
 * @brief
 *   Main function.
 *****************************************************************************/
int main (void)
{
	//uint32_t counter = 0;

	/* Initialize chip */
	CHIP_Init();

	/* Initialize systick */
	if (SysTick_Config(CMU_ClockFreqGet(cmuClock_CORE) / 1000)) while (1);

	/* Initialize RTC compare settings */
	initRTCcomp();

	/* Initialize GPIO wakeup */
	initGPIOwakeup();

#ifdef DEBUGGING /* DEBUGGING */
	dbprint_INIT(USART1, 4, true, false); /* VCOM */
	//dbprint_INIT(USART1, 0, false, false); /* US1_TX = PC0 */
#endif /* DEBUGGING */

	/* Initialize VCC GPIO and turn the power to the accelerometer on */
	initADXL_VCC();

	/* Initialize USART0 as SPI slave (also initialize CS pin) */
	initADXL_SPI();

	/* Initialize LED's */
	initLEDS();

	/* Soft reset ADXL handler */
	resetHandlerADXL();


	/* Profile the ADXL (make sure to not use VCOM here!) */
	//testADXL();


	/* Set the measurement range (0 - 1 - 2) */
	configADXL_range(1); /* 0 = +-2g -- 1 = +-4g -- 3 = +-8g */

	/* Configure ODR (0 - 1 - 2 - 3 - 4 - 5) */
	configADXL_ODR(0); /* 0 = 12.5 Hz -- 3 = 100 Hz (reset default) */


	/* Read and display values forever */
	//readValuesADXL();


	/* Configure activity detection on INT1 */
	configADXL_activity(3); /* [g] */

	/* Enable wake-up mode */
	/* TODO: Maybe implement this in the future... */
	//writeADXL(ADXL_REG_POWER_CTL, 0b00001000); /* 5th bit */


	/* Enable measurements */
	measureADXL(true);

#ifdef DEBUGGING /* DEBUGGING */
	dbprintln("");
#endif /* DEBUGGING */

	while(1)
	{
		led0(true); /* Enable LED0 */
		Delay(1000);
		led0(false); /* Disable LED0 */

		/* Read status register to acknowledge interrupt
		 * (can be disabled by changing LINK/LOOP mode in ADXL_REG_ACT_INACT_CTL) */
		if (triggered)
		{
			readADXL(ADXL_REG_STATUS);
			triggered = false;
		}

#ifdef DEBUGGING /* DEBUGGING */
	dbinfo("Disabling systick & going to sleep...\r\n");
#endif /* DEBUGGING */

		systickInterrupts(false); /* Disable SysTick interrupts */
		enableSPIpinsADXL(false); /* Disable SPI pins */

		EMU_EnterEM2(false); /* "true" doesn't seem to have any effect (save and restore oscillators, clocks and voltage scaling) */

		enableSPIpinsADXL(true); /* Enable SPI pins */
		systickInterrupts(true); /* Enable SysTick interrupts */
	}
}
