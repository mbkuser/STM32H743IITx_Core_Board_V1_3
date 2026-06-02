/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* ── Структура PID регулятора ── */
typedef struct {
    float kp;           /* коэффициент пропорциональный */
    float ki;           /* коэффициент интегральный     */
    float kd;           /* коэффициент дифференциальный */
    float integral;     /* накопленная интегральная часть */
    float prev_error;   /* предыдущая ошибка для D-части */
    float output_min;   /* ограничение выхода снизу      */
    float output_max;   /* ограничение выхода сверху     */
} PID_TypeDef;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
float PID_Compute(PID_TypeDef *pid, float setpoint, float measured);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define WK_UP_Pin GPIO_PIN_0
#define WK_UP_GPIO_Port GPIOA
#define WK_UP_EXTI_IRQn EXTI0_IRQn
#define KEY0_Pin GPIO_PIN_1
#define KEY0_GPIO_Port GPIOA
#define KEY0_EXTI_IRQn EXTI1_IRQn
#define LED0_RED_Pin GPIO_PIN_0
#define LED0_RED_GPIO_Port GPIOB
#define LED1_GREEN_Pin GPIO_PIN_1
#define LED1_GREEN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
/* ── Макросы для секций ── */
#define ITCM_CODE   __attribute__((section(".itcm_text"),  noinline))	//Зачем noinline? Если функция будет заинлайнена куда-то во Flash, смысл теряется.
#define DTCM_DATA   __attribute__((section(".dtcm_data")))   /* с начальным значением  */
#define DTCM_BSS    __attribute__((section(".dtcm_bss")))    /* обнуляется при старте  */
#define DTCM_NOINIT __attribute__((section(".dtcm_noinit")))/* не трогается при старте */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
