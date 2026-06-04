/*
 * sdram.h
 *
 *  Created on: 12 мая 2026 г.
 *      Author: KUTUZOV
 */

#ifndef INC_SDRAM_H_
#define INC_SDRAM_H_

#include "stm32h7xx_hal.h"

/* Атрибут для размещения в SDRAM */
#define SDRAM_ATTR __attribute__((section(".sdram_section")))

#define SDRAM_BASE_ADDR     0xC0000000UL
#define SDRAM_SIZE          (32 * 1024 * 1024)  /* 32 МБайт */
#define SDRAM_TIMEOUT       0xFFFFUL

#define TEST_WORDS (1024*1024)

/* Регистр режима SDRAM (Mode Register) */
#define SDRAM_MODEREG_BURST_LENGTH_1          0x0000U
#define SDRAM_MODEREG_BURST_LENGTH_2          0x0001U
#define SDRAM_MODEREG_BURST_LENGTH_4          0x0002U
#define SDRAM_MODEREG_BURST_LENGTH_8          0x0004U
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   0x0000U
#define SDRAM_MODEREG_CAS_LATENCY_2           0x0020U
#define SDRAM_MODEREG_CAS_LATENCY_3           0x0030U
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD 0x0000U
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  0x0200U

HAL_StatusTypeDef SDRAM_Init(SDRAM_HandleTypeDef *hsdram);
HAL_StatusTypeDef SDRAM_Test(void);

void SDRAM_Performance(void);

#endif /* INC_SDRAM_H_ */

/*
## Анализ настроек MPU для SDRAM: Cortex_M7/Parameter Settings

STM32H743IITx (LQFP176). Настройки MPU корректные. Разберём каждый пункт подробно.

---

## Speculation default mode — Enabled

Управляет спекулятивной предвыборкой инструкций и данных в Cortex-M7. Процессор заглядывает вперёд и загружает данные до того как они реально понадобятся.

Для SDRAM с правильно настроенным MPU это безопасно — MPU сообщает процессору тип памяти (Normal Non-cacheable), и спекулятивный доступ к этому региону не вызывает проблем. Оставьте Enabled.

---

## CPU ICache / CPU DCache — Enabled

Включает кэши инструкций и данных ядра Cortex-M7. CubeMX генерирует вызовы `SCB_EnableICache()` и `SCB_EnableDCache()` в начале `main()`. Правильно, не меняйте.

---

## MPU Control Mode

```
Background Region Privileged accesses only +
MPU Disabled during hard fault, NMI and FAULTMASK
```

Это два бита регистра MPU_CTRL:

**PRIVDEFENA = 1** — доступ к адресам не покрытым ни одним регионом MPU разрешён только привилегированному коду. Если ваш код работает в привилегированном режиме (по умолчанию в STM32), то обращения к Flash, внутренней RAM, периферии работают без явного описания их в MPU регионах.

**HFNMIENA = 0** — MPU отключается во время HardFault, NMI и FAULTMASK обработчиков. Это важно — если MPU заблокирует что-то внутри обработчика ошибки, вы не сможете диагностировать проблему. Оставьте как есть.

---

## Region 0 — детальный разбор

### MPU Region Base Address = 0xC0000000 ✅

Правильный базовый адрес SDRAM Bank 1 на STM32H743. Именно здесь начинается адресное пространство FMC SDRAM Bank 1.

### MPU Region Size = 32MB ✅

W9825G6KH-6I имеет ёмкость 256 Мбит = 32 МБайт. Размер совпадает точно.

### MPU SubRegion Disable = 0x0 ✅

MPU делит каждый регион на 8 равных частей (sub-regions). Каждый бит этого поля отключает одну часть. `0x0` означает все 8 частей активны — весь регион 32 МБ работает. Правильно.

### MPU TEX field level = level 1 ✅

TEX=1 (001 в бинарном) совместно с C=0 и B=0 даёт тип памяти **Normal, Non-cacheable**. Это правильный выбор для SDRAM когда используется DMA, LTDC или другая периферия с прямым доступом к памяти — не нужно вручную управлять когерентностью кэша.

### MPU Access Permission = ALL ACCESS PERMITTED ✅

Разрешает чтение и запись как привилегированному, так и непривилегированному коду. Для SDRAM как области данных это правильно.

### MPU Instruction Access = DISABLE ✅

Устанавливает бит XN (eXecute Never). Процессор не сможет выполнять код из SDRAM. Правильная настройка — SDRAM используется как данные, запрет выполнения кода является защитой от случайного перехода в эту область.

### MPU Shareability Permission = DISABLE ✅

Помечает регион как несовместно используемый. На STM32H743 (одноядерный процессор) нет второго ядра которое могло бы обращаться к той же памяти. Важный нюанс: если бы здесь стояло ENABLE при включённом кэше — D-Cache автоматически отключился бы для этого региона. У вас кэш итак выключен (DISABLE), поэтому Shareability здесь роли не играет, но DISABLE — правильный выбор.

### MPU Cacheable Permission = DISABLE ✅

Кэширование выключено. Согласовано с TEX=1, C=0 — Normal Non-cacheable. Данные читаются напрямую из SDRAM через FMC при каждом обращении. Безопасно для DMA.

### MPU Bufferable Permission = DISABLE ✅

Буферизация записи выключена. Запись идёт напрямую в SDRAM без промежуточного буфера. Согласовано с остальными параметрами.

---

## Регионы 1–15 — Disabled

Все остальные регионы выключены. Это нормально — у вас только один внешний регион (SDRAM). Остальная память (Flash, внутренняя RAM, периферия) покрывается фоновым регионом MPU через PRIVDEFENA=1.

---

## Нужно ли добавлять User Constants?

**Нет, не нужно.** Вот разница между двумя подходами:

**User Constants в CubeMX** — это константы которые CubeMX использует при **генерации своего кода**. Они попадают в сгенерированные файлы как `#define`. Нужны когда вы хотите чтобы именно CubeMX-сгенерированный код использовал символическое имя вместо числа.

**`#define` в вашем `sdram.h`** — это константа вашего прикладного кода. Она доступна везде где вы подключаете этот заголовочный файл.

Поскольку `SDRAM_BASE_ADDR` используется только в вашем коде (`sdram.c`, `sdram.h`), а не в CubeMX-генерируемых файлах — `#define` в `sdram.h` полностью достаточно. Дублировать это в User Constants нет смысла, это только создаст путаницу с двумя определениями одного и того же значения под разными именами.

*/
