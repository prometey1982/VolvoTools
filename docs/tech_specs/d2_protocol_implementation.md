# Техзадание на реализацию протокола D2

## Цель

Документировать формат и логику протокола D2, используемого для диагностики и прошивки Volvo P80/P1/P2-ECU через CAN-шину. Описать существующую реализацию в кодовой базе (C++20, Windows/CMake).

## Обзор

D2 — проприетарный протокол Volvo поверх CAN (29-бит, extended frames). Все сообщения передаются на CAN ID `D2Message::CanId = 0xFFFFE` с 8-байтными payload-ами.

**Архитектура:**

```
D2Message (сериализация логических данных в CanFrame)
    ↓
D2Request (отправка через ICanChannel, парсинг ответа)
    ↓
ICanChannel (J2534 / Mock)
```

**Bootloader-операции** используют отдельный примитивный протокол (8-байтный raw-фрейм, без префиксов/серий):

```
D2ProtocolCommonSteps → makeBootloaderFrame → ICanChannel
```

## Формат CAN-фрейма

```cpp
struct CanFrame {
    uint32_t id;
    std::vector<uint8_t> data;  // payload (0..8 байт)
    bool isExtendedId;
};
```

Все D2-фреймы: 8 байт payload, extended CAN ID `0xFFFFE`, isExtendedId = true.

| Байт | Назначение |
|------|-----------|
| `[0]` | Заголовок протокола (префикс / seriesId) |
| `[1..7]` | Полезные данные (до 7 байт, остаток — нули) |

## Заголовки (Byte 0)

### Префиксы

| Значение | Бит 0x40 | Назначение |
|----------|----------|------------|
| `0xC8 + len` | 1 | Одиночное сообщение (single-frame, ответ сразу) |
| `0x88 + len` | 0 | Первый фрейм мульти-фреймовой серии |
| `0x48 + len` | 1 | Последний фрейм серии |
| `0x09..0x0F`, `0x08` | 0 | Продолжение серии (seriesId) |

`len` = количество байт полезных данных в текущем фрейме (1–7).

### Серийные идентификаторы (seriesId)

Цикл: `0x09 → 0x0A → ... → 0x0F → 0x08 → 0x09 → ...`

Бит 0x40 = 0 для всех seriesId. Признак конца серии: `0x48 + len` (бит 0x40 = 1).

## Формат запроса (Request)

Сборка CAN-фреймов из трёх компонентов: `ecuId` (1 байт), `requestId` (N байт), `params` (M байт). Общий размер данных = `1 + N + M` байт.

### Single-frame (≤ 7 байт)

```
[0] = 0xC8 + dataSize
[1] = ecuId
[2..1+N] = requestId[0..N-1]
[2+N..1+N+M] = params[0..M-1]
[1+N+M..7] = 0x00 (padding)
```

### Multi-frame (> 7 байт)

**Фрейм 1 (первый):**
```
[0] = 0x88 + payloadSize
[1..payloadSize] = ecuId + первые байты requestId/params
```

**Фреймы 2..K-1 (продолжения):**
```
[0] = seriesId (0x09 → 0x0A → ...)
[1..payloadSize] = следующие байты данных
```

**Фрейм K (последний):**
```
[0] = 0x48 + payloadSize
[1..payloadSize] = последние байты данных
```

### Алгоритм сборки

Реализация: `generateCanFrames` в `Common/src/protocols/D2Message.cpp:26-50`

```
dataSize = requestId.size() + params.size() + 1  // +1 для ecuId
seriesCounter = 0

for i = 0; i < dataSize; i += 7:
    payloadSize = min(dataSize - i, 7)
    firstMessage = (i == 0)
    lastMessage  = (i + payloadSize >= dataSize)

    prefix = 8
           + (firstMessage ? 0x80 : 0)        // бит первого
           + (lastMessage  ? 0x40 : seriesCounter)  // бит последнего или seriesId
           + (firstMessage || lastMessage ? payloadSize : 0)  // размер только там, где нужен

    canPayload = uint8_t[8]{}
    canPayload[0] = prefix
    canPayload[1..payloadSize] = getData(ecuId, requestId, params, i + j)
    // getData: offset 0 → ecuId, offset 1.. → requestId, затем params

    result.push_back(CanFrame{CanId, canPayload, true})
    
    seriesCounter = (seriesCounter + 1) % 8  // инкремент для следующего middle-фрейма
```

**Четыре случая префикса:**

| first | last | prefix | Формула |
|-------|------|--------|---------|
| 1 | 1 | `0xC8 + payloadSize` | single-frame (≤7 байт) |
| 1 | 0 | `0x88 + payloadSize` | первый фрейм multi-frame |
| 0 | 1 | `0x48 + payloadSize` | последний фрейм |
| 0 | 0 | `0x09` → `0x0A` → … → `0x0F` → `0x08` → … | middle (seriesId) |

## Формат ответа (Response)

Парсинг ответа ЭБУ: `D2Request::process` в `Common/src/protocols/D2Request.cpp:34-97`

### Первый фрейм

```
[0] = header
[1] = ecuId                    (должен совпадать с запрошенным)
[2] = requestId[0] + 0x40      (должен совпадать)
[3..2+restRequestSize] = requestId[1..] (должен совпадать)
[3+restRequestSize..] = полезные данные ответа
```

где `restRequestSize = requestId.size() - 1`.

`inSeries = !(header[0] & 0x40)` — если бит 0x40 = 0, ожидается серия.

### Продолжения (серийные фреймы)

```
[0] = заголовок
    бит 0x40 = 0 → продолжение серии (collect data from [1..7])
    бит 0x40 = 1, header >= 0x48 → последний фрейм
    бит 0x40 = 1, header < 0x48 → ошибка "Wrong data length in series"
[1..7] = данные
```

### Сборка результата

- Первый фрейм: dataOffset = `3 + restRequestSize` (пропуск заголовка, ecuId, requestId[0]+0x40, rest requestId)
- Продолжения: dataOffset = 1 (пропуск заголовка)
- Копирование: `result += frame[dataOffset..]`

## Формат ошибки

`checkD2Error` в `Common/src/Util.cpp:741-747`

```
[4] = 0x7F  (маркер отрицательного ответа)
[5] = ecuId
[6] = код ошибки → выбрасывается D2Error
```

Формат ошибки **фиксированный** (позиции 4,5,6 не зависят от длины requestId). Ошибка проверяется на первом фрейме ответа до разбора данных.

## Raw-формат (однофреймовые команды)

Используется в `D2ProtocolCommonSteps` и `D2RawMessages` для bootloader-операций.

```
[0] = ecuId
[1] = command (0xA8+len, 0xC0, 0xC8, 0xF8, 0xA0, 0x9C, 0xB4, 0xBC)
[2..7] = параметры / данные
```

**Сборка:** `makeBootloaderFrame(ecuId, {command, ...})` в `D2ProtocolCommonSteps` или `D2RawMessages::create*` в `D2Messages.cpp`.

**Ответ на raw-фрейм** (приём в `D2ProtocolCommonSteps`):
- `response[1..]` сравнивается с ожидаемыми байтами (echo-подтверждение)
- Номер команды в ответе может отличаться (например, запрос `0xC0` → ответ `0xC6`)

## Команды D2

### Raw-команды (однофреймовые, через `D2RawMessages` или `makeBootloaderFrame`)

| Команда | Функция | Назначение |
|---------|---------|------------|
| `0xC0` | `createStartPrimaryBootloaderMsg` | Запуск Primary Bootloader (ответ: `0xC6`) |
| `0xC8` | `createWakeUpECUMsg` | Пробуждение ЭБУ |
| `0xA8+len` | `createWriteDataFrames` | Запись данных (len = 1..6, данные в [2..]) |
| `0xA8` | `createSBLTransferCompleteMsg` | Завершение передачи SBL |
| `0xF8` | `createEraseMsg` | Стирание флеша (ответ: `0xF9` + статус) |
| `0xA0` | `createJumpToMsg` | Переход на адрес / запуск (ответ: `0xA0`) |
| `0x9C` | `createSetMemoryAddrMsg` | Установка адреса памяти (4 байта addr) |
| `0xB4` | `createCalculateChecksumMsg` | Расчёт контрольной суммы |
| `0xBC` | `createReadOffsetMsg2` | Чтение по смещению |
| `0x86` | `goToSleepCanRequest` | Усыпление шины (ID = 0xFF + периодическая отправка) |

### Структурированные запросы (через `D2Message(ecuId, requestId, params)`)

| requestId | params | Функция | Назначение |
|-----------|--------|---------|------------|
| `{0xB9, 0xFB}` | `{}` | `requestVIN` | Запрос VIN (CEM) |
| `{0xB9, 0xFC}` | `{}` | `requestVehicleConfiguration` | Конфигурация автомобиля (CEM) |
| `{0xA6, 0xF0, 0x00, 0x01}` | `{}` | `requestMemory` | Запрос памяти (ECM_ME) |
| `{0xAA, 0x00}` | `{}` | `unregisterAllMemoryRequest` | Отмена регистрации памяти (ECM_ME) |
| `{0xA7}` | `{addr[2], addr[1], addr[0], 1, size}` | `readDataByOffset` | Чтение по смещению |
| `{0xB4, 0x21, 0x34}` | `{addr[3..0], size}` | `readDataByAddr` | Чтение по адресу |
| `{0xAA, 0x50}` | `{addr[2], addr[1], addr[0], size}` | `registerAddrRange` | Регистрация адресов для чтения |
| `{0xBA}` | `{addr[2], addr[1], addr[0], data}` | `writeDataByAddr` | Запись байта по адресу |
| `{0xB0, 0x07, 0x01, 0xFF}` | `{timeHi, timeLo}` | `setCurrentTime` | Установка времени DIM |

## Ответ ЭБУ: формат эха

При получении запроса ЭБУ подтверждает его в первом фрейме ответа:

```
response[1] = ecuId          (эхо без изменений)
response[2] = requestId[0] + 0x40  (эхо с битом 6)
response[3..] = requestId[1..]     (эхо остатка)
```

После эха следуют полезные данные ответа.

## Последовательности протокола

### Фаза просыпания

```
[Инструмент] → [CAN: 0xFF, 0xC8, 0,0,0,0,0,0]  (wakeUp, ID=0xFF)
→ пауза
[Инструмент] → [CAN: 0xFF, 0x86, 0,0,0,0,0,0]  (fallAsleep, периодически 5мс, 3 сек)
→ пауза
```

### Загрузка bootloader

```
[Инструмент] → D2Raw(ecuId, {0xC0})              (startPBL)
[ECU] → D2Raw(ecuId, {0xC6})                      (подтверждение)

// Если требуется SBL:
[Инструмент] → DataFrames(SBL-образ)               (loadSBL через transferData)
[Инструмент] → D2Raw(ecuId, {0xA0, addr[0..4]})   (startRoutine, переход на SBL)
```

### Прошивка

```
[Инструмент] → setMemoryAddr (0x9C + addr)        (установка адреса)
[ECU] → 0x9C (эхо)

// Цикл батчами по 10 фреймов:
[Инструмент] → data batch (10 × 0xA8+len с данными)
[Инструмент] → clearRx()

// Финализация чанка:
[Инструмент] → setMemoryAddr (0x9C + endAddr)
[Инструмент] → calculateChecksum (0xB4 + endAddr)
[ECU] → 0xB1 + checksum
```

## Реализация

### Классы

| Класс | Наследует | Файл .hpp | Назначение |
|-------|-----------|-----------|------------|
| `CanFrame` | (struct) | `Common/common/CanFrame.hpp` | CAN-фрейм: id, data, isExtendedId |
| `CanMessage` | (abstract) | `Common/common/CanMessage.hpp` | Базовый класс: хранит `_data`, объявляет `getFrames()` |
| `D2Message` | `CanMessage` | `Common/common/protocols/D2Message.hpp` | Сериализация (ecuId + requestId + params) → CanFrame |
| `D2Request` | — | `Common/common/protocols/D2Request.hpp` | Отправка D2Message + парсинг ответа |
| `D2Messages` | — | `Common/common/protocols/D2Messages.hpp` | Фабрика D2-запросов (возвращает `D2Message`) |
| `D2RawMessages` | — | `Common/common/protocols/D2Messages.hpp` | Фабрика bootloader-команд (возвращает `CanFrame`) |
| `D2ProtocolCommonSteps` | — | `Common/common/protocols/D2ProtocolCommonSteps.hpp` | Высокоуровневые последовательности протокола |
| `D2Error` | `std::runtime_error` | `Common/common/protocols/D2Error.hpp` | Исключение D2-ошибки |

### Зависимости

```
D2Message → CanMessage
D2Request → D2Message, ICanChannel
D2ProtocolCommonSteps → D2Message, D2Messages, ICanChannel, VBF
D2Messages → D2Message
D2RawMessages → (raw CanFrame, без D2Message)
```

### Ответственность

- **D2Message**: сериализует логические данные (ecuId + requestId + params) в `std::vector<CanFrame>` через `getFrames()`. Генерирует фреймы с правильными префиксами и seriesId с разбивкой на single/multi-frame.
- **D2Request**: отправляет `D2Message::getFrames()` через `ICanChannel::send()`, принимает ответ(ы), валидирует эхо (`ecuId`, `requestId[0] + 0x40`), детектирует ошибки через `checkD2Error`, собирает multi-frame ответ.
- **D2ProtocolCommonSteps**: оркестрирует bootloader-последовательности. Использует `makeBootloaderFrame` для raw-команд и `D2Messages::setCurrentTime` для D2-команд.
- **D2RawMessages**: фабрика bootloader-команд (возвращает готовый `CanFrame` с `[0]=ecuId, [1]=command`).

## Известные проблемы

### seriesId инкремент (исправлено)

**Было:** инкремент `seriesId` в начале каждой итерации — `0x09` вычислялся, но не использовался на первом фрейме.

**Последовательность (было):** `[0x8F] [0x0A] [0x0B] [0x0C] [0x49]` — первый middle получал `0x0A`.

**Исправлено:** `seriesCounter` инкрементируется после каждого фрейма, но используется только в middle-фреймах (через тернарный оператор `lastMessage ? 0x40 : seriesCounter`). Последовательность верная: `[0x8F] [0x09] [0x0A] [0x0B] [0x49]`.

### `createPayload` — обратный порядок `params` и `requestId` (исправлено)

**Файл:** `Common/src/protocols/D2Message.cpp:10-16`

Было: `result.insert(result.begin(), ...)` — params вставлялись перед requestId.

Исправлено: `result.insert(result.end(), ...)` — params добавляются после requestId. Порядок данных:

```
[ecuId, requestId[0], requestId[1], ..., params[0], params[1], ...]
```

### Обработка ошибок в raw-фреймах (не будет исправляться)

`D2ProtocolCommonSteps` использует `writeMessagesAndCheckAnswer` для raw-команд. Функция проверяет ответ побайтово (`response[1..] == toCheck`) и не вызывает `checkD2Error`.

**Причина:** bootloader-протокол имеет собственный формат ответа (echo-подтверждение команды), отличный от D2-формата ошибки (0x7F на позиции 4). `checkD2Error` предназначен только для D2-протокола (диагностические запросы через `D2Request::process`).

### Отправка D2-запросов без D2Message/D2Request (исправлено)

Ранее ряд D2-команд (диагностические, не bootloader) отправлялись как сырые CAN-фреймы, минуя `D2Message` и `D2Request`:
- **Нет формирования префикса** `[0]` — вместо `0xC8/0x88 + len` в первый байт клался случайный байт
- **Нет разбивки на multi-frame** — данные > 7 байт обрезались
- **Нет обработки ответа** через `checkD2Error` и проверки эха

**Исправлено:** `setDIMTime` — через `D2Messages::setCurrentTime`, D2ReaderChecksum — через `D2Message::makeD2RawMessage`, D2ReaderME7 — через `D2Messages::createReadOffsetMsg2`. Logger и D2Request — исправлен баг `{begin(), end()}`.

## Различия send vs receive

| Аспект | Запрос | Ответ |
|--------|--------|-------|
| `[0]` | префикс/seriesId | заголовок |
| `[1]` | `ecuId` | `ecuId` (эхо) |
| `[2]` | `requestId[0]` | `requestId[0] + 0x40` (эхо + бит) |
| `[3..]` | `requestId[1..]` + params | `requestId[1..]` (эхо), затем данные |

Разные форматы — особенность протокола D2, а не баг.

## Разграничение: D2-протокол vs bootloader-протокол

В кодовой базе присутствуют два разных протокола, использующих один CAN ID `0xFFFFE`:

| Характеристика | D2-протокол (диагностика) | Bootloader-протокол (прошивка) |
|---------------|--------------------------|-------------------------------|
| Формат | Многофреймовый, префиксы/seriesId, эхо requestId | Однофреймовый: `[0]=ecuId, [1]=command, [2..7]=params` |
| Multi-frame | Да (через seriesId) | Нет |
| Эхо requestId | Да (`requestId[0] + 0x40`) | Нет (проверка: `response[1..] == toCheck`) |
| Ошибки | `checkD2Error` (0x7F на позиции 4) | Нет отдельного обработчика |
| Где используется | `D2Request`, `D2Messages`, D2Reader*, Logger | `D2ProtocolCommonSteps` (startPBL, eraseFlash, writeData, и т.д.) |
| Класс для сборки | `D2Message` (структурированный / `getFrames()`) | `makeBootloaderFrame` (helper в .cpp) или `D2RawMessages` |

**Важно:** bootloader-команды НЕ должны использовать `D2Message` или `D2Request` — у них другой формат и логика.

## Выполненные изменения: переход на D2Message / D2Request

### Цель

Максимально использовать `D2Message` для сборки CAN-фреймов и `D2Request` для отправки/приёма в **D2-протоколе** (диагностические запросы). Bootloader-команды остаются как есть (raw-фреймы).

| Изменение | Файл(ы) | Статус |
|-----------|---------|--------|
| Централизация CAN ID | `D2Message.hpp` — добавлен `CanId = 0xFFFFE`; все `0xFFFFE` → `D2Message::CanId` | ✓ |
| Переименование `makeD2RawFrame` | `D2ProtocolCommonSteps.cpp`: `makeD2RawFrame` → `makeBootloaderFrame` | ✓ |
| `setDIMTime` → `D2Messages::setCurrentTime` | `D2ProtocolCommonSteps.cpp`: ручной payload заменён на D2Message | ✓ |
| D2ReaderChecksum → `D2Message::makeD2RawMessage` | `D2ReaderChecksum.cpp`: ручной payload заменён на фабричный метод | ✓ |
| D2ReaderME7 → `D2Messages::createReadOffsetMsg2` | `D2ReaderME7.cpp`: ручной `{0x7A, 0xBC}` заменён на фабрику | ✓ |
| Исправление `{begin(), end()}` бага | `D2Request.cpp`, `Logger.cpp`, `D2ProtocolCommonSteps.cpp` | ✓ |
| `D2Message::getFrames()` возвращает `vector<CanFrame>` | `D2Message.cpp`: `generateCanFrames` напрямую строит CanFrame | ✓ |
| `CanMessage` — pure virtual `getFrames()` | `CanMessage.hpp`: абстрактный базовый класс с `getFrames()` | ✓ |
| `D2RawMessages` — фабрика bootloader-команд | `D2Messages.cpp`: создание команд `createSetMemoryAddrMsg` и т.д. | ✓ |
| `generateCanFrames` — упрощение | `D2Message.cpp`: ~~inSeries, messagePrefix, memset~~ → единая формула `prefix` | ✓ |
| seriesId инкремент — исправлен баг | `D2Message.cpp`: сдвиг 0x09→0x0A устранён, последовательность 0x09→0x0A→… | ✓ |

## ТЗ на реализацию тестов D2Message и D2Request

### Цель

Создать тесты для `D2Message` (сериализация запросов) и `D2Request` (отправка/приём ответов) с использованием существующего `MockICanChannel` и Boost.Test.

### Файлы

| Файл | Описание |
|------|----------|
| `Common/test/D2MessageTest.cpp` | Тесты `D2Message` |
| `Common/test/D2RequestTest.cpp` | Тесты `D2Request` |
| `Common/test/CMakeLists.txt` | Новый, или добавить в существующий `CMakeLists.txt` |

### Инфраструктура

- **Фреймворк:** Boost.Test (уже используется в `Flasher/test/D2FlasherTest.cpp`)
- **Mock:** `MockICanChannel` (уже есть в `Flasher/test/MockICanChannel.hpp`) — вынести в `Common/test/` или использовать `target_include_directories`
- **Проверки:** `BOOST_CHECK_EQUAL`, `BOOST_CHECK`, `BOOST_CHECK_THROW`, `BOOST_REQUIRE`

### Структура тестов D2Message

#### 1. Конструкторы

| Тест | Описание |
|------|----------|
| `ConstructorRawData` | `D2Message(DataType{0x50, 0xB9, 0xFB})` — raw payload |
| `ConstructorEcuIdRequestId` | `D2Message(0x50, {0xB9, 0xFB})` — structured, без params |
| `ConstructorEcuIdRequestIdParams` | `D2Message(0x50, {0xB7}, {0x01, 0x02})` — structured, с params |
| `ConstructorMove` | `D2Message(D2Message(std::move(...)))` — move-конструктор |

#### 2. Getters

| Тест | Описание |
|------|----------|
| `GetEcuId` | `getEcuId()` == переданному ecuId |
| `GetRequestId` | `getRequestId()` == переданному requestId |
| `GetEcuIdRaw` | Для raw-конструктора `getEcuId()` == 0 (пусто) |
| `GetRequestIdRaw` | Для raw-конструктора `getRequestId()` == пусто |

#### 3. Сериализация single-frame (dataSize ≤ 7)

| Тест | Описание |
|------|----------|
| `SingleFrame1Byte` | 1 байт данных → 1 CAN-фрейм, prefix 0xC9 |
| `SingleFrame7Bytes` | 7 байт → 1 CAN-фрейм, prefix 0xCF |
| `SingleFramePrefix` | `[0]` == `0xC8 + dataSize` |
| `SingleFrameEcuId` | `[1]` == ecuId |
| `SingleFrameRequestId` | `[2..]` == requestId |
| `SingleFrameParams` | `[2+requestId.size..]` == params |
| `SingleFramePadding` | Все байты после payload == 0x00 |

#### 4. Сериализация multi-frame (dataSize > 7)

| Тест | Описание |
|------|----------|
| `MultiFrame2Frames` | dataSize=8 → 2 CAN-фрейма |
| `MultiFrame3Frames` | dataSize=15 → 3 CAN-фрейма |
| `MultiFrame8Frames` | dataSize=57 → 8+1=9 CAN-фреймов |
| `MultiFrameExact7n` | dataSize=14 (ровно 2×7) → 2 CAN-фрейма |

#### 5. Префиксы multi-frame

| Тест | Описание |
|------|----------|
| `FirstFramePrefixMulti` | Превый фрейм: `[0]` == `0x88 + 7` |
| `MiddleFrameSeriesId` | Средние фреймы: `[0]` == seriesId (0x09, 0x0A, …) |
| `MiddleFrameNoPayloadSize` | Средние фреймы: `[0]` не содержит payloadSize |
| `LastFramePrefix` | Последний фрейм: `[0]` == `0x48 + payloadSize` |

#### 6. SeriesId последовательность

| Тест | Описание |
|------|----------|
| `SeriesIdSequence9toF` | 8 middle-фреймов: 0x09, 0x0A, …, 0x0F |
| `SeriesIdWrapAround` | 9 middle-фреймов: … 0x0F, 0x08, 0x09 |
| `SeriesIdNoGap` | Первый middle === 0x09 (проверка исправления бага) |

#### 7. Граничные случаи

| Тест | Описание |
|------|----------|
| `DataSizeExact7` | 7 байт → single-frame (граница single/multi) |
| `DataSize8` | 8 байт → 2 фрейма (минимальный multi) |
| `ParamsEmpty` | `params = {}` — корректрный single/multi |
| `RequestIdOnly` | `params = {}` — фреймы только с requestId |

### Структура тестов D2Request

#### 1. Успешный приём single-frame

| Тест | Описание |
|------|----------|
| `SingleFrameResponse` | Отправка запроса → ответ 1 CAN-фреймом → ответ разобран корректно |
| `SingleFrameResponseData` | Данные ответа соответствуют ожидаемым |
| `SingleFrameEchoCheck` | Проверка эха `ecuId` и `requestId[0] + 0x40` |

#### 2. Успешный приём multi-frame

| Тест | Описание |
|------|----------|
| `MultiFrameResponse2Frames` | Ответ из 2 фреймов → сборка корректна |
| `MultiFrameResponse3Frames` | Ответ из 3 фреймов |
| `MultiFrameResponseMany` | Ответ из 9+ фреймов |
| `MultiFrameSeriesId` | Серийные фреймы с корректными seriesId |

#### 3. Эхо-проверка

| Тест | Описание |
|------|----------|
| `EchoWrongEcuId` | `response[1] != ecuId` → continue (пропуск) |
| `EchoWrongRequestId` | `response[2] != requestId[0] + 0x40` → continue |
| `EchoRestMismatch` | `response[3..] != requestId[1..]` → continue |

#### 4. Обработка ошибок D2

| Тест | Описание |
|------|----------|
| `ErrorResponse` | `[4]=0x7F, [5]=ecuId, [6]=errorCode` → `D2Error` |
| `ErrorWrongEcuId` | `[5] != ecuId` → не ошибка, continue |
| `ErrorNo7F` | `[4] != 0x7F` → не ошибка |

#### 5. Таймауты и пустые ответы

| Тест | Описание |
|------|----------|
| `ReceiveTimeout` | `receive()` → false → `runtime_error` |
| `EmptyFrameSkip` | `response.data.empty()` → skip, continue |
| `ShortFrameSkip` | `response.data.size() < dataOffset + restRequestSize + 1` → skip |

#### 6. Параметры таймаута

| Тест | Описание |
|------|----------|
| `CustomTimeout` | `process(channel, 5000)` — таймаут 5 сек |
| `SendMessagesDelay` | `process(channel, 1000, 50)` — задержка 50мс между отправкой фреймов |

### Тестовые сценарии (интеграционные)

| Тест | Описание |
|------|----------|
| `D2MessageD2RequestRoundtrip` | Создать D2Message → отправить через D2Request с mock-ответом → проверить разобранные данные |
| `RequestIdEchoConsistency` | Длинный requestId (> 7 байт) — multi-frame запрос → ответ с полным эхом |

### CMakeLists.txt (реализован)

```cmake
cmake_minimum_required(VERSION 3.16)
project(CommonTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS unit_test_framework)
find_package(Easyloggingpp REQUIRED)

add_executable(CommonTests
    D2MessageTest.cpp
    D2RequestTest.cpp
)
target_link_libraries(CommonTests Common Boost::unit_test_framework easyloggingpp::easyloggingpp)
target_include_directories(CommonTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Flasher/test  # для MockICanChannel
    ${CMAKE_SOURCE_DIR}/Common
)

if (WIN32)
    target_compile_definitions(CommonTests PRIVATE
       WIN32_LEAN_AND_MEAN
       NOMINMAX
    )
endif()

add_test(NAME D2Common COMMAND CommonTests --log_level=all)
```

### Статус реализации тестов

| Тест | Статус |
|------|--------|
| D2MessageTest — 18 тестов (конструкторы, single-frame, multi-frame, seriesId, граничные случаи) | ✓ Реализован (`Common/test/D2MessageTest.cpp`) |
| D2RequestTest — тесты (single/multi-frame ответы, эхо-проверки, ошибки, таймауты) | ✓ Реализован (`Common/test/D2RequestTest.cpp`) |
| MockICanChannel | ✓ Реализован (`Flasher/test/MockICanChannel.hpp`) |
| Сборка: cmake --build build --config Release | ✓ 0 ошибок |
