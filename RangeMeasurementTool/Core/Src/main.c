/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dac.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <wchar.h>
#include <stdbool.h>
#include <math.h>
#include "hagl.h"
#include "font6x9.h"
#include "rgb565.h"
#include "ds18b20.h"
#include "ws2812b.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// gamma correction for WS2812B
const uint8_t gamma8[] = {
		0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
		1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
		2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
		5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
		10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
		17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
		25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
		37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
		51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
		69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
		90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
		115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
		144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
		177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
		215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
const uint8_t ds1[] = { 0x28, 0x86, 0x54, 0x14, 0x00, 0x00, 0x00, 0xFA }; // temp sensor 1 adress
const uint8_t ds2[] = { 0x28, 0x5B, 0x1D, 0x13, 0x00, 0x00, 0x00, 0xE8 }; // temp sensor 2 adress
volatile bool temp_measure_pending = true; // boolean value for launching measurement
volatile float temp_ds1 = 0.0;
volatile float temp_ds2 = 0.0; // initial value for temperature (used in interruption)
volatile float distance = 0.0;
volatile float prev_distance = 0.0;

// function for sending text using UART (i.e to PC)
int __io_putchar(int ch)
{
	if (ch == '\n')
		__io_putchar('\r');

	HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
	return 1;
}

// speed of sound calculation based on air temperature
static float calc_sound_speed(float temp_ds1)
{
	return 331.8f + 0.6f * temp_ds1;
}

// function to check how many digits value has (used in displaying distance on LCD)
int howManyDigits(double value)
{
	// if value == 0, then 1 digit
	if (!value)
		return 1;

	int digits = (int)log10(fabs(value)) + 1;
	return digits;
}

// interruptions from timers
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim == &htim7) { //Timer nr 7 is used for refreshing screen and diodes
		lcd_copy(); // update LCD (send buffer from i.e hagl_put_text)
		ws2812b_update(); // update WS2812B diodes (send buffer i.e ws2812b_set_color_all)
	} else if (htim == &htim16) { // Timer nr 16 is used for temperature measurement and preparing buffer for LCD
		if (temp_measure_pending) {
			ds18b20_start_measure(ds1); // start measurements for both ds18b20
			ds18b20_start_measure(ds2);
			temp_measure_pending = false; // measure started flag
		} else {
			// read and save converted temperature
			temp_ds1 = ds18b20_get_temp(ds1);
			temp_ds2 = ds18b20_get_temp(ds2);
			// buffers for temperature (wchar_t is accepted by hagl_put_text)
			wchar_t buffer_ds1[32];
			wchar_t buffer_ds2[32];
			// coversion float temp for wide char to put in hagl_put_text
			swprintf(buffer_ds1, 32, L"Case temp: %.1f*C", temp_ds1);
			swprintf(buffer_ds2, 32, L"Sensor temp: %.1f*C", temp_ds2);

			// hagl function for writing text with error handler (ds18b20 displays errors as 85*C value)
			if (temp_ds1 <= -80.0f) {
				hagl_put_text(L"Sensor (1) error...", 10, 20, RED, font6x9);
			} else {
				hagl_put_text(buffer_ds1, 10, 20, YELLOW, font6x9);
			}

			if (temp_ds1 <=-80.0f) {
				hagl_put_text(L"Sensor (2) error...", 10, 20, RED, font6x9);
			} else {
				hagl_put_text(buffer_ds2, 10, 40, YELLOW, font6x9);
			}
			// measurement finished, ready for next one
			temp_measure_pending = true;
		}
	} else if (htim == &htim2) { // Timer nr 2 is used for measuring distance and preparing buffers for LCD and LED
		// capturing rising and falling edge of sensor signal
		uint32_t start = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_1);
		uint32_t stop = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_2);
		// calculating distance based on sound speed at measured temperature
		distance = (stop - start) * calc_sound_speed(temp_ds1) / 20000.0f;
		// preparing data for LCD
		wchar_t buffer_distance[32];
		swprintf(buffer_distance, 32, L"Distance: %.1f cm", distance);

		// if numver of digits has changed (154 cm -> 67 cm), refresh screen by drawing rectangle
		if (howManyDigits(distance) != howManyDigits(prev_distance))
			hagl_fill_rectangle(10, 60, 150, 70, BLACK);

		// limiting range for LED - it will change colors only in 0-200 cm range
		float distance_led;
		if (distance > 200) {
			distance_led = 200;
		} else if (distance < 0) {
			distance_led = 0;
		} else  {
			distance_led = distance;
		}

		// closer means more red, further means more green
		ws2812b_set_color_all(255-roundf(distance_led/200*255), roundf(distance_led/200*255), 0);

		hagl_put_text(buffer_distance, 10, 60, YELLOW, font6x9); // prepare buffer for LCD
		prev_distance = distance;
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI2_Init();
  MX_DAC1_Init();
  MX_TIM6_Init();
  MX_USART3_UART_Init();
  MX_TIM7_Init();
  MX_TIM16_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
	HAL_TIM_Base_Start_IT(&htim2);
	HAL_TIM_Base_Start_IT(&htim7);
	HAL_TIM_Base_Start_IT(&htim16);
	HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_1);
	HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	lcd_init(); // LCD initialization

	while (1)
	{

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
