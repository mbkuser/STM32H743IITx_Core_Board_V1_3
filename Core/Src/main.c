/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sdram.h"
//добавил git
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

TIM_HandleTypeDef htim17;

UART_HandleTypeDef huart1;

SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN PV */

/* Большой буфер в SDRAM */
SDRAM_ATTR uint8_t  video_framebuffer[800 * 480 * 2];  /* RGB565 буфер кадра */
SDRAM_ATTR uint32_t audio_buffer[48000];               /* 1 секунда аудио */

extern volatile uint32_t tick_led;
extern volatile uint8_t key0_pressed_flag;
extern volatile uint8_t key0_released_flag;
extern volatile uint8_t wk_up_pressed_flag;

//extern volatile uint32_t *buf;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_FMC_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM17_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ── Символы из линкерного скрипта ── */
extern uint32_t _sitcm_text;        /* начало секции в ITCM (VMA) */
extern uint32_t _eitcm_text;        /* конец секции в ITCM (VMA)  */
extern uint32_t _sitcm_text_load;   /* образ секции во Flash (LMA) */

volatile int32_t  count_speed;	/* для проверки быстродействия       */

/* =============================================================
   ДАННЫЕ В DTCM
   Нулевая латентность при обращении из ITCM обработчика
   ============================================================= */

/* ── Инициализированные переменные (.dtcm_data) ──
   Начальные значения копируются из Flash при старте           */

DTCM_DATA float pid_output_scale = 1000.0f;   /* масштаб ШИМ */

/* ── Неинициализированные переменные (.dtcm_bss) ──
   Обнуляются при старте, не занимают место во Flash           */

DTCM_BSS volatile int32_t  encoder_count;  /* текущий счёт энкодера  */
DTCM_BSS volatile int32_t  encoder_prev;   /* предыдущий счёт        */
DTCM_BSS volatile float    motor_speed;    /* скорость об/мин        */
DTCM_BSS volatile uint32_t tim17_tick;     /* счётчик вызовов ISR    */
DTCM_BSS volatile float    speed_setpoint; /* уставка скорости       */

/* ── Буфер для осциллограммы (не инициализируем) ──            */
DTCM_NOINIT float speed_log[256];          /* кольцевой буфер        */
DTCM_NOINIT uint8_t speed_log_idx;         /* индекс в буфере        */

/* Использование как обычных переменных */
ITCM_CODE
void fill_screen(uint16_t color)
{
/*
	  uint32_t odr;

	  // get current Output Data Register value
	  odr = GPIOB->ODR;

	  // Set selected pins that were at low level, and reset ones that were high
	  GPIOB->BSRR = ((odr & GPIO_PIN_1) << 16U) | (~odr & GPIO_PIN_1);
*/
	//HAL_GPIO_TogglePin(LED1_GREEN_GPIO_Port, LED1_GREEN_Pin);

	GPIOB->BSRR = GPIO_PIN_1;           // Установить пин

	//GPIOB->ODR ^= GPIO_PIN_1;  // Одна инструкция!

//    for (int i = 0; i < 800 * 480; i++)
//    {
//        ((uint16_t*)video_framebuffer)[i] = color;
//    }

    //memset(video_framebuffer, (uint8_t)color, 800 * 480 * 2);
//	for (count_speed = 0; count_speed < 300000; count_speed++){;}
//	for (int32_t i = 0; i < 300000; i++){int32_t x = i;}
	for (int32_t i = 0; i < 100; i++){;}

	//HAL_GPIO_TogglePin(LED1_GREEN_GPIO_Port, LED1_GREEN_Pin);

	GPIOB->BSRR = (GPIO_PIN_1 << 16);  // Сбросить пин

	//GPIOB->ODR ^= GPIO_PIN_1;  // Одна инструкция!

/*
	  // get current Output Data Register value
	  odr = GPIOB->ODR;

	  // Set selected pins that were at low level, and reset ones that were high
	  GPIOB->BSRR = ((odr & GPIO_PIN_1) << 16U) | (~odr & GPIO_PIN_1);
*/
}

/* Явное включение TCM — безопасно вызывать даже если уже включена */
void TCM_Enable(void)
{
    /* Включаем ITCM */
    SCB->ITCMCR |= SCB_ITCMCR_EN_Msk      /* Enable          */
                |  SCB_ITCMCR_RMW_Msk     /* Read-Modify-Write */
                |  SCB_ITCMCR_RETEN_Msk;  /* Retention (сохранение при sleep) */

    /* Включаем DTCM */
    SCB->DTCMCR |= SCB_DTCMCR_EN_Msk
                |  SCB_DTCMCR_RMW_Msk
                |  SCB_DTCMCR_RETEN_Msk;

    __DSB();   /* Data Synchronization Barrier  */
    __ISB();   /* Instruction Synchronization Barrier */
}

/*
Сейчас работает правка в файле /STM32H743IITx_Core_Board_V1_3/Core/Startup/startup_stm32h743iitx.s
Максимально раннее копирование. Трогаешь системный файл, CubeMX может перезаписать

...

SECTIONS
{
  // The startup code goes first into FLASH
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) // Startup code
    . = ALIGN(4);
  } >FLASH

//-----------------------------------------------
  // Секция для кода, исполняемого из ITCM: функции с атрибутом section(".itcm_text")
  .itcm_text :
  {
    . = ALIGN(4);
    _sitcm_text = .;	// начало секции в ITCM
    *(.itcm_text)       // Сюда попадут функции с атрибутом .itcm_text
    *(.itcm_text*)
    . = ALIGN(4);
    _eitcm_text = .;	// конец секции в ITCM
  } > ITCMRAM AT> FLASH	// LMA (загрузка) — Flash, VMA (исполнение) — ITCM :  — код хранится во Flash, но исполняется из ITCM
  // символ начала данных в Flash (откуда копировать при старте)
  _sitcm_text_load = LOADADDR(.itcm_text);	//LOADADDR — возвращает физический адрес во Flash для копирования при старте
//-----------------------------------------------

  // The program code and other data goes into FLASH
  .text :
  {
    . = ALIGN(4);
    *(.text)           // .text sections (code)
    *(.text*)          // .text* sections (code)
    *(.glue_7)         // glue arm to thumb code
    *(.glue_7t)        // glue thumb to arm code
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

....

	*/
//поэтому копировать ITCM секцию вручную не надо
void Copy_ITCM_Section(void)
{
    uint32_t *src = &_sitcm_text_load;
    uint32_t *dst = &_sitcm_text;
    while (dst < &_eitcm_text)
    {
        *dst++ = *src++;
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

    TCM_Enable();       /* ← первой строкой, хотя в H743 по умолчанию ITCM и DTCM, включены */

/*
	Перенос таблицы векторов в DTCM
	Когда возникает прерывание, ядро Cortex-M7 делает два обращения к памяти перед тем как начать исполнять наш обработчик:

		Прерывание сработало
		        │
		        ▼
		┌───────────────────────────────────┐
		│  1. Читает адрес из VTOR + offset │  ← обращение к таблице векторов
		│     (например VTOR + 0xA0 = SPI1) │
		└───────────────────────────────────┘
		        │
		        ▼
		┌───────────────────────────────────┐
		│  2. Переходит по этому адресу     │  ← обращение к самой функции
		│     и начинает исполнять код      │
		└───────────────────────────────────┘

	Откуда читается таблица по умолчанию — Flash или ITCM?
	На STM32H743 после сброса SCB->VTOR = 0x08000000 — таблица векторов лежит во Flash.
	Доступ к Flash на H743 выглядит так:

	CPU → AXI Bus Matrix → Flash контроллер → Flash
	         ↑
	    3–8 тактов задержки (wait states)
	    + ART Accelerator / I-Cache помогают,
	      но НЕ для таблицы векторов напрямую

	Что даёт перенос в DTCM
	DTCM подключена к ядру напрямую, как и ITCM:

	                    ┌─── ITCM Bus ───► ITCM RAM (код функции)
	CPU Cortex-M7 ──────┤
	                    └─── DTCM Bus ───► DTCM RAM (таблица векторов)

	Оба доступа: 0 wait states, 1 такт

	То есть при переносе таблицы в DTCM:
	Шаг							Flash (по умолчанию)	DTCM (после переноса)
	Чтение адреса вектора		3–8 тактов				1 такт
	Переход к функции (ITCM)	уже 1 такт				1 такт
	Итого накладные расходы		~4–9 тактов лишних		0 лишних

	Таблица должна быть скопирована до того как разрешишь прерывания. В main() это безопасно.

	HAL_Init() внутри вызывает HAL_NVIC_SetPriorityGrouping() и настраивает SysTick — это уже использует прерывания.
	Поэтому перенос таблицы должен быть строго до HAL_Init().

	DTCM всего 128 KB. 1 KB на таблицу — это немного, но учитывай если DTCM уже плотно занята стеком и данными.

	Перенос таблицы векторов в DTCM в паре с переносом обработчика(ов) в ITCM вместе они дают минимально возможную латентность прерывания на этом МК.
*/
    /* ── Шаг 1: Перенос таблицы векторов в DTCM ── */
    #define VTOR_NEW_ADDR  0x20000000UL
    #define VTOR_OLD_ADDR  0x08000000UL
    #define VTOR_SIZE      (256U * 4U)   /* 1024 байт для H743 */

    memcpy((void*)VTOR_NEW_ADDR, (void*)VTOR_OLD_ADDR, VTOR_SIZE);
    SCB->VTOR = VTOR_NEW_ADDR;
    __DSB();   /* дождаться записи */
    __ISB();   /* сбросить конвейер */
    /*__DSB() и __ISB() критически важны! Без них ядро может не увидеть новое значение VTOR из-за конвейера.*/

    /* ── Шаг 2: Обычная инициализация ── */
    //HAL_Init();
    //SystemClock_Config();
    //MX_SPI1_Init();


    /* Проверяем состояние TCM регистров */
    uint32_t itcmcr = SCB->ITCMCR;
    uint32_t dtcmcr = SCB->DTCMCR;

    /* Бит 0 = EN, Бит 2 = RMW, Бит 3 = RETEN */
    /* На H743 после сброса должно быть: */
    /* ITCMCR = 0x00000001 (минимум EN=1)  */
    /* DTCMCR = 0x00000001 (минимум EN=1)  */

    if (!(itcmcr & SCB_ITCMCR_EN_Msk))
    {
        /* ITCM выключена — неожиданно для H743 */
        Error_Handler();
    }

    if (!(dtcmcr & SCB_DTCMCR_EN_Msk))
    {
        /* DTCM выключена — неожиданно для H743 */
        Error_Handler();
    }
  //Copy_ITCM_Section();   // ← ПЕРВЫМ делом, до HAL_Init()

    /* Проверяем что ITCM заполнена (не нули) */
    uint32_t *itcm = (uint32_t*)0x00000000;
    if (itcm[0] == 0x00000000)
    {
        /* Копирование не произошло — что-то не так */
        Error_Handler();
    }

/*

DTCM — Data Tightly Coupled Memory
128 КБ · 0x20000000 · прямой доступ CPU
Латентность CPU		0 тактов ожидания
D-Cache				не нужен — итак быстрая
DMA1 / DMA2			нет доступа (аппаратно)
MDMA				есть доступ

Рекомендуется для
▸ Стек программы — уже здесь по умолчанию. Каждый вызов функции, push/pop = 0 WS.
▸ Переменные ISR — счётчики, флаги, накопители внутри обработчиков прерываний.
▸ PID и регуляторы реального времени — ошибка, интеграл, уставки PWM, данные энкодера.
▸ Стеки и TCB задач FreeRTOS — ядро RTOS обращается к стеку при каждом переключении задачи.
▸ Временные буферы вычислений — только если к ним не обращается DMA.

// В STM32H743xITx_FLASH.ld добавить:
DTCM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K

// Стек уже в DTCM:
_estack = ORIGIN(DTCM) + LENGTH(DTCM);

// Секция для переменных в DTCM:
.dtcm_data (NOLOAD) :
{
  *(.dtcmram)
  *(.dtcmram*)
} >DTCM

// В коде:
__attribute__((section(".dtcmram")))
volatile int32_t encoder_count = 0;

__attribute__((section(".dtcmram")))
float pid_integral = 0.0f;
*/

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();
  //SCB_DisableDCache();
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
  MX_FMC_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  MX_TIM17_Init();
  /* USER CODE BEGIN 2 */



//  printf("SYSCLK=%lu\r\n", HAL_RCC_GetSysClockFreq());
//  printf("HCLK=%lu\r\n", HAL_RCC_GetHCLKFreq());
//  uint32_t v_SYSCLK = HAL_RCC_GetSysClockFreq();	//SYSCLK=480000000
//  uint32_t v_HCLK   = HAL_RCC_GetHCLKFreq();		//HCLK=240000000 Если это так, дальше считаем, что SDCLK = 120 МГц.

  // Включение счётчика тактов (DWT_CYCCNT)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  // Пауза для стабилизации
  HAL_Delay(100);

  /* Инициализация самой SDRAM */
  if (SDRAM_Init(&hsdram1) != HAL_OK)
  {
      Error_Handler();  /* Мигаем светодиодом или встаём в бесконечный цикл */
  }

  /* Теперь SDRAM доступна как обычная память начиная с 0xC0000000 */
  uint32_t *sdram = (uint32_t *)SDRAM_BASE_ADDR;

  /* Пример записи */
  sdram[0] = 0xDEADBEEF;

  /* Пример чтения */
  uint32_t val = sdram[0];  /* должно быть 0xDEADBEEF */
  if(val != 0xDEADBEEF)
  {
      Error_Handler();  /* Мигаем светодиодом или встаём в бесконечный цикл */
  }

//  fill_screen(0xABCD);
//  fill_screen(0x1234);

  if (SDRAM_Test() != HAL_OK)
  {
      Error_Handler();  /* Мигаем светодиодом или встаём в бесконечный цикл */
  }

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED1_GREEN_Pin, GPIO_PIN_RESET);

  HAL_TIM_Base_Start_IT(&htim17);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if(tick_led >= 500){
		  tick_led=0;
		  HAL_GPIO_TogglePin(LED0_RED_GPIO_Port, LED0_RED_Pin);
		  //HAL_GPIO_TogglePin(LED1_GREEN_GPIO_Port, LED1_GREEN_Pin);

		  //fill_screen(0x1234);

		  SDRAM_Performance();

	      /* Главный цикл читает данные из DTCM — тоже быстро */
	      //float spd = motor_speed;          /* DTCM переменная */
	      //uint32_t ticks = tim17_tick;      /* DTCM переменная */
	  }

	  //HAL_Delay(500); // Пауза 500 мс

      if (key0_pressed_flag)
      {
          key0_pressed_flag = 0;

          // Здесь выполняем нужное действие при нажатии KEY0
          // Например:
		  fill_screen(0x1234);
		  HAL_GPIO_TogglePin(LED0_RED_GPIO_Port, LED0_RED_Pin);
      }

      if (key0_released_flag)
      {
          key0_released_flag = 0;

          // Действие при отпускании (например, выключить светодиод)
		  fill_screen(0x5678);
		  HAL_GPIO_TogglePin(LED0_RED_GPIO_Port, LED0_RED_Pin);
      }

      if(wk_up_pressed_flag)
      {
    	  wk_up_pressed_flag = 0;

		  fill_screen(0xABCD);
		  HAL_GPIO_TogglePin(LED0_RED_GPIO_Port, LED0_RED_Pin);
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 15;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM17 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM17_Init(void)
{

  /* USER CODE BEGIN TIM17_Init 0 */
	/*
	F_timer = Clock / ((Prescaler + 1) * (Period + 1))

	Допустим, нужно получить частоту прерываний
	1000 Гц (период 1 мс)
	на таймере TIM17 с тактовой частотой 240 МГц.
	PSC выбираем равным 23999. Подставляем в формулу:
	(Period + 1) = Clock / ((PSC + 1) * F_timer) = 240 000 000 / (24000 * 1000) = 10
	Таким образом, получаем Period = 9.

	100 Гц(10 мс)
	PSC выбираем равным 23999. Подставляем в формулу:
	(Period + 1) = Clock / ((PSC + 1) * F_timer) = 240 000 000 / (24000 * 100) = 100
	Period = 99.

	1 МГц
	PSC выбираем равным 23. Подставляем в формулу:
	(Period + 1) = Clock / ((PSC + 1) * F_timer) = 240 000 000 / (24 * 1000 000) = 10
	Period = 9.

	100 КГц(0,00001 сек)
	PSC выбираем равным 23. Подставляем в формулу:
	(Period + 1) = Clock / ((PSC + 1) * F_timer) = 240 000 000 / (24 * 100 000) = 100
	Period = 99.

	Типовые значения PSC/ARR для разных частот
	Нужная частота			PSC			ARR			Реальная частота
	1 Гц					23999		9999		1,000 Гц
	10 Гц					2399		9999		10,000 Гц
	1 кГц					239			999			1 000,0 Гц
	10 кГц					23			999			10 000,0 Гц
	100 кГц					23			99			100 000,0 Гц
	1 МГц					0			239			1 000 000,0 Гц

	*/

  /* USER CODE END TIM17_Init 0 */

  /* USER CODE BEGIN TIM17_Init 1 */

  /* USER CODE END TIM17_Init 1 */
  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 23999;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 9;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM17_Init 2 */

  /* USER CODE END TIM17_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/* FMC initialization function */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 9;
  SdramTiming.SelfRefreshTime = 6;
  SdramTiming.RowCycleDelay = 8;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 3;
  SdramTiming.RCDDelay = 3;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */

  /* USER CODE END FMC_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED0_RED_Pin|LED1_GREEN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : WK_UP_Pin */
  GPIO_InitStruct.Pin = WK_UP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(WK_UP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY0_Pin */
  GPIO_InitStruct.Pin = KEY0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED0_RED_Pin LED1_GREEN_Pin */
  GPIO_InitStruct.Pin = LED0_RED_Pin|LED1_GREEN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(WK_UP_EXTI_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(WK_UP_EXTI_IRQn);

  HAL_NVIC_SetPriority(KEY0_EXTI_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(KEY0_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
  //  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  //  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
	  HAL_GPIO_TogglePin(LED0_RED_GPIO_Port, LED0_RED_Pin);
//	  HAL_GPIO_TogglePin(LED1_GREEN_GPIO_Port, LED1_GREEN_Pin);
	  HAL_Delay(500); // Пауза 500 мс
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
