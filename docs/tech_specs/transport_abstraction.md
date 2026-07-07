# ТЗ: Абстракция транспортного уровня (J2534 → ICanChannel)

## 1. Цель

Убрать жёсткую зависимость `*ProtocolCommonSteps`, флешеров и логгеров от `j2534::J2534Channel`, заменив её абстрактным CAN-каналом. Это позволит подключать альтернативные транспорты (ESP32, STM32, ELM327) без изменения логики протоколов.

## 2. Проблема

Текущий код имеет сквозную привязку к J2534:

```
J2534 DLL (PassThru)
    ↓
j2534::J2534 / J2534Channel   ←--- весь код завязан
    ↓
BaseMessage (j2534)            ←--- toPassThruMsgs() — J2534-специфичный метод
    ↓
  CanMessage ← D2Message      ←--- наследуют BaseMessage
  UDSMessage                  ←--- наследуют BaseMessage
    ↓
UDSProtocolCommonSteps        ←--- принимают J2534Channel& + uint32_t canId
D2ProtocolCommonSteps          ←--- принимают J2534Channel& + uint8_t ecuId
KWPProtocolCommonSteps
UDSRequest / D2Request        ←--- process(J2534Channel&)
TP20Session / TP20RequestProcessor
    ↓
FlasherBase / UDSFlasher / KWPFlasher / D2Flasher
    ↓
VolvoFlasher / VolvoLogger
```

**Точки привязки к J2534:**
- 11 методов `UDSProtocolCommonSteps` c `const J2534Channel&` + `uint32_t canId`
- 8 методов `D2ProtocolCommonSteps` c `const J2534Channel&` + `uint8_t ecuId`
- `UDSRequest::process(const J2534Channel&)` — send/recv UDS
- `D2Request` / `RequestProcessorBase` иерархия
- `TP20Session` / `TP20RequestProcessor`
- `FlasherBase` — владеет `J2534ChannelProvider`, возвращающим `unique_ptr<J2534Channel>`
- Наследование `BaseMessage ← CanMessage ← D2Message` и `BaseMessage ← UDSMessage` — только ради `toPassThruMsgs()`
- `PASSTHRU_MSG` — структура J2534 — используется во всех шагах

## 3. Целевая архитектура

```
J2534 DLL      ELM327 (AT)    ESP32 (WiFi/UART)   STM32 (USB)     ← транспорт
    ↓               ↓               ↓                  ↓
J2534Channel  Elm327Adapter   Esp32CanAdapter    Stm32CanAdapter  ← адаптеры
    ↓               ↓               ↓                  ↓
    └───────────────┴───────────────┴──────────────────┘
                              ↓
                     ICanChannel                        ← единый интерфейс
                              ↓
                    *ProtocolCommonSteps
                    UDSRequest / D2Request
                    TP20Session / RequestProcessorBase
                              ↓
                    FlasherBase / *Flasher
                    VolvoFlasher / VolvoLogger
```

Ключевое изменение: `*ProtocolCommonSteps` и `*Flasher` теряют зависимость от `j2534::J2534Channel`. Вся J2534-специфика инкапсулирована в `J2534ChannelAdapter`.

## 4. Новые типы

### 4.1. `CanFrame` — единое CAN-сообщение

```cpp
// Common/common/CanFrame.hpp
#pragma once

#include <cstdint>
#include <vector>

struct CanFrame {
    uint32_t id = 0;               // CAN arbitration ID (11 или 29 бит)
    std::vector<uint8_t> data;     // payload
    bool isExtendedId = false;     // true = 29-bit, false = 11-bit
};
```

Заменяет `PASSTHRU_MSG` как единицу обмена. Никаких структур J2534 в публичных заголовках компонентов `Common`, `Flasher`, `Logger`.

### 4.2. `ICanChannel` — единый интерфейс канала

```cpp
// Common/common/ICanChannel.hpp
#pragma once

#include "CanFrame.hpp"

#include <vector>

class ICanChannel {
public:
    virtual ~ICanChannel() = default;

    // === Отправка ===
    virtual bool send(const CanFrame& frame) = 0;
    virtual bool send(const std::vector<CanFrame>& frames) = 0;

    // === Приём (timeout в миллисекундах) ===
    virtual bool receive(CanFrame& frame, unsigned long timeout) = 0;
    virtual bool receive(std::vector<CanFrame>& frames, unsigned long timeout) = 0;

    // === Очистка буферов ===
    virtual void clearRx() = 0;
    virtual void clearTx() = 0;

    // === Периодические сообщения ===
    // Используется для keep-alive (0x3E 0x80) и wake-up последовательностей.
    // Если аппаратный таймер не поддерживается, адаптер эмулирует периодику
    // в отдельном потоке.
    virtual bool startPeriodicMsg(const CanFrame& frame,
                                  unsigned long intervalMs,
                                  unsigned long& msgId) = 0;
    virtual bool stopPeriodicMsg(unsigned long msgId) = 0;

    // === CAN-фильтры ===
    // Аппаратная или программная фильтрация по CAN ID.
    virtual bool startMsgFilter(unsigned long filterType,
                                const CanFrame& mask,
                                const CanFrame& pattern,
                                unsigned long& filterId) = 0;
    virtual bool stopMsgFilter(unsigned long filterId) = 0;

    // === Управление ===
    virtual bool setConfig(unsigned long parameter, unsigned long value) = 0;
    virtual bool ioctl(unsigned long ioctlId,
                       const void* input,
                       void* output) = 0;
};
```

Адаптер возвращает `false` для неподдерживаемой операции — вызывающий код уже умеет это обрабатывать (как сейчас `J2534Channel` возвращает `STATUS_NOT_SUPPORTED`).

### 4.3. `J2534ChannelAdapter` — адаптер для существующего PassThru-устройства

```cpp
// Common/common/J2534ChannelAdapter.hpp
class J2534ChannelAdapter final : public ICanChannel {
public:
    explicit J2534ChannelAdapter(std::unique_ptr<j2534::J2534Channel> channel);
    ~J2534ChannelAdapter() override;

    // ICanChannel — все методы транслируются в вызовы J2534Channel
    bool send(const CanFrame& frame) override;
    bool send(const std::vector<CanFrame>& frames) override;
    bool receive(CanFrame& frame, unsigned long timeout) override;
    bool receive(std::vector<CanFrame>& frames, unsigned long timeout) override;
    void clearRx() override;
    void clearTx() override;
    bool startPeriodicMsg(const CanFrame&, unsigned long, unsigned long&) override;
    bool stopPeriodicMsg(unsigned long) override;
    bool startMsgFilter(unsigned long, const CanFrame&, const CanFrame&, unsigned long&) override;
    bool stopMsgFilter(unsigned long) override;
    bool setConfig(unsigned long parameter, unsigned long value) override;
    bool ioctl(unsigned long, const void*, void*) override;
};
```

**Внутренняя конверсия `CanFrame → PASSTHRU_MSG`:**

```
PASSTHRU_MSG.Data[0..3] = CAN ID (big-endian, зависит от ProtocolID)
PASSTHRU_MSG.Data[4..N] = CanFrame.data
PASSTHRU_MSG.DataSize   = CanFrame.data.size() + 4
PASSTHRU_MSG.ProtocolID = _protocolId (сохранён при создании адаптера)
PASSTHRU_MSG.Flags      = _txFlags (сохранён при создании адаптера)
```

**Обратная конверсия `PASSTHRU_MSG → CanFrame`:**

```
CanFrame.id = PASSTHRU_MSG.Data[0..3]
CanFrame.data = PASSTHRU_MSG.Data[4..DataSize]
CanFrame.isExtendedId = (PASSTHRU_MSG.Flags & CAN_29BIT_ID) != 0
```

`PASSTHRU_MSG` остаётся только в `J2534ChannelAdapter.cpp` и в `j2534/` (внешняя зависимость).

### 4.4. Изменение `J2534ChannelProvider`

Текущий `J2534ChannelProvider` создаёт и возвращает `unique_ptr<J2534Channel>`. После изменений:

```cpp
// Common/common/J2534ChannelProvider.hpp
class J2534ChannelProvider {
public:
    std::vector<std::unique_ptr<ICanChannel>> getAllChannels(uint32_t ecuId) const;
    std::unique_ptr<ICanChannel> getChannelForEcu(uint32_t ecuId) const;
    // ...
};
```

Каждый созданный `J2534Channel` оборачивается в `J2534ChannelAdapter`. Для D2-протоколов каналы с ISO9141 (K-Line) не создаются — проверка `getProtocolId() != ISO9141` из `D2ProtocolCommonSteps` уходит вместе с каналом. Логика `UDSProtocolCommonSteps::openChannels` (создание UDS-канала + low-speed канала) переносится внутрь `J2534ChannelProvider`.

## 5. Что меняется в каждом слое

### 5.1. `*ProtocolCommonSteps`

| Сигнатура | Было | Стало |
|---|---|---|
| `UDSProtocolCommonSteps::openChannels` | `(J2534&, baudrate, canId)` → `vector<unique_ptr<J2534Channel>>` | Удаляется. Открытие каналов уходит в `J2534ChannelProvider` / `ChannelProviderFactory` |
| Все остальные методы | `(const J2534Channel& channel, uint32_t canId, ...)` | `(ICanChannel& channel, ...)`. `canId` уходит в `CanFrame.id` внутри метода |
| `D2ProtocolCommonSteps::fallAsleep` | `channels[i]->startPeriodicMsgs(D2Message(...), 5)` | `channels[i]->startPeriodicMsg({0x7DF, {0x10, 0x02}}, 5, id)` |
| `D2ProtocolCommonSteps::wakeUp` | `channels[i]->writeMsgs(D2Message::wakeUpCanRequest, ...)` | `channels[i]->send(wakeUpCanFrame)` + проверка `ISO9141` удаляется |

`channels[i]->getProtocolId()` и `channel.getProtocolId()` / `channel.getTxFlags()` — эти вызовы удаляются. Адаптер хранит ProtocolID/TxFlags внутри, конверсия `CanFrame → PASSTHRU_MSG` использует их неявно.

### 5.2. `UDSRequest` / `D2Request`

```cpp
// Было:
class UDSRequest {
    std::vector<uint8_t> process(const j2534::J2534Channel& channel, size_t timeout = 1000);
    std::vector<uint8_t> process(const j2534::J2534Channel& channel,
                                  const std::vector<uint8_t>& checkData,
                                  size_t retryCount = 1, size_t timeout = 1000);
};

// Стало:
class UDSRequest {
    std::vector<uint8_t> process(ICanChannel& channel, size_t timeout = 1000);
    std::vector<uint8_t> process(ICanChannel& channel,
                                  const std::vector<uint8_t>& checkData,
                                  size_t retryCount = 1, size_t timeout = 1000);
};
```

Внутренняя реализация: `channel.writeMsgs(_message, numMsgs)` → `channel.send({canId, payload})`.  
`_message` (UDSMessage) больше не используется — хранится `uint32_t _canId` + `std::vector<uint8_t> _data` напрямую.

### 5.3. `KWPProtocolCommonSteps` / `TP20Session` / `RequestProcessorBase`

`KWPProtocolCommonSteps` принимает `RequestProcessorBase&` — этот интерфейс не меняется, так как он уже абстрактный. Меняются его реализации:
- `UDSRequestProcessor` — переходит с `J2534Channel&` на `ICanChannel&`
- `TP20RequestProcessor` — переходит с `J2534Channel&` (через `TP20Session`) на `ICanChannel&`

`TP20Session` и `TP20RequestProcessor` меняют `J2534Channel&` → `ICanChannel&` аналогично `UDSRequest`.

### 5.4. `*Flasher` (hFSM2)

`UDSFlasherImpl` и `KWPFlasherImpl` хранят `ICanChannel&` вместо `J2534Channel&`. Методы `startImpl()` получают `vector<unique_ptr<ICanChannel>>&`.

### 5.5. `FlasherBase`

```cpp
// Было:
common::J2534ChannelProvider _j2534ChannelProvider;

// Стало:
common::J2534ChannelProvider _channelProvider;  // тип не меняется, меняется возврат
```

`start()` вызывает `_channelProvider.getAllChannels(ecuId)` — возвращает `vector<unique_ptr<ICanChannel>>`.

## 6. Полный список изменяемых файлов

### Новые файлы (3)

| № | Файл | Описание |
|---|---|---|
| 1 | `Common/common/CanFrame.hpp` | Структура CAN-сообщения |
| 2 | `Common/common/ICanChannel.hpp` | Абстрактный интерфейс канала |
| 3 | `Common/common/J2534ChannelAdapter.hpp` | Адаптер J2534 → ICanChannel |
| 4 | `Common/src/J2534ChannelAdapter.cpp` | Реализация адаптера |

### Изменяемые файлы (14)

| № | Файл | Изменение |
|---|---|---|
| 1 | `Common/common/J2534ChannelProvider.hpp` | Возвращает `unique_ptr<ICanChannel>` |
| 2 | `Common/src/J2534ChannelProvider.cpp` | Оборачивает каналы в `J2534ChannelAdapter`, не создаёт ISO9141 |
| 3 | `Common/common/protocols/UDSProtocolCommonSteps.hpp` | `J2534Channel&` → `ICanChannel&` |
| 4 | `Common/src/protocols/UDSProtocolCommonSteps.cpp` | `PASSTHRU_MSG` → `CanFrame`; `UDSMessage` → встроенная отправка |
| 5 | `Common/common/protocols/D2ProtocolCommonSteps.hpp` | `J2534Channel&` → `ICanChannel&` |
| 6 | `Common/src/protocols/D2ProtocolCommonSteps.cpp` | `PASSTHRU_MSG` → `CanFrame`; `D2Message` → встроенная отправка |
| 7 | `Common/common/protocols/UDSRequest.hpp` | `process(const J2534Channel&)` → `process(ICanChannel&)` |
| 8 | `Common/src/protocols/UDSRequest.cpp` | `channel.writeMsgs(_message)` → `channel.send(CanFrame{canId, _data})` |
| 9 | `Flasher/flasher/D2FlasherBase.hpp` | Сигнатура `startImpl` |
| 10 | `Flasher/src/D2FlasherBase.cpp` | Использует `ICanChannel&` |
| 11 | `Flasher/src/UDSFlasher.cpp` | hFSM2 context + `startImpl` тип канала |
| 12 | `Flasher/src/KWPFlasher.cpp` | hFSM2 context + `startImpl` тип канала |
| 13 | `Flasher/flasher/FlasherBase.hpp` | Тип возврата `J2534ChannelProvider` |
| 14 | `Flasher/src/FlasherBase.cpp` | Адаптация под новый тип |

### Файлы, которые НЕ меняются

- `VolvoFlasher/src/VolvoFlasher.cpp` — main() использует `FlasherBase` через уже абстрактные методы
- `VolvoLogger/src/*` — то же самое
- `j2534/` — внешняя зависимость, не трогается
- `BaseMessage`, `CanMessage`, `UDSMessage`, `D2Message` — остаются как наследие, единственный пользователь — `J2534ChannelAdapter`
- `CanMessagesTransceiver` — может быть переведён позже
- `keys.cpp`, `data.yaml`, `docs/`

## 7. Порядок реализации

### Шаг 1: Базовые типы (без изменения существующего кода)
1. `Common/common/CanFrame.hpp` — структура
2. `Common/common/ICanChannel.hpp` — интерфейс

### Шаг 2: Адаптер
3. `Common/common/J2534ChannelAdapter.hpp` — объявление
4. `Common/src/J2534ChannelAdapter.cpp` — реализация конверсии CanFrame ↔ PASSTHRU_MSG

### Шаг 3: `J2534ChannelProvider`
5. Изменить возвращаемые типы
6. Оборачивать каналы в `J2534ChannelAdapter`

### Шаг 4: Протокольные шаги и Request'ы
7. `UDSProtocolCommonSteps` (.hpp + .cpp)
8. `D2ProtocolCommonSteps` (.hpp + .cpp)
9. `UDSRequest` (.hpp + .cpp)
10. `D2Request` (если используется)
11. `TP20Session` / `UDSRequestProcessor` / `TP20RequestProcessor`

### Шаг 5: Флешеры
12. `FlasherBase`
13. `D2FlasherBase`
14. `UDSFlasher`
15. `KWPFlasher`

### Шаг 6: Сборка и проверка
16. `cmake --build build --config Release`

## 8. Пример конверсии

### `UDSRequest::process()` — было/стало

```cpp
// === БЫЛО ===
// UDSRequest хранит UDSMessage (наследует BaseMessage)
// process() вызывает channel.writeMsgs(_message)

std::vector<uint8_t> UDSRequest::process(const j2534::J2534Channel& channel,
                                          size_t timeout)
{
    unsigned long numMsgs = 0;
    if(channel.writeMsgs(_message, numMsgs, timeout) != STATUS_NOERROR || numMsgs < 1)
        throw std::runtime_error("Failed to send");

    std::vector<uint8_t> result;
    channel.readMsgs([&result, this](const uint8_t* data, size_t dataSize) {
        // парсинг PASSTHRU_MSG.Data
        result.assign(data, data + dataSize);
        return false;
    }, timeout);
    return result;
}

// === СТАЛО ===
// UDSRequest хранит uint32_t _canId + std::vector<uint8_t> _data
// process() вызывает channel.send(CanFrame)

std::vector<uint8_t> UDSRequest::process(ICanChannel& channel,
                                          size_t timeout)
{
    CanFrame request{_canId, _data};
    if (!channel.send(request))
        throw std::runtime_error("Failed to send");

    CanFrame response;
    if (!channel.receive(response, timeout))
        throw std::runtime_error("Failed to receive");

    return parseResponse(response.data);  // парсинг CanFrame.data
}
```

### `UDSProtocolCommonSteps::eraseChunk()` — было/стало

```cpp
// === БЫЛО ===
bool UDSProtocolCommonSteps::eraseChunk(const j2534::J2534Channel& channel,
                                         uint32_t canId,
                                         const VBFChunk& chunk)
{
    UDSMessage eraseRoutineMsg(canId, {0x31, 0x01, 0xff, 0x00, ...});
    unsigned long numMsgs;
    if (channel.writeMsgs(eraseRoutineMsg, numMsgs) != STATUS_NOERROR)
        return false;
    const auto result = readMessageCheckAndGet(channel, {0x71, ...}, {}, 10);
    return !result.empty();
}

// === СТАЛО ===
bool UDSProtocolCommonSteps::eraseChunk(ICanChannel& channel,
                                         uint32_t canId,
                                         const VBFChunk& chunk)
{
    if (!channel.send({canId, {0x31, 0x01, 0xff, 0x00, ...}}))
        return false;
    // readMessageCheckAndGet переведён на ICanChannel
    const auto result = readMessageCheckAndGet(channel, {0x71, ...}, {}, 10);
    return !result.empty();
}
```

## 9. Критерии готовности

1. `*ProtocolCommonSteps` заголовки не содержат `#include <j2534/...>`
2. `UDSRequest`, `D2Request`, `TP20Session` заголовки не содержат `#include <j2534/...>`
3. `PASSTHRU_MSG` не встречается нигде, кроме `J2534ChannelAdapter.cpp` и `j2534/` (внешняя зависимость)
4. `BaseMessage` / `CanMessage` / `UDSMessage` / `D2Message` не импортируются из `Common/common/protocols/`, `Flasher/`, `Logger/`. Исключение — `J2534ChannelAdapter` (единственный класс, которому нужен `BaseMessage` и `PASSTHRU_MSG`)
5. Проект собирается без ошибок: `cmake --build build --config Release`
6. Функциональность flash/read/pin/log полностью сохранена (сравнение статусов и callback'ов при тестировании с J2534-адаптером)
