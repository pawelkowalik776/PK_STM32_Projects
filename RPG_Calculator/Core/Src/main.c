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
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "hagl.h"
#include "font6x9.h"
#include "rgb565.h"
#include <stdint.h>
#include "images.h"
#include <stdio.h>
#include <wchar.h>
#include <math.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBOUNCE_DELAY 50
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

// variable indicating current state of the program. Used in switch case in main loop
typedef enum {
	STATE_INIT,
	STATE_WAITING_FOR_ATK_DICE,
	STATE_WAITING_FOR_DEF_DICE,
	STATE_ATTACK_MISSED_DELAY,
	STATE_WAITING_FOR_HP,
	STATE_WAITING_FOR_ARMOR,
	STATE_WAITING_FOR_DMG,
	STATE_WAITING_FOR_LOCATION,
	STATE_WAITING_FOR_REDUCTION,
	STATE_CALCULATION
} ProgramState;

// variables to be input by user and calculated by program
volatile static uint8_t rollATK = 0;
volatile static uint8_t rollDEF = 0;
volatile static uint8_t targetHP = 0;
volatile static uint8_t targetARMOR = 0;
volatile static uint8_t enteredDMG = 0;
volatile static float locationMultiplier = 0;
volatile static float armorTypeMultiplier = 0;
volatile static uint8_t critDMG = 0;
volatile static float totalDMG = 0;

// initial state of the program
static ProgramState state = STATE_INIT;

// variables needed to use button as interruption, featuring debouncing
static volatile uint32_t lastDebounceTime = 0;
static volatile bool debounceActive = false;
static volatile bool buttonConfirm = false;
//static volatile bool buttonPressed = false;

// variables needed to use encoder for input data and navigate through program
volatile int32_t encoder_value = 0;
int16_t prev_encoder_value = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


// function for sending text using UART (i.e to PC, mainly for debugging)
int __io_putchar(int ch)
{
	if (ch == '\n')
		__io_putchar('\r');

	HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
	return 1;
}

// function to check how many digits value has (used in displaying digits on LCD)
int howManyDigits(double value)
{
	// if value == 0, then 1 digit
	if (!value)
		return 1;
	else
		return (int)log10(fabs(value)) + 1;;
}

// val - value to be coverted into string
// *text - pointer to string text that will be attached before value
// color - color of whole text, x0,y0 - starting point of "drawing" text on screen
void textStringCombiner(int32_t val, const wchar_t *text, color_t color, int16_t x0, int16_t y0)
{

		if ( howManyDigits(encoder_value) != howManyDigits(prev_encoder_value) )
			hagl_fill_rectangle(wcslen(text)*6+6, y0, wcslen(text)*6+6 + 3*6, y0 + 9, BLACK); 	// "delete" additional digit when number of digits change (10 -> 9)

		wchar_t buffer[16];																		// declaration of string that will contain value "val"
		wchar_t result[32] = L""; 																// empty string initialization

		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L" %d", val);						// enter "val" to string "buffer"

		wcscat(result, text);																	// input "text" in "result" string
		wcscat(result, buffer);																	// add "buffer" string (val) to "result" string
		hagl_put_text(result, x0, y0, color, font6x9);											// draw it on the screen using input coordinates

}

//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
//{
//	if (GPIO_Pin == ENCODER_BUTTON_Pin) {
//		if (!debounceActive) {
//			debounceActive = true;
//			lastDebounceTime = HAL_GetTick();
//			buttonPressed = HAL_GPIO_ReadPin(ENCODER_BUTTON_GPIO_Port, ENCODER_BUTTON_Pin);
//		}
//	}
//}

//void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
//{
//	if (htim == &htim6) { //Timer nr 6 is used for refreshing screen
//		lcd_copy(); // update LCD (send buffer from i.e hagl_put_text)
//	} else if (htim == &htim7) {
//		uint32_t currentTime = HAL_GetTick();
//
//		if (debounceActive && (currentTime - lastDebounceTime >= DEBOUNCE_DELAY)) {
//			debounceActive = false;
//
//			bool currentButtonState = HAL_GPIO_ReadPin(ENCODER_BUTTON_GPIO_Port, ENCODER_BUTTON_Pin);
//
//			if (currentButtonState == buttonPressed) {
//				if (buttonPressed) {
//					buttonConfirm = true;
//				}
//			}
//
//		}
//
//	}
//}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == ENCODER_BUTTON_Pin) {
		if (!debounceActive) {
			debounceActive = true;
			lastDebounceTime = HAL_GetTick();
			buttonConfirm = true;
		}
	}
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim == &htim6) { //Timer nr 6 is used for refreshing screen
		lcd_copy(); // update LCD (send buffer from i.e hagl_put_text)
	} else if (htim == &htim7) {
		uint32_t currentTime = HAL_GetTick();

		if (debounceActive && (currentTime - lastDebounceTime >= DEBOUNCE_DELAY)) {
			debounceActive = false;
		}
	}
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if (htim == &htim3) {
		encoder_value = __HAL_TIM_GET_COUNTER(&htim3)/2;
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
  MX_TIM3_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  lcd_init();

  HAL_TIM_Base_Start_IT(&htim6);
  HAL_TIM_Base_Start(&htim3);
  HAL_TIM_Encoder_Start_IT(&htim3, TIM_CHANNEL_ALL);
  HAL_TIM_Base_Start_IT(&htim7);

  bitmap_t bitmap_intro;
  init_bitmap(&bitmap_intro, image_intro, image_intro_size, LCD_WIDTH, LCD_HEIGHT);
  hagl_blit(0, 0, &bitmap_intro);
  //lcd_copy();
  HAL_Delay(500);
  hagl_clear_screen();
  //lcd_copy();

  while (1)
  {
	  switch (state)
	  {
	  case STATE_INIT:
		  hagl_put_text(L"Enter ATK roll:", 5, 5, YELLOW, font6x9);
		  state = STATE_WAITING_FOR_ATK_DICE;
		  break;

	  case STATE_WAITING_FOR_ATK_DICE:

		  if (encoder_value != prev_encoder_value) { 													// code below executed only if value of encoder has beem changed
			  textStringCombiner(encoder_value, L"Enter ATK roll:", YELLOW, 5, 5);
			  prev_encoder_value = encoder_value;
		  }

			if (buttonConfirm) {
				buttonConfirm = false;
				rollATK = encoder_value;
				hagl_put_text(L"Enter DEF roll: ", 5, 15, YELLOW, font6x9); 							// po to zeby wysweetlic przed ruszeniem enkoderem
				state = STATE_WAITING_FOR_DEF_DICE;
			}
		  //hagl_put_text(L"Enter ATK roll:", 10, 10, YELLOW, font6x9);
		  break;

	  case STATE_WAITING_FOR_DEF_DICE:

		if (encoder_value != prev_encoder_value) {
			textStringCombiner(encoder_value, L"Enter DEF roll:", YELLOW, 5, 15);
			prev_encoder_value = encoder_value;
		}

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  rollDEF = encoder_value;
			  //state = NEXT_STEP;

			  if (rollATK <= rollDEF) {
				  state = STATE_ATTACK_MISSED_DELAY;
				  hagl_clear_screen();
				  encoder_value = 0; // zerowanie nie dziala
				  rollATK = 0;
				  rollDEF = 0;

			  } else {
				  hagl_put_text(L"Enter target's HP:", 5, 25, YELLOW, font6x9);
				  state = STATE_WAITING_FOR_HP;
			  }
		  }
		  break;

	  case STATE_ATTACK_MISSED_DELAY:
		  hagl_put_text(L"--- ATTACK MISSED ---", 18, LCD_HEIGHT/2-5, GREEN, font6x9);

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  hagl_clear_screen();
			  state = STATE_INIT;
		  }
		  break;


	  case STATE_WAITING_FOR_HP:

		if (encoder_value != prev_encoder_value) {
			textStringCombiner(encoder_value, L"Enter target's HP:", YELLOW, 5, 25);
			prev_encoder_value = encoder_value;
		}

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  targetHP = encoder_value;
			  hagl_put_text(L"Enter target's ARMOR:", 5, 35, YELLOW, font6x9);
			  state = STATE_WAITING_FOR_ARMOR;
		  }
		  break;

	  case STATE_WAITING_FOR_ARMOR:

		if (encoder_value != prev_encoder_value) {
			textStringCombiner(encoder_value, L"Enter target's ARMOR:", YELLOW, 5, 35);
			prev_encoder_value = encoder_value;
		}

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  targetARMOR = encoder_value;
			  hagl_put_text(L"Enter total DMG:", 5, 45, YELLOW, font6x9);
			  state = STATE_WAITING_FOR_DMG;
		  }
		  break;

	  case STATE_WAITING_FOR_DMG:

		if (encoder_value != prev_encoder_value) {
			textStringCombiner(encoder_value, L"Enter total DMG:", YELLOW, 5, 45);
			prev_encoder_value = encoder_value;
		}

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  enteredDMG = encoder_value;
			  hagl_put_text(L"Enter location of attack:", 5, 55, YELLOW, font6x9);
			  hagl_put_text(L"x1/2", 10, 67, YELLOW, font6x9);
			  hagl_put_text(L"x1", 75, 67, YELLOW, font6x9);
			  hagl_put_text(L"x3", 135, 67, YELLOW, font6x9);
			  state = STATE_WAITING_FOR_LOCATION;
		  }
		  break;

	  case STATE_WAITING_FOR_LOCATION:

		  if ( (encoder_value % 3) == 0) {
			  hagl_draw_rounded_rectangle(8, 65, 9+4*6+2, 65+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(73, 65, 74+2*6+1, 65+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(133, 65, 134+2*6+2, 65+11, 1, BLACK);
		  } else if ( (encoder_value % 3) == 1) {
			  hagl_draw_rounded_rectangle(73, 65, 74+2*6+1, 65+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(8, 65, 9+4*6+2, 65+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(133, 65, 134+2*6+2, 65+11, 1, BLACK);
		  } else {
			  hagl_draw_rounded_rectangle(133, 65, 134+2*6+2, 65+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(8, 65, 9+4*6+2, 65+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(73, 65, 74+2*6+1, 65+11, 1, BLACK);
		  }

		  if (buttonConfirm) {
			  buttonConfirm = false;

			  if ( (encoder_value % 3) == 0) {
				  locationMultiplier = 0.5;
			  } else if ( (encoder_value % 3) == 1) {
				  locationMultiplier = 1;
			  } else {
				  locationMultiplier = 3;
			  }

			  hagl_put_text(L"Enter armor multiplier:", 5, 81, YELLOW, font6x9);
			  hagl_put_text(L"x1/2", 10, 81+12, YELLOW, font6x9);
			  hagl_put_text(L"x1", 75, 81+12, YELLOW, font6x9);
			  hagl_put_text(L"x2", 135, 81+12, YELLOW, font6x9);
			  state = STATE_WAITING_FOR_REDUCTION;
		  }

		  break;

	  case STATE_WAITING_FOR_REDUCTION:

		  if ( (encoder_value % 3) == 0) {
			  hagl_draw_rounded_rectangle(8, 91, 9+4*6+2, 91+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(73, 91, 74+2*6+1, 91+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(133, 91, 134+2*6+2, 91+11, 1, BLACK);
		  } else if ( (encoder_value % 3) == 1) {
			  hagl_draw_rounded_rectangle(73, 91, 74+2*6+1, 91+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(8, 91, 9+4*6+2, 91+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(133, 91, 134+2*6+2, 91+11, 1, BLACK);
		  } else {
			  hagl_draw_rounded_rectangle(133, 91, 134+2*6+2, 91+11, 1, GREEN);
			  hagl_draw_rounded_rectangle(8, 91, 9+4*6+2, 91+11, 1, BLACK);
			  hagl_draw_rounded_rectangle(73, 91, 74+2*6+1, 91+11, 1, BLACK);
		  }

		  if (buttonConfirm) {
			  buttonConfirm = false;

			  if ( (encoder_value % 3) == 0) {
				  armorTypeMultiplier = 0.5;
			  } else if ( (encoder_value % 3) == 1) {
				  armorTypeMultiplier = 1;
			  } else {
				  armorTypeMultiplier = 2;
			  }

			  state = STATE_CALCULATION;

		  }

		  break;

	  case STATE_CALCULATION:

		  if ( (rollATK - rollDEF >= 7) && (rollATK - rollDEF <= 9) ) {
			  critDMG = 3;
		  } else if ( (rollATK - rollDEF >= 10) && (rollATK - rollDEF <= 12) ) {
			  critDMG = 5;
		  } else if ( (rollATK - rollDEF >= 13) && (rollATK - rollDEF <= 14) ) {
			  critDMG = 8;
		  } else if ( rollATK - rollDEF >= 15 ) {
			  critDMG = 10;
		  }

		  totalDMG = floor(floor((enteredDMG - targetARMOR) * armorTypeMultiplier) * locationMultiplier) + critDMG;

		  hagl_draw_line(0, 103, LCD_WIDTH, 103, YELLOW);

		  if (totalDMG > 0) {
			  textStringCombiner(totalDMG, L"TOTAL DMG:", RED, 5, 107);
			  textStringCombiner( (targetHP - totalDMG), L"HP left:", RED, 85, 107);

			  switch (critDMG) {
			  case 3:
				  hagl_put_text(L"LIGHT CRITICAL WOUND", 5, 117, RED, font6x9);
				  break;

			  case 5:
				  hagl_put_text(L"MEDIUM CRITICAL WOUND", 5, 117, RED, font6x9);
				  break;

			  case 8:
				  hagl_put_text(L"HEAVY CRITICAL WOUND", 5, 117, RED, font6x9);
				  break;

			  case 10:
				  hagl_put_text(L"DEADLY CRITICAL WOUND", 5, 117, RED, font6x9);
				  break;
			  default:
				  break;
			  }

		  } else {
			  textStringCombiner(0, L"TOTAL DMG:", GREEN, 5, 107);
		  }

		  if (buttonConfirm) {
			  buttonConfirm = false;
			  totalDMG = 0;
			  critDMG = 0;
			  enteredDMG = 0;
			  targetARMOR = 0;
			  armorTypeMultiplier = 0;
			  locationMultiplier = 0;
			  hagl_clear_screen();
			  state = STATE_INIT;
		  }

		  break;

		  }
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
