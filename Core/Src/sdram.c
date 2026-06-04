/*
 * sdram.c
 *
 *  Created on: 12 мая 2026 г.
 *      Author: KUTUZOV
 */

#include "sdram.h"
#include <string.h>
#include "main.h"


uint32_t * const sdram = (uint32_t *)0xC0000000;

uint32_t *src = (uint32_t*)0xC0000000;
uint32_t *dst = (uint32_t*)0xC0400000;

/**
 * @brief  Полная инициализационная последовательность SDRAM
 *         согласно спецификации W9825G6KH-6
 * @param  hsdram: указатель на дескриптор SDRAM HAL
 * @retval HAL_OK при успехе
 */
HAL_StatusTypeDef SDRAM_Init(SDRAM_HandleTypeDef *hsdram)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};
    uint32_t tmpr = 0;

    /* ---------------------------------------------------------------
       Шаг 1: Команда CLOCK CONFIGURATION ENABLE
       Включает тактовый сигнал SDCLK. После подачи нужно выждать
       минимум 100 мкс (2 такта SDCLK по спецификации W9825G6KH-6).
    --------------------------------------------------------------- */
    cmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK)
        return HAL_ERROR;

    /* Ждём 200 мкс — требование даташита раздел 7.1.
       HAL_Delay(1) = 1 мс, что в 5 раз больше минимума — с запасом.
    ЕДИНСТВЕННАЯ программная задержка.
    */

    HAL_Delay(1);  /* минимум 1 мс для надёжности */

/*
Даташит раздел 7.1 прямо говорит:
"After power up, an initial pause of 200 µS is required followed by a precharge of all banks"
Это физическое требование — внутренние схемы SDRAM (усилители строк, схемы смещения, внутренние опорные напряжения)
должны стабилизироваться после того как начал работать тактовый сигнал. До истечения этих 200 мкс SDRAM не гарантирует
корректную реакцию ни на какую команду.

Почему задержка только после шага 1
После подачи команды CLK_ENABLE контроллер FMC начинает генерировать SDCLK на ножку микросхемы.
Именно с этого момента отсчитывается 200 мкс. До этого момента SDCLK отсутствовал — отсчитывать было нечего.
После всех остальных шагов явная программная задержка не нужна по одной причине: FMC сам соблюдает все тайминги
между командами через запрограммированные регистры. Разберём каждый шаг:
Шаг 2 — PALL (Precharge All):
После завершения команды FMC автоматически выдерживает паузу tRP (Row Precharge time). Вы это запрограммировали
в регистре TRP=3. FMC не примет следующую команду пока tRP не истечёт. HAL_SDRAM_SendCommand возвращает управление
только когда FMC готов.
Шаг 3 — 8× Auto-Refresh:
Между каждым циклом регенерации FMC выдерживает паузу tRC (Row Cycle time) — это ваш регистр TRC=8. Все 8 циклов
выполняются аппаратно с правильными паузами. Никакого HAL_Delay не нужно.
Шаг 4 — Load Mode Register:
После записи Mode Register SDRAM требует паузу tRSC = 2 CLK перед следующей командой. Это именно то, для чего существует
регистр TMRD=2. FMC отсчитывает эти 2 такта аппаратно.

Если нужна точная задержка 200 мкс
HAL_Delay(1) даёт 1 мс вместо 200 мкс — это избыточно, но безопасно. Если критична скорость инициализации, можно использовать
DWT счётчик циклов:

// Точная задержка через DWT (CoreDebug должен быть включён)
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

uint32_t cycles_200us = (SystemCoreClock / 1000000) * 200; // 200 мкс в тактах
while (DWT->CYCCNT < cycles_200us);
*/

    /* ---------------------------------------------------------------
       Шаг 2: Команда PRECHARGE ALL BANKS (PALL)
       Закрывает все открытые строки во всех банках.
    --------------------------------------------------------------- */
    cmd.CommandMode            = FMC_SDRAM_CMD_PALL;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    /* задержка не нужна */
    /* ---------------------------------------------------------------
       Шаг 3: AUTO-REFRESH — минимум 8 циклов
       Заряжает внутренние конденсаторы памяти перед первым
       программированием Mode Register.
    --------------------------------------------------------------- */
    cmd.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 8;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    /* задержка не нужна */
    /* ---------------------------------------------------------------
       Шаг 4: LOAD MODE REGISTER
       Конфигурирует внутренние параметры SDRAM:
         - Burst Length = 1 (оптимально для произвольного доступа)
         - Burst Type = Sequential
         - CAS Latency = 3 (безопаснее при 100+ МГц)
         - Write Burst = Single (запись без burst)
    --------------------------------------------------------------- */
    tmpr = SDRAM_MODEREG_BURST_LENGTH_1            |
           SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL     |
           SDRAM_MODEREG_CAS_LATENCY_3             |
           SDRAM_MODEREG_OPERATING_MODE_STANDARD   |
           SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = tmpr;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    /* задержка не нужна */
    /* ---------------------------------------------------------------
       Шаг 5: Настройка периода Auto-Refresh

       Формула: COUNT = (SDCLK_freq × tREF) / num_rows − 20

       При SDCLK = 100 МГц, tREF = 64 мс, строк = 8192:
         COUNT = (100_000_000 × 0.064) / 8192 − 20
               = 6_400_000 / 8192 − 20
               = 781.25 − 20
               ≈ 761

       При SDCLK = 120 МГц, tREF = 64 мс, строк = 8192:
         COUNT = (120_000_000 × 0.064) / 8192 − 20
               = 7_680_000 / 8192 − 20
               = 937.5 − 20
               ≈ 917

       Регистр SDRTR задаётся в тактах SDCLK.
       Значение должно быть чуть меньше расчётного для надёжности.
    --------------------------------------------------------------- */
//    HAL_SDRAM_ProgramRefreshRate(hsdram, 761);	//100MHz
    HAL_SDRAM_ProgramRefreshRate(hsdram, 917);	//120 MHz

    return HAL_OK;
}

/**
 * @brief  Тест SDRAM: запись-чтение паттернов
 * @retval HAL_OK — тест пройден, HAL_ERROR — обнаружена ошибка
 */
HAL_StatusTypeDef SDRAM_Test(void)
{
    volatile uint32_t *sdram = (volatile uint32_t *)SDRAM_BASE_ADDR;
    uint32_t size_words = SDRAM_SIZE / 4;  /* Количество 32-битных слов */

    /* --- Тест 1: Walking 1s (бегущая единица) --- */
    for (uint32_t i = 0; i < 32; i++)
    {
        sdram[0] = (1UL << i);
        SCB_CleanInvalidateDCache_by_Addr((uint32_t*)sdram, 4);
        if (sdram[0] != (1UL << i)) return HAL_ERROR;
    }

    /* --- Тест 2: Паттерн 0xAAAA5555 — чередование бит --- */
    for (uint32_t i = 0; i < size_words; i++)
        sdram[i] = (i % 2 == 0) ? 0xAAAA5555UL : 0x5555AAAAUL;

    SCB_CleanInvalidateDCache();  /* Сбрасываем кэш перед верификацией */

    for (uint32_t i = 0; i < size_words; i++)
    {
        uint32_t expected = (i % 2 == 0) ? 0xAAAA5555UL : 0x5555AAAAUL;
        if (sdram[i] != expected) return HAL_ERROR;
    }

    /* --- Тест 3: Адресный тест — значение = адрес --- */
    for (uint32_t i = 0; i < size_words; i++)
        sdram[i] = i;

//    SCB_CleanInvalidateDCache();
    __DSB();
    __ISB();

    for (uint32_t i = 0; i < size_words; i++)
    {
        if (sdram[i] != i) return HAL_ERROR;
    }

    /* --- Тест 4: Паттерн 0x00000000 --- */
    for(uint32_t i=0;i<size_words;i++)
        sdram[i]=0x00000000UL;

    __DSB();
    __ISB();

    for (uint32_t i = 0; i < size_words; i++)
    {
        if (sdram[i] != 0x00000000UL) return HAL_ERROR;
    }

    /* --- Тест 5: Паттерн 0xFFFFFFFF --- */
    for(uint32_t i=0;i<size_words;i++)
        sdram[i]=0xFFFFFFFFUL;

    __DSB();
    __ISB();

    for (uint32_t i = 0; i < size_words; i++)
    {
        if (sdram[i] != 0xFFFFFFFFUL) return HAL_ERROR;
    }


    return HAL_OK;
}

void SDRAM_Performance(void)
{
	// Измерить скорость записи
	uint32_t start,end;

	GPIOB->BSRR = GPIO_PIN_1;           // Установить пин
	start = DWT->CYCCNT;
	for (uint32_t i = 0; i < TEST_WORDS; i += 8) {
	    sdram[i]   = i;
	    sdram[i+1] = i+1;
	    sdram[i+2] = i+2;
	    sdram[i+3] = i+3;
	    sdram[i+4] = i+4;
	    sdram[i+5] = i+5;
	    sdram[i+6] = i+6;
	    sdram[i+7] = i+7;
	}	/* дополнительно читаем последнее слово */
	volatile uint32_t tmp = sdram[TEST_WORDS - 1];
	__DSB();   // Data Synchronization Barrier
	end = DWT->CYCCNT;
	GPIOB->BSRR = (GPIO_PIN_1 << 16);  // Сбросить пин

	float time_s_wr =	(float)(end-start)/480000000.0f;
	float mbps_wr = (TEST_WORDS*4.0f)/time_s_wr/1024.0f/1024.0f;	//скорость записи MB/s

	GPIOB->BSRR = (GPIO_PIN_1 << 16);  // Сбросить пин
//	GPIOB->BSRR = GPIO_PIN_1;           // Установить пин
	/*

	//---------------------------------------------------
	//Измерить скорость чтения
	volatile uint32_t sum=0;

	start = DWT->CYCCNT;
	for(uint32_t i=0;i<TEST_WORDS;i++)
	{
	    sum += sdram[i];
	}
	end = DWT->CYCCNT;
	float time_s_rd =	(float)(end-start)/480000000.0f;
	float mbps_rd = (TEST_WORDS*4.0f)/time_s_rd/1024.0f/1024.0f;	//скорость чтения MB/s

	start = DWT->CYCCNT;
	for(uint32_t i=0;i<TEST_WORDS;i+=16)
	{
	    sum += sdram[i];
	}
	end = DWT->CYCCNT;
	time_s_rd =	(float)(end-start)/480000000.0f;
	mbps_rd = ((TEST_WORDS/16)*4.0f)/time_s_rd/1024.0f/1024.0f;	//скорость чтения MB/s через 16


	start = DWT->CYCCNT;
	for(uint32_t i=0;i<TEST_WORDS;i+=64)
	{
	    sum += sdram[i];
	}
	end = DWT->CYCCNT;
	time_s_rd =	(float)(end-start)/480000000.0f;
	mbps_rd = ((TEST_WORDS/64)*4.0f)/time_s_rd/1024.0f/1024.0f;	//скорость чтения MB/s через 64


	start = DWT->CYCCNT;
	for(uint32_t i=0;i<TEST_WORDS;i+=256)
	{
	    sum += sdram[i];
	}
	end = DWT->CYCCNT;
	time_s_rd =	(float)(end-start)/480000000.0f;
	mbps_rd = ((TEST_WORDS/256)*4.0f)/time_s_rd/1024.0f/1024.0f;	//скорость чтения MB/s через 256
//---------------------------------------------------

	start = DWT->CYCCNT;
	memcpy(dst, src, 4*1024*1024);
	end = DWT->CYCCNT;
	time_s_wr =	(float)(end-start)/480000000.0f;
	mbps_wr = (TEST_WORDS*4.0f)/time_s_wr/1024.0f/1024.0f;	//скорость записи memcpy MB/s
*/

//	GPIOB->BSRR = (GPIO_PIN_1 << 16);  // Сбросить пин -------------DEBAG

}

/*
1. Характеристики чипов
W9825G6KH-6I — синхронная DRAM (SDRAM) компании Winbond:

Ёмкость: 256 Мбит (32 МБайт), организация 4M × 4Банка × 16 бит
Шина данных: 16 бит
Максимальная частота: 166 МГц (суффикс -6 означает 6 нс цикл)
4 банка, 8192 строки, 512 столбцов
Напряжение питания: 3,3 В
Корпус: 54-pin TSOP-II

STM32H743IIT6 имеет встроенный контроллер FMC (Flexible Memory Controller), который поддерживает
подключение SDRAM с доступом через адресное пространство от 0xC0000000 (Bank 5) или 0xD0000000 (Bank 6).

2. Принципиальная схема подключения
Соответствие выводов (MCU → SDRAM)
Все сигналы FMC работают на 3,3 В и подходят напрямую к W9825G6KH-6.
Шина адреса (13 линий):
STM32H743                     W9825G6KH-6I              Описание
PF0 → FMC_A0                  A0
PF1 → FMC_A1                  A1
PF2 → FMC_A2                  A2
PF3 → FMC_A3                  A3
PF4 → FMC_A4                  A4
PF5 → FMC_A5                  A5
PF12 → FMC_A6                 A6
PF13 → FMC_A7                 A7
PF14 → FMC_A8                 A8
PF15 → FMC_A9                 A9
PG0 → FMC_A10                 A10                      BA0 — выбор банка
PG1 → FMC_A11                 A11                      BA1 — выбор банка
PG2 → FMC_A12                 A12

Шина данных (16 линий):
STM32H743                     W9825G6KH-6I
PD14 → FMC_D0                 DQ0
PD15 → FMC_D1                 DQ1
PD0 → FMC_D2                  DQ2
PD1 → FMC_D3                  DQ3
PE7 → FMC_D4                  DQ4
PE8 → FMC_D5                  DQ5
PE9 → FMC_D6                  DQ6
PE10 → FMC_D7                 DQ7
PE11 → FMC_D8                 DQ8
PE12 → FMC_D9                 DQ9
PE13 → FMC_D10                DQ10
PE14 → FMC_D11                DQ11
PE15 → FMC_D12                DQ12
PD8 → FMC_D13                 DQ13
PD9 → FMC_D14                 DQ14
PD10 → FMC_D15                DQ15

Управляющие сигналы:
STM32H743                     W9825G6KH-6I              Описание
PG8 → FMC_SDCLK               CLK                      Тактовый сигнал
PH2 → FMC_SDCKE0              CKE                      Разрешение тактирования
PH3 → FMC_SDNE0               CS#                      Выбор микросхемы (активный LOW)
PF11 → FMC_SDNRAS             RAS#                     Строб строки (активный LOW)
PG15 → FMC_SDNCAS             CAS#                     Строб столбца (активный LOW)
PH5 → FMC_SDNWE               WE#                      Разрешение записи (активный LOW)
PE0 → FMC_NBL0                LDQM                     Маска младшего байта
PE1 → FMC_NBL1                UDQM                     Маска старшего байта

Используйем Bank 1 SDRAM (SDNE0/SDCKE0) — это соответствует базовому адресу 0xC0000000.

3. Требования к трассировке PCB
Это критически важный раздел — плохая разводка погубит даже правильно написанный код:

Все линии FMC к SDRAM должны быть одинаковой длины (skew matching ±10 мил). Особенно важно для CLK и линий данных.
Подтягивающие/байпасные конденсаторы 100 нФ керамика как можно ближе к каждому выводу питания SDRAM (VDD/VDDQ).
Один конденсатор 10 мкФ на банк питания рядом с микросхемой.
Не прокладывайте линии FMC под высокочастотными компонентами.
Минимальная ширина сигнальных дорожек — 0,1 мм, импеданс 50 Ом.

4. Настройка в STM32CubeMX
--------------------------
Шаг 1. Включите FMC: Connectivity → FMC → SDRAM 1 → поставьте галочку.

Шаг 2. Параметры банка (Bank 1):
Параметр                       Значение
Clock period                   2 (т.е. HCLK/2)
Read pipe delay                1
Burst read                     Enabled
Memory type                    SDRAM
Memory data bus width          16 bits
Number of column address bits  9
Number of row address bits     13
CAS latency                    3
Write protection               Disabled
SDRAM common clock             2
Number of internal banks       4

Шаг 3. Тайминги (для CLK = 100 МГц, т.е. HCLK/2 = 100 МГц при HCLK = 200 МГц):
(Настройка для HCLK=480 MHz приведена ниже: Расчёт для Шаг 3. Тайминги для SYSCLK = 480 МГц, To FMC = 240 МГц)
Параметр                       Значение                Описание
TMRD                           2                       Load Mode Register to Active
TXSR                           7                       Exit self-refresh delay
TRAS                           4                       Self-refresh time
TRC                            7                       Row cycle delay
TWR                            2                       Write recovery time
TRP                            2                       Row precharge delay
TRCD                           2                       Row to column delay

Если ваш HCLK = 400 МГц (максимум H743), то SDCLK = 200 МГц — проверьте datasheet:
W9825G6KH-6 поддерживает до 166 МГц, поэтому делитель нужно поставить /3 или /4.
При HCLK = 400 МГц используйте делитель /3 → SDCLK ≈ 133 МГц.

Шаг 4. Настройка GPIO: CubeMX сделает это автоматически — все пины FMC будут выставлены в режим AF (Alternate Function),
очень высокая скорость (Very High), без подтяжек.
Шаг 5. Включите MPU (Memory Protection Unit):
В System Core → CORTEX_M7 настройте регион для SDRAM:

Базовый адрес: 0xC0000000
Размер: 32 МБ
Атрибуты: Normal, Cacheable, Bufferable, Shareable

5. Код инициализации SDRAM
После генерации кода CubeMX создаст файл MX_FMC_Init().
Инициализация SDRAM требует отправки специальной последовательности команд.
В файле sdram.c это HAL_StatusTypeDef SDRAM_Init(SDRAM_HandleTypeDef *hsdram).

6. Вызов инициализации в main.c после MX_FMC_Init()
#include "sdram.h"

SDRAM_HandleTypeDef hsdram1;

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_FMC_Init();   // Инициализация периферии FMC (HAL)

    // Инициализация самой SDRAM
    if (SDRAM_Init(&hsdram1) != HAL_OK)
    {
        Error_Handler();  // Мигаем светодиодом или встаём в бесконечный цикл
    }

    // Теперь SDRAM доступна как обычная память начиная с 0xC0000000
    uint32_t *sdram = (uint32_t *)SDRAM_BASE_ADDR;

    // Пример записи
    sdram[0] = 0xDEADBEEF;

    // Пример чтения
    uint32_t val = sdram[0];  // должно быть 0xDEADBEEF

    while (1) { }
}

7. Настройка MPU и кэша (обязательно для H743)
STM32H743 имеет L1-кэш (ICache + DCache).
Без правильной настройки MPU вы получите устаревшие данные в кэше вместо актуального содержимого SDRAM.
Настройка MPU для региона SDRAM
Вызвать ДО HAL_Init() и MX_FMC_Init()

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  // Disables the MPU
  HAL_MPU_Disable();

  // Initializes and configures the Region and the memory to be protected

  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  // Enables the MPU
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

// Включить кэши ядра(уже есть после MPU_Config() в main())
SCB_EnableICache();
SCB_EnableDCache();

Если SDRAM используется для DMA (например, DMA2D, LTDC, кодек) —
поставьте IsBufferable = 0,
IsCacheable = 0
для того региона, куда DMA пишет. Иначе ядро будет читать кэшированные (устаревшие) данные.


8. Функция тестирования памяти
HAL_StatusTypeDef SDRAM_Test(void)

9. Использование SDRAM для размещения переменных
Вы можете явно разместить переменные или буферы в SDRAM через атрибуты и linker script.
В STM32H743xITx_FLASH.ld добавьте:(куда правильно добавить смотри ниже)

// В секции MEMORY добавить:
SDRAM (xrw) : ORIGIN = 0xC0000000, LENGTH = 32M

// В секции SECTIONS добавить:
.sdram_bss (NOLOAD) :
{
    . = ALIGN(4);
    *(.sdram_section)
    . = ALIGN(4);
} >SDRAM

В коде:
// Атрибут для размещения в SDRAM
#define SDRAM_ATTR __attribute__((section(".sdram_section")))

// Большой буфер в SDRAM
SDRAM_ATTR uint8_t  video_framebuffer[800 * 480 * 2];  // RGB565 буфер кадра
SDRAM_ATTR uint32_t audio_buffer[48000];               // 1 секунда аудио

// Использование как обычных переменных
void fill_screen(uint16_t color)
{
    for (int i = 0; i < 800 * 480; i++)
        ((uint16_t*)video_framebuffer)[i] = color;
}

10. Типичные проблемы и их решение
Проблема: данные читаются неверно / постоянно 0xFFFFFFFF
— Проверьте пины CLK и CKE в осциллографе. Если CLK не генерируется — FMC не настроен.
Убедитесь, что MX_FMC_Init() вызван до SDRAM_Init().
Проблема: первые несколько байт верны, остальные нет
— Скорее всего, проблема с таймингами.
Уменьшите частоту SDCLK (поставьте делитель /3 вместо /2) или увеличьте значения тайминга TRCD, TRP на 1.
Проблема: данные «зависают» через случайное время
— Неправильный период авторегенерации (refresh).
Пересчитайте SDRTR по формуле выше. При перегреве PCB refresh надо делать чаще.
Проблема: DMA записывает мусор вместо данных
— Кэш-когерентность. Перед запуском DMA вызовите SCB_CleanDCache_by_Addr(src, len),
после завершения DMA — SCB_InvalidateDCache_by_Addr(dst, len).
Проблема: сборка не помещает переменные в SDRAM
— Убедитесь, что linker script изменён и используется правильная секция .sdram_section.
Проверьте size утилитой — размер .sdram_bss должен быть ненулевым.

Это полный цикл от аппаратного подключения до рабочего кода.

------------------------------------------------------------------
PS

------------------------------------------------------------------
Cortex Memory Protection Unit Region 0 Settings:
------------------------------------------------
Поле в CubeMX                 Ваше значение             Правильно?
------------------------------------------------------------------
MPU Region                    Enabled                   ✅
MPU Region Base Address       0xC0000000                ✅
MPU Region Size               32MB                      ✅
MPU SubRegion Disable         0x0                       ✅ все подрегионы активны
MPU TEX field level           level 1                   ✅ Normal Non-cacheable
MPU Access Permission         ALL ACCESS PERMITTED      ✅
MPU Instruction Access        DISABLE                   ✅
MPU Shareability Permission   DISABLE                   ✅
MPU Cacheable Permission      DISABLE                   ✅
MPU Bufferable Permission     DISABLE                   ✅

MPU Instruction Access = DISABLE означает что процессор не может выполнять код из SDRAM —
это и есть XN бит (eXecute Never) в регионе MPU. Для SDRAM как области данных это правильно.
SDRAM используется как данные — буферы, переменные, куча. Код там не живёт. Если поставить ENABLE —
процессор может случайно перейти на адрес в SDRAM и начать интерпретировать данные как инструкции.
Это либо Hard Fault, либо непредсказуемое поведение.
Поэтому правильно ставить DISABLE — тогда любая попытка выполнить код из SDRAM немедленно вызывает
исключение, и вы сразу видите ошибку.

MPU SubRegion Disable = 0x0
Это поле делит регион на 8 равных частей. Каждый бит отключает одну часть. 0x0 означает все 8 частей
включены — то есть весь регион 32 МБ активен.

Расчёт для Шаг 3. Тайминги для SYSCLK = 480 МГц, To FMC = 240 МГц
-----------------------------------------------------------------
SDCLK = HCLK3 / 2 → 120 МГц; SDCLK = HCLK3 / 3 → 80 МГц

SYSCLK 480 МГц; HCLK3 (FMC) 240 МГц; SDCLK = HCLK3/2 120 МГц; Период tCK 8.333 нс

Формула: регистр = (T_min_нс / tCK) ; tCK = (1 / 120 МГц) = 8.333 нс

Тайминги FMC_SDTRx — регистровые значения
-----------------------------------------
CAS Latency = 3(Pinout & Configuration/FMC/SDRAM1/SDRAM timing in memory clock cycles)

Почему расчёт делался для CL(CAS Latency)=3, а не равным 2?
## Что говорит даташит:
В разделе 9.5 для грейда **-6I** указано два варианта:

```
CLK Cycle Time при CL=2: tCK min = 7.5 нс → максимум 133 МГц
CLK Cycle Time при CL=3: tCK min = 6.0 нс → максимум 166 МГц
```
Заголовок в Order Information тоже прямо говорит:
```
W9825G6KH-6I: 166MHz/CL3 или 133MHz/CL2
```
---
## Почему при 120 МГц выбран CL=3
При SDCLK = 120 МГц период tCK = 8.333 нс. Проверим оба варианта:
```
CL=2: требует tCK >= 7.5 нс → 8.333 нс >= 7.5 нс ✓ формально допустимо
CL=3: требует tCK >= 6.0 нс → 8.333 нс >= 6.0 нс ✓ допустимо с запасом
```
Технически **оба варианта работают** при 120 МГц. Выбор CL=3 обоснован по нескольким причинам:
**Запас по частоте:**
```
CL=2: запас = 8.333 − 7.5 = 0.833 нс (всего ~10%)
CL=3: запас = 8.333 − 6.0  = 2.333 нс (~28%)
```
У CL=2 запас очень маленький. Реальный тактовый сигнал на плате — это не идеальный меандр.
Джиттер генератора, задержки на дорожках PCB, паразитные ёмкости — всё это съедает эти 0.833 нс.
При CL=3 запас в 2.333 нс значительно надёжнее.
**Время выборки данных (tAC):**
```
CL=2: tAC max = 6 нс
CL=3: tAC max = 5 нс
```
При CL=2 данные появляются на шине через 6 нс после фронта CLK. При tCK = 8.333 нс у FMC остаётся
только 8.333 − 6.0 = 2.333 нс на захват данных с учётом всех задержек линий. При CL=3 ситуация чуть
лучше — 8.333 − 5.0 = 3.333 нс.
**Параметр Read Pipe Delay в FMC:**
STM32H7 FMC имеет регистр RPIPE (задержка конвейера чтения, 0–2 такта). Он компенсирует суммарную
задержку данных от SDRAM до момента захвата. При CL=2 нужно точнее подбирать RPIPE, при CL=3 настройка
проще и стабильнее.
**Практика применения:**
Все официальные примеры ST для STM32H7 (включая отладочные платы STM32H743I-EVAL, NUCLEO-H743ZI)
используют CL=3 даже на частотах ниже 133 МГц.
---
## Когда имеет смысл CL=2
CL=2 оправдан только если вы принципиально хотите снизить латентность первого доступа. Разница
составляет один такт SDCLK — при 120 МГц это **8.333 нс**. Для большинства задач такая разница не ощутима,
а риски нестабильности при CL=2 на 120 МГц не стоят этого выигрыша.
Итак:
Тайминги FMC_SDTRx — регистровые значения
CAS Latency = 3(Pinout & Configuration/FMC/SDRAM1/SDRAM timing in memory clock cycles)
-------------------------------------------------------------------------------------------------------------
Параметр	               Мин. по даташиту	 Расчёт	                 Значение	 Реальное время	 Запас
-------------------------------------------------------------------------------------------------------------
TMRD Mode reg → Active	    2 CLK	           2 CLK (фикс.)	     2	         16.667 нс	     +0 нс
TXSR Выход из self-refresh 72 нс	         ⌈72 / 8.333⌉ = ⌈8.640⌉	 9	         74.997 нс	     +2.997 нс
// tRAS: время от Active до Precharge (минимальное 42 нс)
//       ВНИМАНИЕ: поле SelfRefreshTime в STM32H7 задаёт НЕ tRAS!
//       Согласно RM0433, SelfRefreshTime — это количество тактов, которое
//       контроллер ждёт перед выдачей команды Self Refresh (обычно 4-5 тактов).
//       Это поле не связано с tRAS. tRAS задаётся отдельно в Init.ACTtoPREdelay.
//       Для SelfRefreshTime достаточно значения 4 (типовое).
timing.SelfRefreshTime = 4;     // ✅ исправлено (было 6)
TRAS Активное время строки 42 нс	         ⌈42 / 8.333⌉ = ⌈5.04⌉	 4	         42 нс	         +2 нс			!!! 4-5 тактов !!! Задержка для входа в режим Self Refresh (время, которое контроллер ждёт перед выдачей команды Self Refresh). Это не tRAS!
TRC Цикл строки	           60 нс	         ⌈60 / 8.333⌉ = ⌈7.20⌉	 8	         66.667 нс	     +0.667 нс
TWR Восстановление записи   2 CLK	           3 CLK (фикс.)	     3	         25.000 нс	     +0 нс
TRP Предзаряд строки	   18 нс	         ⌈18 / 8.333⌉ = ⌈2.160⌉	 3	         16.666 нс       +1.666 нс
TRCD Строка → Столбец	   18 нс	         ⌈18 / 8.333⌉ = ⌈2.160⌉	 3	         15.666 нс	     +1.666 нс

TWR Восстановление записи  2 CLK
в CubeMX написано:
Write recovery time
Write recovery time must be between 3 and 16.
Parameter Description:
Specifies the delay between a Write and a Precharge command in number of memory clock cycles.
WriteRecoveryTime must satisfy the following constraints:
1: WriteRecoveryTime >= SelfRefreshTime - RowToColumnDelay,
2: WriteRecoveryTime >= RowCycleDelay - RowToColumnDelay - RowPrechargeDelay.
If two SDRAM devices are used, the FMC_SDTR1 and FMC_SDTR2 registers must be programmed with the same
Write Recovery Time corresponding to the slowest SDRAM device.

FMC контроллер STM32 накладывает дополнительные ограничения на TWR, независимо от того что написано в даташите SDRAM.
Почему минимум 3, а не 2
CubeMX проверяет два неравенства с вашими текущими значениями:
Ограничение 1:
TWR >= TRAS − TRCD
TWR >= 6 − 3 = 3
Ограничение 2:
TWR >= TRC − TRCD − TRP
TWR >= 8 − 3 − 3 = 2
Минимум 2. Это математическая зависимость внутри FMC — он должен гарантировать корректную
последовательность команд Write → Precharge → Activate с учётом всех других таймингов.
Что поставить
Ставим TWR = 3 — это минимально допустимое значение при наших таймингах.
Оно безопасно и соответствует требованиям как FMC, так и SDRAM (которая требует всего 2,
но 3 тоже нормально — больше минимума не вредит).

Авторегенерация (SDRTR):
------------------------
COUNT = ⌊(f_SDCLK × t_REF) / N_rows⌋ − 20 = ⌊(120e6 × 64e−3) / 8192⌋ − 20 = ⌊937.50⌋ − 20 = 937 − 20 = 917
Интервал на строку 7.81 мкс;   SDRTR COUNT 917;  HAL вызов 917UL

SYSCLK=480 МГц | HCLK3=240 МГц | SDCLK=120 МГц | tCK=8.333 нс

Итоговый код инициализации:
---------------------------
FMC_SDRAM_TimingTypeDef timing = {0};
timing.LoadToActiveDelay    = 2;   // TMRD: tMRD  = 2 CLK
timing.ExitSelfRefreshDelay = 9;   // TXSR: tXSR  = 72 нс → ⌈8.640⌉
// tRAS: время от Active до Precharge (минимальное 42 нс)
//       ВНИМАНИЕ: поле SelfRefreshTime в STM32H7 задаёт НЕ tRAS!
//       Согласно RM0433, SelfRefreshTime — это количество тактов, которое
//       контроллер ждёт перед выдачей команды Self Refresh (обычно 4-5 тактов).
//       Это поле не связано с tRAS. tRAS задаётся отдельно в Init.ACTtoPREdelay.
//       Для SelfRefreshTime достаточно значения 4 (типовое).
timing.SelfRefreshTime = 4;     // ✅ исправлено (было 6)
timing.SelfRefreshTime      = 4;   // TRAS: tRAS  = 42 нс → ⌈5.040⌉   !!! 4-5 тактов !!! Задержка для входа в режим Self Refresh (время, которое контроллер ждёт перед выдачей команды Self Refresh). Это не tRAS!
timing.RowCycleDelay        = 8;   // TRC:  tRC   = 60 нс → ⌈7.200⌉
timing.WriteRecoveryTime    = 3;   // TWR:  tWR   = 3 CLK
timing.RPDelay              = 3;   // TRP:  tRP   = 18 нс → ⌈2.160⌉
timing.RCDDelay             = 3;   // TRCD: tRCD  = 18 нс → ⌈2.160⌉

Период авторегенерации:
-----------------------
HAL_SDRAM_ProgramRefreshRate(&hsdram1, 917);

Откуда берётся каждая цифра:
---------------------------
Цепочка тактирования:
---------------------
SYSCLK 480 МГц → AHB3 prescaler /2 → HCLK3 = 240 МГц → FMC_SDCLK делитель /2 или /3

Делитель /2 даёт 120 МГц — это рекомендуемый вариант для W9825G6KH-6 (чип поддерживает до 166 МГц).
Делитель /3 = 80 МГц — консервативный режим, если есть проблемы со стабильностью или длинные дорожки на PCB.

Общая формула для тайминга в тактах:
------------------------------------
register_value = ( T_min_нс / tCK )
где tCK — период одного такта SDCLK. При 120 МГц это 8.333 нс.

Особые случаи (в тактах, не в наносекундах):
--------------------------------------------
TMRD и TWR в даташите W9825G6KH-6I задаются в тактах CLK, а не в нс.
Оба равны минимум 2 тактам — это не зависит от частоты.

Расчёт авторегенерации (SDRTR):
-------------------------------
W9825G6KH-6I требует освежать каждую из 8192 строк за 64 мс.
(Из даташита, раздел 9.5 AC Characteristics, строка Refresh Time (8K Refresh Cycles):
-40°C ≤ TA ≤ 85°C → tREF = 64 мс)
Это означает: все 8192 строки должны быть обновлены за 64 мс.
Это требование стандарта JEDEC для SDRAM — конденсаторы в ячейках памяти теряют заряд,
и если строку не обновить за это время, данные в ней пропадут.
Значит, интервал между командами Auto-Refresh — 7.8125 мкс.
Откуда берётся интервал на одну строку
64 мс / 8192 строк = 7.8125 мкс на строку
Контроллер FMC должен выдавать команду Auto-Refresh не реже чем раз в 7.8125 мкс.

Важное замечание для -6I
В даташите есть специальный случай для грейда -6J (не -6I):
-40°C ≤ TA ≤ 105°C → tREF = 16 мс
Для вашего -6I температурный диапазон -40°C ≤ TA ≤ 85°C, поэтому применяется 64 мс — всё верно.
Если бы вы использовали грейд -6J при температуре выше 85°C, нужно было бы пересчитывать с tREF = 16 мс:
16 мс / 8192 = 1.953 мкс на строку

SDRTR = ⌊1.953 мкс × 120 МГц⌋ − 20
      = ⌊234.4⌋ − 20 = 214
Это в 4 раза чаще — при повышенной температуре конденсаторы разряжаются быстрее.

Число тактов между командами:
-----------------------------
COUNT = ( f_SDCLK × 7.8125 мкс ) − 20
Вычитание 20 — это запас, рекомендованный ST: контроллер FMC должен успеть обработать команду до истечения счётчика.
При 120 МГц получается 917, при 80 МГц — 605.

Какой делитель выбрать:
-----------------------
Если трассировка PCB аккуратная (линии выровнены по длине, хорошая развязка),
берите /2 (120 МГц) — максимальная пропускная способность.
Если плата экспериментальная или дорожки длинные (>5 см) — используйте /3 (80 МГц),
это даст дополнительный запас по таймингам.

----------------------------------------
В STM32H743xITx_FLASH.ld добавьте:

Размещать нужно после секции .bss, перед финальной секцией ._user_heap_stack (или аналогичной).
Вот конкретное место в типовом скрипте CubeMX:

SECTIONS
{
  .isr_vector : { ... } >FLASH
  .text       : { ... } >FLASH
  .rodata     : { ... } >FLASH
  ...
  .data       : { ... } >RAM AT> FLASH
  .bss        : { ... } >RAM        // ← стандартная секция BSS

  // ====== ВСТАВИТЬ СЮДА ======
  .sdram_bss (NOLOAD) :
  {
    . = ALIGN(4);
    *(.sdram_section)
    . = ALIGN(4);
  } >SDRAM
  // ============================

  ._user_heap_stack :              // ← эта секция идёт после
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM
}

Важный момент: директива (NOLOAD) говорит линкеру, что эта секция не загружается из flash при старте —
данные в SDRAM не инициализируются автоматически. Именно поэтому в коде нужно либо явно обнулять буфер через memset,
либо инициализировать его вручную после SDRAM_Init().

-----------------------------------------

STM32H743xITx_RAM.ld — это альтернативный линкер-скрипт для запуска программы полностью из RAM, минуя flash.
Используется исключительно в отладочных целях.
Зачем это нужно:
Flash-память STM32H743 имеет ограниченный ресурс перезаписи — около 10 000 циклов.
Когда вы активно отлаживаете прошивку и перепрошиваете контроллер по 50–100 раз в день, flash быстро изнашивается.
При загрузке в RAM такого износа нет.
Кроме того, запись в RAM через отладчик происходит значительно быстрее, чем стирание и запись flash-страниц,
поэтому цикл «скомпилировал → залил → проверил» становится заметно короче.
Принципиальное отличие от FLASH.ld:
В FLASH.ld секция .text (код) живёт во flash, .data копируется из flash в RAM при старте через startup-код.
В RAM.ld всё — и .text, и .data — размещается сразу в RAM, startup-код не делает никакого копирования.
Ограничения:
После выключения питания программа, загруженная в RAM, исчезает — при следующем включении контроллер запустит то,
что записано во flash (или вообще ничего, если flash пустая). Поэтому RAM.ld используют только во время сессии отладки,
а финальную прошивку всегда собирают с FLASH.ld.
Для нашей задачи с SDRAM нужен только FLASH.ld — секцию .sdram_bss добавляете туда, RAM.ld не трогаете.

---------------------------------------

Как использовать SDRAM для выполнения кода:

Cortex-M7 умеет выполнять код из любой читаемой области памяти, включая внешнюю SDRAM. Но есть важные нюансы.
Что нужно изменить
В MPU поставить DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE и сделать регион кэшируемым (иначе код будет работать очень медленно):

MPU_InitStruct.TypeExtField    = MPU_TEX_LEVEL0;
MPU_InitStruct.IsCacheable     = MPU_ACCESS_CACHEABLE;
MPU_InitStruct.IsBufferable    = MPU_ACCESS_BUFFERABLE;
MPU_InitStruct.IsShareable     = MPU_ACCESS_NOT_SHAREABLE;
MPU_InitStruct.DisableExec     = MPU_INSTRUCTION_ACCESS_ENABLE; // XN=0

Без ICache код из SDRAM будет выполняться со скоростью FMC — каждая инструкция будет ждать несколько тактов доступа к внешней шине.
С включённым ICache повторные обращения к одним и тем же участкам кода идут из кэша на полной скорости.
Главная проблема — курица и яйцо: SDRAM нельзя использовать до её инициализации.
Значит, минимум стартовый код (векторы прерываний, Reset_Handler, SystemInit, MX_FMC_Init, SDRAM_Init)
должен находиться во внутренней Flash. Только после инициализации SDRAM можно перейти к коду в ней.
Типичные сценарии применения:
Загрузчик из внешней памяти — bootloader во Flash читает прошивку с SD-карты или по сети и копирует её в SDRAM, затем прыгает на неё.
Так делают когда приложение не влезает во Flash.

Динамический код — код компилируется или загружается в рантайме (скрипты, плагины). Редко нужно на микроконтроллере.

Отладка — быстрее загружать код в SDRAM через отладчик, чем стирать Flash.

Практическая реальность для STM32H743
У STM32H743 есть 2 МБ внутренней Flash — для подавляющего большинства задач этого хватает.
Выполнение кода из SDRAM оправдано только если приложение реально не помещается во Flash, или если нужна динамическая загрузка кода.

----------------------------
Кэширование работает отлично, пока данные принадлежат только процессору. Как только в игру вступает DMA (прямой доступ к памяти),
картина усложняется. Это первая причина проблем при, казалось бы, правильной конфигурации.

Проблема: DMA-контроллер обращается к SDRAM напрямую, минуя кэш. В то же время процессор может работать с устаревшей копией данных
в своем быстром кэше. В результате вы получаете ошибки и "битые" данные.

Решение: В таких случаях необходимо вручную управлять согласованностью кэша.
Перед запуском DMA на чтение из SDRAM нужно очистить кэш (SCB_CleanDCache_by_Addr),
а после завершения DMA — инвалидировать кэш (SCB_InvalidateDCache_by_Addr), чтобы процессор загрузил свежие данные.


Обязательное обслуживание кэша при DMA
Это главная сложность кэшируемой SDRAM. При работе с DMA (LTDC, DMA2D, DMA1/2) нужно явно синхронизировать кэш:
---------------------------------------------------------------------------------------------------------------
Перед тем как DMA ЧИТАЕТ из SDRAM (CPU предварительно записал туда данные)
SCB_CleanDCache_by_Addr((uint32_t*)buf, size);
Сбрасывает "грязные" строки кэша в SDRAM, иначе DMA прочитает устаревшие данные из памяти

После того как DMA ЗАПИСАЛ в SDRAM (CPU хочет прочитать свежие данные)
SCB_InvalidateDCache_by_Addr((uint32_t*)buf, size);
Инвалидирует кэш-строки, иначе CPU прочитает старые данные из кэша, а не то что записал DMA

Адрес и размер должны быть выровнены по границе кэш-строки (32 байта на Cortex-M7):
-----------------------------------------------------------------------------------
Выравнивание буфера обязательно
__attribute__((aligned(32)))
__attribute__((section(".sdram_section")))
uint8_t dma_buf[1024];

Перед запуском DMA-передачи:
SCB_CleanDCache_by_Addr(
    (uint32_t*)dma_buf,
    (sizeof(dma_buf) + 31) & ~31  // округлить до 32 байт
);
HAL_DMA_Start(...);

Когда использовать каждый вариант
Если SDRAM используется преимущественно CPU (вычисления, LVGL, heap) — кэшируемый вариант даёт реальный прирост.
Если SDRAM используется преимущественно DMA/LTDC — можно оставить некэшируемым и не писать лишний код синхронизации.
Для смешанного случая можно сделать два MPU региона:

Регион 0: основной кэшируемый (CPU данные)
Базовый адрес: 0xC0000000, размер: 31 МБ, TEX=0, C=1, B=1

Регион 1: некэшируемый для DMA/LTDC буферов
Базовый адрес: 0xC1F00000, размер: 1 МБ, TEX=1, C=0, B=0
Регион 1 перекрывает часть Региона 0 и имеет приоритет
*/
