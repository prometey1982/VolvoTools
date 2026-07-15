# ТЗ: CanIdProvider — генерация CAN ID для разных протоколов

## 1. Цель

Устранить хардкод CAN ID в `UDSProtocolCommonSteps`, `D2ProtocolCommonSteps` и будущих `PinCrackerSteps`. Выделить единый механизм генерации физических (point-to-point) и функциональных (broadcast) CAN ID в зависимости от протокола, разрядности шины и конфигурации ЭБУ.

## 2. Проблема

### 2.1. Текущий хардкод

```
UDSProtocolCommonSteps.cpp:
  fallAsleep → send({0x7DF, {0x10, 0x02}}, ...)       // 11-bit hardcode
  keepAlive  → send({0x7DF, {0x3E, 0x80}}, ...)       // 11-bit hardcode
  wakeUp     → send({0x7DF, {0x11, 0x11}}, ...)        // 11-bit hardcode
  authorize  → принимает uint32_t canId из параметра    // единственное правильное место
```

Для P2_UDS (29-bit) эти хардкоды не работают — нужны `0x18DB33F1` для broadcast и `0x18DA50F1` для physical.

### 2.2. Три формата CAN ID

| Протокол | Physical (к ЭБУ) | Functional (broadcast) | Формула |
|---|---|---|---|
| UDS 11-bit | `ecuConfig.canId` (из data.yaml) | `0x7DF` | Константа |
| UDS 29-bit | `0x18DA{ps}F1` | `0x18DB{group}F1` | `0x18DA{PS}F1 = 0x18DA00F1 \| (PS << 8)` |
| D2 | `0xFFFFE` (константа) | `0xFFFFE` (тот же) | Константа |
| KWP/TP20 | `ecuConfig.canId` | Не используется | Из data.yaml |

Где:
- `PS` (PDU Specific) = адрес ЭБУ для physical (`0x50` для CEM), группа для functional (`0x33` для powertrain)
- `SA` (Source Address) = `0xF1` (инструмент/тестер)
- Для 29-bit UDS: `0x18DA{PS}SA` = `0x18DA00F1 | (PS << 8)`
- Для D2: `0xFFFFE` — фиксированный ID для всех D2-сообщений

## 3. Целевая архитектура

### 3.1. Диаграмма классов

```
           CanIdProvider (abstract)
                │
        ┌───────┼───────────┐
        ▼       ▼           ▼
  CanId11bit  CanId29bit  CanIdD2
  (ISO15765)  (ISO15765)  (CAN/D2)
```

```
BusConfiguration (data.yaml) → CanIdProviderFactory
                                      ↓
                               CanIdProvider
                                    │
                    ┌───────────────┴───────────────────┐
                    ▼                                   ▼
           UDSProtocolCommonSteps               PinCrackerSteps
           (fallAsleep, wakeUp, keepAlive)      (preAuth, postAuth, tryPin)
```

### 3.2. Интерфейс

```cpp
// Common/common/CanIdProvider.hpp
#pragma once

#include <cstdint>

namespace common {

class CanIdProvider {
public:
    virtual ~CanIdProvider() = default;

    // CAN ID для point-to-point сообщений (к конкретному ЭБУ).
    virtual uint32_t getPhysCanId() const = 0;

    // CAN ID для broadcast/functional сообщений (ко всем ЭБУ на шине).
    virtual uint32_t getFuncCanId() const = 0;

    // 11 или 29 бит.
    virtual bool isExtendedId() const = 0;
};

} // namespace common
```

### 3.3. Реализации

#### 3.3.1. CanId11bit — ISO15765 11-bit

```cpp
// Common/common/CanIdProvider.hpp
class CanId11bit final : public CanIdProvider {
public:
    // canId = CANIdentifier из YAML (0x7E0 для ECM, 0x7E1 для TCM...)
    explicit CanId11bit(uint32_t canId)
        : _physCanId{ canId }
    {}

    uint32_t getPhysCanId() const override { return _physCanId; }
    uint32_t getFuncCanId() const override { return 0x7DF; }
    bool isExtendedId() const override { return false; }

private:
    uint32_t _physCanId;  // из data.yaml CANIdentifier (0x7E0, 0x7E1, ...)
};
```

Используется: P3/SPA/Ford_UDS/VAG (там, где data.yaml: `protocolId=ISO15765, canIdBitSize=11`, заполнен `CANIdentifier`).

#### 3.3.2. CanId29bit — ISO15765 29-bit

```cpp
// Common/common/CanIdProvider.hpp
class CanId29bit final : public CanIdProvider {
public:
    // ecuId = Address из YAML (PS-байт: 0x50 для CEM)
    // funcGroup = группа для broadcast (0x33 = powertrain)
    // sa = source address (0xF1 для инструмента)
    CanId29bit(uint32_t ecuId, uint32_t funcGroup, uint32_t sa = 0xF1)
        : _physCanId{ buildPhysCanId(ecuId, sa) }
        , _funcCanId{ buildFuncCanId(funcGroup, sa) }
    {}

    uint32_t getPhysCanId() const override { return _physCanId; }
    uint32_t getFuncCanId() const override { return _funcCanId; }
    bool isExtendedId() const override { return true; }

private:
    static uint32_t buildPhysCanId(uint32_t ps, uint32_t sa) {
        // 0x18DA{PS}SA
        return 0x18DA00F1 | ((ps & 0xFF) << 8);
    }

    static uint32_t buildFuncCanId(uint32_t group, uint32_t sa) {
        // 0x18DB{group}SA
        return 0x18DB00F1 | ((group & 0xFF) << 8);
    }

    uint32_t _physCanId;
    uint32_t _funcCanId;
};
```

Используется: P2_UDS, P1_UDS (там, где `protocolId=ISO15765, canIdBitSize=29`, `CANIdentifier` отсутствует).

Параметры берутся из `data.yaml`:
- `ps` = `Address` YAML (маппится в `ecuInfo.ecuId` — PS-байт, например `0x50` для CEM)
- `funcGroup` = `busConfiguration.funcGroup` (если есть, иначе `0x33` по умолчанию)
- `CANIdentifier` не используется (для 29-bit = `0`)

#### 3.3.3. CanIdD2 — D2-протокол (CAN 29-bit)

```cpp
// Common/common/CanIdProvider.hpp
class CanIdD2 final : public CanIdProvider {
public:
    static constexpr uint32_t D2_CAN_ID = 0xFFFFE;

    uint32_t getPhysCanId() const override { return D2_CAN_ID; }
    uint32_t getFuncCanId() const override { return D2_CAN_ID; }
    bool isExtendedId() const override { return true; }
};
```

Используется: P80/P1/P2 (там, где `protocolId=CAN, canIdBitSize=29`).

#### 3.3.4. CanIdTP20 — KWP/TP20 (ISO14230)

```cpp
// Common/common/CanIdProvider.hpp
class CanIdTP20 final : public CanIdProvider {
public:
    // TP20 — последовательный протокол, CAN ID не используется.
    // Реализация заглушка — методы не должны вызываться.
    uint32_t getPhysCanId() const override { return 0; }
    uint32_t getFuncCanId() const override { return 0; }
    bool isExtendedId() const override { return false; }
};
```

### 3.4. Фабрика

```cpp
// Common/common/CanIdProvider.hpp
namespace common {

std::unique_ptr<CanIdProvider> createCanIdProvider(
    unsigned long protocolId,
    uint32_t canIdBitSize,
    uint32_t ecuId,         // Address из YAML (PS-байт для 29-bit, игнорируется для D2)
    uint32_t canId,         // CANIdentifier из YAML (11-bit, для 29-bit и D2 = 0)
    uint32_t funcGroup = 0x33  // для 29-bit broadcast
);

} // namespace common
```

Логика фабрики:
- `protocolId == ISO15765 && canIdBitSize == 11` → `CanId11bit(canId)` (func=`0x7DF`), где `canId` — из `CANIdentifier` YAML
- `protocolId == ISO15765 && canIdBitSize == 29` → `CanId29bit(ecuId, funcGroup)` (phys=`0x18DA{ecuId}F1`, func=`0x18DB{group}F1`), `canId` игнорируется
- `protocolId == CAN && canIdBitSize == 29` → `CanIdD2()` (phys=func=`0xFFFFE`), оба ID игнорируются
- `protocolId == ISO14230 || protocolId == TP20` → `CanIdTP20()` (CAN ID не используется)

**Откуда берутся параметры:**

| Поле | YAML-ключ | Для 11-bit UDS | Для 29-bit UDS | Для D2 |
|---|---|---|---|---|
| `ecuId` | `Address` | Не используется | PS-байт (`0x50` для CEM) | Игнорируется |
| `canId` | `CANIdentifier` | CAN ID (`0x7E0` для ECM) | `0` (не заполнен) | `0` (не заполнен) |

**Для bus-only провайдера** (когда нужен только `funcCanId` для `preAuth`/`postAuth`):
- `ecuId=0, canId=0` — `getPhysCanId()` вернёт `0` (не используется)
- Для D2 это не имеет значения — `CanIdD2` игнорирует оба поля

**Для ECU-провайдера** (когда нужен `physCanId` для `tryPin`):
- 11-bit UDS: `canId = ecuInfo.canId` (из `CANIdentifier`)
- 29-bit UDS: `ecuId = ecuInfo.ecuId` (из `Address`, это PS-байт)
- D2: `CanIdD2` всегда возвращает `0xFFFFE`, параметры не нужны

### 3.5. Структура данных конфигурации (data.yaml)

Для 29-bit UDS необходимо дополнить `BusConfiguration` полем `funcGroup` (если отсутствует — `0x33` по умолчанию):

```yaml
busInfo:
  - name: "high_speed"
    protocolId: 6           # ISO15765
    baudrate: 500000
    canIdBitSize: 29
    funcGroup: 0x33         # ← новое поле
    ecuInfo:
      - ecuId: 0x7A
        canId: 0x50         # PS (адрес ЭБУ)
        name: "CEM"
```

**Обратная совместимость:** если `funcGroup` отсутствует, фабрика использует `0x33`.

## 4. Где используется CanIdProvider

### 4.0. Схема применения

```
CanIdProvider
     │
     ├── physCanId (point-to-point) ── для tryPin / authorize / requestDownload / TransferData
     │      ├── UDSPinCrackerSteps::tryPin()
     │      ├── D2PinCrackerSteps::tryPin()
     │      └── (уже корректно: UDSRequest принимает canId параметром)
     │
     └── funcCanId (broadcast) ── для fallAsleep / wakeUp / keepAlive / enterProgSession
            ├── UDSPinCrackerSteps::preAuth/postAuth
            ├── D2PinCrackerSteps::preAuth/postAuth
            ├── UDSFlasher::startImpl()     ← сейчас хардкод 0x7DF
            ├── UDSPinFinder                ← сейчас хардкод 0x7DF
            └── KWPFlasher                  ← TP20 не использует CAN ID
```

### 4.1. Что НЕ требует CanIdProvider

| Компонент | Причина |
|---|---|
| `UDSRequest::process(channel, canId, ...)` | Уже принимает `uint32_t canId` параметром — не нужен провайдер |
| `D2Request::process(channel, ...)` | D2 использует фиксированный `0xFFFFE`, известен из `D2Message::CanId` |
| `UDSProtocolCommonSteps::authorize(channel, canId, pin)` | Уже принимает `canId` как `uint32_t` |
| `UDSProtocolCommonSteps::transferData(channel, canId, ...)` | Уже принимает `canId` как `uint32_t` |
| `J2534ChannelProvider` | Его задача — открыть канал, не генерировать CAN ID |

### 4.2. Что требует CanIdProvider

| Компонент | Зачем | Сейчас |
|---|---|---|
| `UDSPinCrackerSteps::preAuth()` | fallAsleep/enterProgSession на `funcCanId` | Хардкод `0x7DF` в `UDSProtocolCommonSteps` |
| `UDSPinCrackerSteps::postAuth()` | wakeUp на `funcCanId` | Хардкод `0x7DF` |
| `UDSPinCrackerSteps::keepAlive()` | keepAlive на `funcCanId` | Хардкод `0x7DF` |
| `UDSPinCrackerSteps::tryPin()` | authorize на `physCanId` ЭБУ | Уже параметр — но CanIdProvider его предоставит |
| `D2PinCrackerSteps` | Все D2-сообщения на `0xFFFFE` (phys=func) | `D2Message::CanId` — но CanIdProvider унифицирует |
| `UDSFlasher` | fallAsleep/wakeUp на `funcCanId` | Через `UDSProtocolCommonSteps` с хардкодом |
| `UDSPinFinder` | fallAsleep/keepAlive/wakeUp на `funcCanId` | Через `UDSProtocolCommonSteps` |

### 4.3. Можно ли обойтись без CanIdProvider?

Технически — да, если передавать `uint32_t funcCanId` и `uint32_t physCanId` отдельными параметрами везде, где нужно. Но:

**Без провайдера:**
```cpp
// Плюс: нет нового класса
// Минус: логика генерации ID размазана по всем вызывающим
UDSProtocolCommonSteps::fallAsleep(channels, 0x18DB33F1);  // P2_UDS
UDSProtocolCommonSteps::fallAsleep(channels, 0x7DF);       // P3
```

**С провайдером:**
```cpp
// Плюс: логика в одном месте, вызывающий не знает про формат ID
UDSProtocolCommonSteps::fallAsleep(channels, canIdProvider->getFuncCanId());
```

Если на каком-то этапе решено, что оверхед нового класса не оправдан, можно ограничиться двумя функциями-хелперами:
```cpp
uint32_t getFuncCanId(unsigned long protocolId, uint32_t bitSize, uint32_t funcGroup);
uint32_t getPhysCanId(unsigned long protocolId, uint32_t bitSize, uint32_t ecuCanId);
```

Но интерфейс `CanIdProvider` даёт расширяемость для будущих протоколов без изменения сигнатур.

## 5. Интеграция в существующий код

### 5.1. UDSProtocolCommonSteps

Текущие методы принимают `uint32_t canId` для `authorize`, `transferData`, `transferChunk`, `eraseFlash`, `eraseChunk`, `startRoutine`, `checkValidApplication` — они **не меняются**, параметр остаётся.

Меняются только методы, которые хардкодят broadcast ID:

```diff
- bool UDSProtocolCommonSteps::fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels);
+ bool UDSProtocolCommonSteps::fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels,
+                                          uint32_t funcCanId);
```

```diff
  // Раньше:
- channels[i]->startPeriodicMsg({0x7DF, {0x10, 0x02}}, 5, msgId)
  // Теперь:
+ channels[i]->startPeriodicMsg({funcCanId, {0x10, 0x02}}, 5, msgId)
```

Аналогично для `wakeUp` и `keepAlive` — получают `funcCanId`/`physCanId` через параметр.

```cpp
// Новая сигнатура
class UDSProtocolCommonSteps {
public:
    static bool fallAsleep(const std::vector<std::unique_ptr<ICanChannel>>& channels,
                            uint32_t funcCanId);
    static std::vector<unsigned long> keepAlive(ICanChannel& channel,
                                                  uint32_t funcCanId);
    static void wakeUp(const std::vector<std::unique_ptr<ICanChannel>>& channels,
                        uint32_t funcCanId);
    // Остальные методы — без изменений (принимают uint32_t canId)
};
```

### 5.2. Per-bus: свой CanIdProvider на каждую шину

На платформах с двумя CAN-шинами (P2_UDS, P1_UDS) каждая шина может иметь свой протокол. Это значит, что **один `CanIdProvider` не покрывает все сценарии** — нужен отдельный провайдер на каждую шину плюс отдельный для целевого ЭБУ.

**Пример P2_UDS (целевой ЭБУ — CEM):**

```
Шина A (high-speed): ISO15765 29-bit → CanId29bit(funcGroup=0x33)
                                         funcCanId = 0x18DB33F1  (broadcast)
Шина B (low-speed):  CAN 29-bit (D2) → CanIdD2()
                                         funcCanId = 0xFFFFE
ЭБУ CEM (на шине A):                 → CanId29bit(ps=0x50)
                                         physCanId = 0x18DA50F1  (point-to-point)
```

**Рекомендуемый паттерн в `PinCracker::run()` или `UDSFlasherImpl`:**

```cpp
// 1. Открыть ВСЕ шины (getAllChannels возвращает каналы для каждой шины)
auto channels = _channelProvider.getAllChannels(ecuId);

// 2. На каждую шину — свой CanIdProvider (только для funcCanId)
//    Для bus-only: ecuId=0, canId=0 — getPhysCanId() не используется
std::vector<std::pair<std::unique_ptr<ICanChannel>, std::unique_ptr<CanIdProvider>>> buses;
for (auto& ch : channels) {
    auto busConfig = getBusConfig(ch);   // из data.yaml
    auto provider = createCanIdProvider(
        busConfig.protocolId,
        busConfig.canIdBitSize,
        0,           // ecuId=0 (bus-only)
        0,           // canId=0 (bus-only)
        busConfig.funcGroup
    );
    buses.emplace_back(std::move(ch), std::move(provider));
}

// 3. Для целевого ЭБУ — отдельный CanIdProvider (physCanId)
//    Для UDS 11-bit:  canId = ecuInfo.canId  (CANIdentifier)
//    Для UDS 29-bit:  ecuId = ecuInfo.ecuId  (Address = PS-байт)
//    Для D2:          CanIdD2 игнорирует оба параметра
auto [busInfo, ecuInfo] = getEcuInfoByEcuId(carPlatform, ecuId);
auto ecuProvider = createCanIdProvider(
    busInfo.protocolId,
    busInfo.canIdBitSize,
    ecuInfo.ecuId,     // Address из YAML (PS для 29-bit, игнорируется для D2)
    ecuInfo.canId,     // CANIdentifier из YAML (для 11-bit, для 29-bit/D2 = 0)
    busInfo.funcGroup
);
auto& ecuChannel = getChannelByEcuId(carPlatform, ecuId, channels);
```

**preAuth/postAuth — на каждой шине со своим funcCanId:**

```cpp
for (auto& [ch, provider] : buses) {
    UDSProtocolCommonSteps::fallAsleep({ch}, provider->getFuncCanId());
}
// ... цикл перебора ...
for (auto& [ch, provider] : buses) {
    UDSProtocolCommonSteps::wakeUp({ch}, provider->getFuncCanId());
}
```

**tryPin — на канале ЭБУ с его physCanId:**

```cpp
UDSProtocolCommonSteps::authorize(ecuChannel, ecuProvider->getPhysCanId(), pin);
```

### 5.3. PinCrackerSteps

`PinCrackerSteps` получает `CanIdProvider` для своей шины через конструктор:

```cpp
class UDSPinCrackerSteps : public PinCrackerSteps {
public:
    // Принимает CanIdProvider для ШИНЫ (funcCanId + при необходимости physCanId)
    UDSPinCrackerSteps(std::unique_ptr<CanIdProvider> canIdProvider,
                       bool needProgSession = false);

    bool preAuth(ICanChannel& channel) override {
        if (_needProgSession) {
            return enterProgSession(channel, _canIdProvider->getFuncCanId());
        }
        return UDSProtocolCommonSteps::fallAsleep({&channel},
                                                   _canIdProvider->getFuncCanId());
    }

    bool tryPin(ICanChannel& channel, uint64_t pin) override {
        // physCanId может быть адресом ЭБУ (если провайдер создан для ЭБУ)
        // или функциональным ID (если провайдер создан для шины)
        return UDSProtocolCommonSteps::authorize(
            channel, _canIdProvider->getPhysCanId(), toPinArray(pin));
    }

    void postAuth(ICanChannel& channel) override {
        UDSProtocolCommonSteps::wakeUp({&channel},
                                        _canIdProvider->getFuncCanId());
    }

private:
    std::unique_ptr<CanIdProvider> _canIdProvider;
    bool _needProgSession;
};
```

**Важно:** `preAuth`/`postAuth` не используют `getPhysCanId()` — им нужен только `funcCanId`. Для этого можно создать провайдер только для шины (с `ecuCanId=0`). `tryPin` использует `getPhysCanId()` — для этого нужен провайдер с адресом ЭБУ.

### 5.4. D2ProtocolCommonSteps

Для D2 изменения минимальны — `CanIdD2` возвращает `0xFFFFE`, который уже используется в `D2Message::CanId`:

```cpp
// D2ProtocolCommonSteps — использует D2Message::CanId (0xFFFFE)
// Может быть опционально заменён на _canIdProvider->getPhysCanId(),
// но существующее поведение не требует изменений.
```

## 6. Изменяемые файлы

### 6.1. Новые

| № | Файл | Описание |
|---|---|---|
| 1 | `Common/common/CanIdProvider.hpp` | Интерфейс + 4 реализации + фабрика (все в одном header) |
| 2 | `Common/src/CanIdProvider.cpp` | Фабрика + CanId29bit::формат |

### 6.2. Изменяемые

| № | Файл | Изменение |
|---|---|---|
| 3 | `Common/common/protocols/UDSProtocolCommonSteps.hpp` | `fallAsleep`, `keepAlive`, `wakeUp` — добавить `uint32_t funcCanId` |
| 4 | `Common/src/protocols/UDSProtocolCommonSteps.cpp` | Заменить `0x7DF` на параметр `funcCanId` |
| 5 | `Common/src/protocols/UDSPinFinder.cpp` | Использовать CanIdProvider вместо хардкода `0x7DF` |
| 6 | `Flasher/src/UDSFlasher.cpp` | Использовать CanIdProvider для fallAsleep/wakeUp |
| 7 | `Flasher/flasher/pin/UDSPinCrackerSteps.hpp` | Принимать `unique_ptr<CanIdProvider>` в конструктор |
| 8 | `Flasher/src/pin/UDSPinCrackerSteps.cpp` | Использовать CanIdProvider |
| 9 | `VolvoFlasher/src/VolvoFlasher.cpp` | `findPin2` — использовать CanIdProvider (через UDSPinCrackerSteps) |
| 10 | `Common/common/data.yaml` | Опционально: добавить `funcGroup` для 29-bit шин |

### 6.3. Не изменяются

- `UDSRequest` / `D2Request` — уже принимают `uint32_t canId` корректно
- `D2ProtocolCommonSteps` — использует `D2Message::CanId` (0xFFFFE)
- `D2Message` / `D2Messages` — без изменений
- `J2534ChannelProvider` — без изменений (он уже создаёт каналы с правильным protocolId)
- `BusConfiguration` / `ECUInfo` — без изменений, `funcGroup` опционален

## 7. Порядок реализации

### Шаг 1: CanIdProvider (без изменения существующего кода)

1. `Common/common/CanIdProvider.hpp` — интерфейс + 4 реализации + фабрика
2. `Common/src/CanIdProvider.cpp` — реализация фабрики
3. Написать тесты: убедиться, что генерация ID корректна:
   - `CanId11bit(0x7E0)` → phys=0x7E0, func=0x7DF, ext=false
   - `CanId29bit(0x50)` → phys=0x18DA50F1, func=0x18DB33F1, ext=true
   - `CanIdD2()` → phys=0xFFFFE, func=0xFFFFE, ext=true

### Шаг 2: UDSProtocolCommonSteps

4. Изменить сигнатуру `fallAsleep`, `keepAlive`, `wakeUp` — добавить `uint32_t funcCanId`
5. Заменить `0x7DF` на `funcCanId` в реализации
6. Обновить всех вызывающих (UDSPinFinder, UDSFlasher, VolvoFlasher)

### Шаг 3: Интеграция в PinCrackerSteps

7. `UDSPinCrackerSteps` принимает `unique_ptr<CanIdProvider>` в конструкторе
8. `preAuth`/`postAuth` используют `_canIdProvider->getFuncCanId()`
9. `tryPin` использует `_canIdProvider->getPhysCanId()`

### Шаг 4 (опционально): data.yaml + funcGroup

10. Добавить `funcGroup` в конфигурации 29-bit шин
11. `createCanIdProvider` может читать `funcGroup` из `BusConfiguration`

## 8. Критерии готовности

1. `CanIdProvider` — интерфейс с `getPhysCanId()`, `getFuncCanId()`, `isExtendedId()`
2. `CanId11bit` — phys=ecuCanId, func=`0x7DF`, ext=false
3. `CanId29bit(ps, funcGroup)` — phys=`0x18DA{ps}F1`, func=`0x18DB{funcGroup}F1`, ext=true
4. `CanIdD2()` — phys=func=`0xFFFFE`, ext=true
5. Фабрика `createCanIdProvider(protocolId, bitSize, ecuId, canId, funcGroup)` создаёт правильную реализацию:
   - `ISO15765 + 11bit` → `CanId11bit(canId)` (из `CANIdentifier` YAML)
   - `ISO15765 + 29bit` → `CanId29bit(ecuId, funcGroup)` (из `Address` YAML как PS-байт)
   - `CAN + 29bit` → `CanIdD2()` (параметры игнорируются)
6. `UDSProtocolCommonSteps::fallAsleep`/`keepAlive`/`wakeUp` не содержат хардкода `0x7DF`
7. `UDSPinCrackerSteps` использует CanIdProvider для получения CAN ID
8. Все старые вызовы `fallAsleep(0x7DF)` обновлены (P3/SPA/Ford_VAG передают `funcCanId=0x7DF`)
9. Сборка: 0 ошибок
10. Функциональность flash/read/pin сохранена для всех платформ (P3, SPA, P2_UDS, P1_UDS, Ford_UDS, VAG)
