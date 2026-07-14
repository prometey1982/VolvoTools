# Реализация подбора ПИН-кода

## 1. Цель

Унифицировать подбор ПИН-кодов для разных протоколов (UDS, KWP, D2) и платформ, устранив дублирование и hardcoded-последовательности в `VolvoFlasher.cpp`. Заменить HFSM2 в `UDSPinFinder` на стратегию с интерфейсом шагов. Выделить общий алгоритм перебора в базовый класс.

## 2. Текущая архитектура (было)

### 2.1. Проблемы

- **`PinCracker`** — пустая заглушка (`Flasher/flasher/pin/PinCracker.hpp`, с опечаткой `~Pinracker()` вместо `~PinCracker()`)
- **`PINCrackerBase`** — удалён (предыдущая пустая заглушка, заменён на `PinCracker`)
- **`UDSPinFinder`** (`Common/common/protocols/`) — HFSM2 с состояниями: `FallAsleep → KeepAlive → Authorize(loop) → WakeUp → Done/Error`
  - HFSM2 избыточен: нет событий, все переходы линейные
  - `Authorize::update()` — poll-loop, дёргает `authorize()` на каждом `fsm.update()`
  - Жёсткая привязка к UDS-протоколу — нет D2/KWP
- **`findPin()`** (`VolvoFlasher.cpp`) — raw PASSTHRU_MSG, дублирующий UDS-логику
- **`switchToDiagSession()`** (`VolvoFlasher.cpp`) — отдельная функция с raw `startPeriodicMsg`
- **Три копии алгоритма перебора**: `findPin()`, `findPin2()`, частично `switchToDiagSession()`

### 2.2. Схема текущих зависимостей

```
VolvoFlasher.cpp
  ├── findPin()           → authByKey()          → PASSTHRU_MSG напрямую
  ├── findPin2()          → UDSPinFinder(HFSM2)  → UDSProtocolCommonSteps
  └── switchToDiagSession → authByKey()          → PASSTHRU_MSG напрямую
```

### 2.3. Что отличается между платформами

| Параметр | UDS (P3/SPA/Ford/VAG) | KWP (VAG K-Line) | D2 (P2/P80) |
|---|---|---|---|
| Тип канала | ISO15765 (CAN) | TP20 (serial) | CAN 29-bit raw |
| Sleep | `0x10 0x02` periodic | Не нужен | Wake-паттерн |
| Keep-alive | `0x3E 0x80` | В сессии | `0x86` periodic |
| Сессия | Не нужна для Pin | `0x10 0x85` + reconnect | Загрузка bootloader |
| Auth | `0x27` + `generateKey()` | `0x27` + `generateKeyCommon()` | Через bootloader |
| Wake | `0x11 0x11/0x81` | Не нужен | D2 wake |
| Pin range | 0x000000–0xFFFFFF | Зависит | Зависит |
| Retry delay | ~5 сек при ошибке | Зависит | Зависит |

## 3. Целевая архитектура (стало)

### 3.1. Общий принцип

Заменить HFSM2 на паттерн **Strategy**. Алгоритм перебора ПИНа линеен и не требует конечного автомата:

```
           PinCracker (алгоритм)
                 │
          ┌──────┴──────┐
          ▼             ▼
    PinCrackerSteps  колбэки состояния
    (интерфейс)
          │
    ┌─────┼─────────┐
    ▼     ▼         ▼
 UDSSteps KWPSteps D2Steps
```

### 3.2. PinCrackerSteps — интерфейс платформо-специфичных шагов

```cpp
// Flasher/flasher/pin/PinCrackerSteps.hpp
#pragma once

#include "common/ICanChannel.hpp"
#include "common/CarPlatform.hpp"
#include "common/J2534ChannelProvider.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace flasher {

class PinCrackerSteps {
public:
    virtual ~PinCrackerSteps() = default;

    // === Открытие каналов ===
    // Создаёт и возвращает каналы для данной платформы/ЭБУ.
    // Может использовать J2534ChannelProvider или другой механизм.
    virtual std::vector<std::unique_ptr<ICanChannel>>
    openChannels(J2534ChannelProvider& provider, uint32_t ecuId) = 0;

    // === Pre-auth (один раз перед циклом) ===
    // fallAsleep, enterProgrammingSession и т.д.
    // По умолчанию — no-op.
    virtual bool preAuth(std::vector<std::unique_ptr<ICanChannel>>& channels) { return true; }

    // === Keep-alive во время цикла ===
    // Запускает периодические сообщения. Возвращает ID для остановки.
    virtual std::vector<unsigned long>
    startKeepAlive(std::vector<std::unique_ptr<ICanChannel>>& channels) { return {}; }

    virtual void stopKeepAlive(std::vector<unsigned long>& ids) { (void)ids; }

    // === Попытка одного ПИНа (ядро цикла) ===
    // Отправляет seed + key, возвращает true при успехе.
    virtual bool tryPin(ICanChannel& channel, uint64_t pin) = 0;

    // === Post-auth (один раз после цикла) ===
    // wakeUp, завершение сессии и т.д.
    // По умолчанию — no-op.
    virtual void postAuth(std::vector<std::unique_ptr<ICanChannel>>& channels) { (void)channels; }

    // === Конфигурация ===
    virtual uint64_t getMinPin() const { return 0; }
    virtual uint64_t getMaxPin() const { return 0xFFFFFF; }

    // Задержка между неудачными попытками (NRC 0x78, rate limiting).
    virtual std::chrono::milliseconds getRetryDelay() const { return std::chrono::milliseconds{0}; }
};

} // namespace flasher
```

### 3.3. PinCracker — общий алгоритм перебора

```cpp
// Flasher/flasher/pin/PinCracker.hpp
#pragma once

#include "flasher/pin/PinCrackerSteps.hpp"
#include "flasher/pin/PinCrackerStorage.hpp"
#include "common/J2534ChannelProvider.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace flasher {

class PinCracker {
public:
    enum class State {
        Initial,
        PreAuth,
        Work,
        PostAuth,
        Done,
        Error
    };

    enum class Direction {
        Up,
        Down
    };

    PinCracker(j2534::J2534& j2534, CarPlatform carPlatform,
               std::unique_ptr<PinCrackerSteps> steps,
               Direction direction, uint64_t startPin,
               std::function<void(State, uint64_t)> stateCallback,
               std::shared_ptr<PinCrackerStorage> storage = {});
    ~PinCracker();

    // Non-copyable
    PinCracker(const PinCracker&) = delete;
    PinCracker& operator=(const PinCracker&) = delete;

    State getCurrentState() const;
    std::optional<uint64_t> getFoundPin() const;

    bool start();   // запускает поток
    void stop();    // останавливает цикл

private:
    void run();

    J2534ChannelProvider _channelProvider;
    std::unique_ptr<PinCrackerSteps> _steps;
    std::shared_ptr<PinCrackerStorage> _storage;
    std::thread _thread;

    Direction _direction;
    uint64_t _startPin;

    std::function<void(State, uint64_t)> _stateCallback;

    mutable std::mutex _mutex;
    State _currentState;
    std::optional<uint64_t> _foundPin;
    std::atomic<bool> _stop{false};
};

} // namespace flasher
```

**Алгоритм `run()`:**

```
channels = _steps->openChannels(provider, ecuId)
if !channels: Error, return

setState(PreAuth)
if !_steps->preAuth(channels): Error, return

keepAliveIds = _steps->startKeepAlive(channels)

pin = _startPin
endPin = (_direction == Up) ? _steps->getMaxPin() : _steps->getMinPin()
step = (_direction == Up) ? 1 : -1

while pin <= endPin && pin >= endPin && !_stop:
    // Пропустить уже проверенные ПИНы
    while !_stop && _storage && _storage->isChecked(pin):
        pin += step

    setState(Work, pin)
    if _steps->tryPin(*channels[0], pin):
        _foundPin = pin
        _storage->markChecked(pin)     // отметить найденный
        break
    _storage->markChecked(pin)         // отметить неудачный
    pin += step
    sleep(_steps->getRetryDelay())

_steps->stopKeepAlive(keepAliveIds)

setState(PostAuth)
_steps->postAuth(channels)

_storage->flush()                      // сохранить состояние

setState(_foundPin ? Done : Error)
```

### 3.4. UDSPinCrackerSteps — реализация для UDS

```cpp
// Flasher/flasher/pin/UDSPinCrackerSteps.hpp
class UDSPinCrackerSteps final : public PinCrackerSteps {
public:
    explicit UDSPinCrackerSteps(CarPlatform carPlatform, uint8_t ecuId);

    std::vector<std::unique_ptr<ICanChannel>>
    openChannels(J2534ChannelProvider& provider, uint32_t ecuId) override;

    bool preAuth(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

    std::vector<unsigned long>
    startKeepAlive(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

    void stopKeepAlive(std::vector<unsigned long>& ids) override;

    bool tryPin(ICanChannel& channel, uint64_t pin) override;

    void postAuth(std::vector<std::unique_ptr<ICanChannel>>& channels) override;

    std::chrono::milliseconds getRetryDelay() const override;

private:
    CarPlatform _carPlatform;
    uint8_t _ecuId;
};
```

**Поведение:**
- `openChannels()` — вызывает `provider.getAllChannels(ecuId)`
- `preAuth()` — вызывает `UDSProtocolCommonSteps::fallAsleep(channels)`
- `startKeepAlive()` — `UDSProtocolCommonSteps::keepAlive(channel)`
- `stopKeepAlive()` — останавливает периодические сообщения
- `tryPin()` — `UDSProtocolCommonSteps::authorize(channel, canId, pin)`
- `postAuth()` — `UDSProtocolCommonSteps::wakeUp(channels)`
- `getRetryDelay()` — 5000 мс (как в текущем authorize с 5 ретраями)

### 3.5. KWPPinCrackerSteps — реализация для KWP (ТBD)

```cpp
// Flasher/flasher/pin/KWPPinCrackerSteps.hpp
class KWPPinCrackerSteps final : public PinCrackerSteps {
    // openChannels → TP20Session
    // preAuth → KWPProtocolCommonSteps::enterProgrammingSession
    // tryPin → KWPProtocolCommonSteps::authorize
    // postAuth → disconnect
};
```

### 3.6. D2PinCrackerSteps — реализация для D2 (ТBD)

```cpp
// Flasher/flasher/pin/D2PinCrackerSteps.hpp
class D2PinCrackerSteps final : public PinCrackerSteps {
    // openChannels → CAN 29-bit raw
    // preAuth → D2ProtocolCommonSteps::startPBL / loadSBL / startSBL
    // tryPin → запрос seed через D2Messages + VolvoGenerateKey
    // postAuth → D2ProtocolCommonSteps::wakeUp
};
```

### 3.7. Фабрика шагов

```cpp
// Flasher/flasher/pin/PinCrackerStepsFactory.hpp
namespace flasher {

std::unique_ptr<PinCrackerSteps> createPinCrackerSteps(
    CarPlatform platform, uint8_t ecuId);

} // namespace flasher
```

Диспетчеризация:
- `ISO15765` (UDS) → `UDSPinCrackerSteps`
- `TP20` (K-Line) → `KWPPinCrackerSteps`
- `CAN` + D2-ECU → `D2PinCrackerSteps`

### 3.8. PinCrackerStorage — хранилище проверенных ПИН-кодов

#### 3.8.1. Мотивация

- При переборе ПИНов можно прервать процесс и продолжить позже — нужно помнить, где остановились.
- Несколько ЭБУ на одной платформе могут иметь пересекающиеся диапазоны ПИНов — повторная проверка уже отвергнутых кодов не имеет смысла.
- Хранилище позволяет агрегировать результат перебора между сессиями.

#### 3.8.2. Интерфейс

```cpp
// Flasher/flasher/pin/PinCrackerStorage.hpp
#pragma once

#include <cstdint>
#include <memory>

namespace flasher {

class PinCrackerStorage {
public:
    virtual ~PinCrackerStorage() = default;

    // Был ли данный ПИН уже проверен?
    virtual bool isChecked(uint64_t pin) const = 0;

    // Отметить ПИН как проверенный.
    virtual void markChecked(uint64_t pin) = 0;

    // Отметить диапазон как проверенный (оптимизация для batch-скипов).
    virtual void markRange(uint64_t from, uint64_t to) = 0;

    // Сохранить состояние (для persistent-реализаций).
    virtual void flush() {}

    // Создать комбинированное хранилище из нескольких.
    static std::shared_ptr<PinCrackerStorage> combine(
        std::shared_ptr<PinCrackerStorage> primary,
        std::shared_ptr<PinCrackerStorage> secondary);
};

} // namespace flasher
```

#### 3.8.3. Реализации

| Класс | Назначение | Где |
|---|---|---|
| `NullPinCrackerStorage` | No-op, по умолчанию | `PinCrackerStorage.hpp` (inline) |
| `InMemoryPinCrackerStorage` | Хранит проверенные ПИНы в `std::unordered_set` или битовой маске | `Flasher/src/pin/InMemoryPinCrackerStorage.cpp` |
| `FilePinCrackerStorage` | Загружает/сохраняет ПИНы в файл (бинарный или CSV) | `Flasher/src/pin/FilePinCrackerStorage.cpp` |

**NullPinCrackerStorage:**
```cpp
class NullPinCrackerStorage final : public PinCrackerStorage {
public:
    bool isChecked(uint64_t) const override { return false; }
    void markChecked(uint64_t) override {}
    void markRange(uint64_t, uint64_t) override {}
};
```

**InMemoryPinCrackerStorage:**
```cpp
class InMemoryPinCrackerStorage final : public PinCrackerStorage {
public:
    bool isChecked(uint64_t pin) const override;
    void markChecked(uint64_t pin) override;
    void markRange(uint64_t from, uint64_t to) override;
    // Доступ к накопленному множеству для экспорта
    const auto& checkedPins() const { return _checked; }
private:
    std::unordered_set<uint64_t> _checked;
};
```

**FilePinCrackerStorage:**
```cpp
class FilePinCrackerStorage final : public PinCrackerStorage {
public:
    explicit FilePinCrackerStorage(std::string path);
    // Загружает из файла при создании, сохраняет при flush()/деструкторе
    bool isChecked(uint64_t pin) const override;
    void markChecked(uint64_t pin) override;
    void markRange(uint64_t from, uint64_t to) override;
    void flush() override;
};
```

Формат файла — бинарная карта битов (0x1000000 бит = 2 MB для всего 24-битного пространства) либо простой текстовый список найденных диапазонов. Выбор формата — на этапе реализации.

#### 3.8.4. Интеграция в PinCracker

**Конструктор** принимает опциональное хранилище (по умолчанию `NullPinCrackerStorage`):

```cpp
class PinCracker {
public:
    PinCracker(j2534::J2534& j2534, CarPlatform carPlatform,
               std::unique_ptr<PinCrackerSteps> steps,
               Direction direction, uint64_t startPin,
               std::function<void(State, uint64_t)> stateCallback,
               std::shared_ptr<PinCrackerStorage> storage = {});
    // ...
};
```

**Изменение в `run()`:** перед каждой попыткой проверять хранилище; после попытки (успех/неуспех) отмечать ПИН как проверенный.

```diff
 while pin <= endPin && pin >= endPin && !_stop:
+    // Пропустить уже проверенные ПИНы
+    while !_stop && _storage && _storage->isChecked(pin):
+        pin += step
+
     setState(Work, pin)
     if _steps->tryPin(*channels[0], pin):
         _foundPin = pin
+        _storage->markChecked(pin)     // отметить найденный
         break
+    _storage->markChecked(pin)         // отметить неудачный
     pin += step
     sleep(_steps->getRetryDelay())

+_storage->flush()                      // сохранить состояние
```

**Требования к `FilePinCrackerStorage.flush()`:**
- `flush()` вызывается после завершения цикла (как при успехе, так и при ошибке)
- Исключения в `flush()` не должны приводить к потере найденного ПИНа
- Для атомарности: запись во временный файл → переименование

#### 3.8.5. Совместное использование

Несколько `PinCracker` могут использовать общее хранилище через `shared_ptr`:

```
auto storage = std::make_shared<FilePinCrackerStorage>("pins.bin");

// Первый ЭБУ: 0x000000 → 0x100000
PinCracker cracker1{ j2534, platform, makeUDSSteps(),
    Direction::Up, 0x000000, callback, storage };
cracker1.start();

// Второй ЭБУ (та же платформа): продолжит с 0x100000
PinCracker cracker2{ j2534, platform, makeUDSSteps(),
    Direction::Up, 0x100000, callback, storage };
cracker2.start();
```

Файловое хранилище позволяет также возобновить перебор после перезапуска программы — `FilePinCrackerStorage` загружает проверенные ПИНы из файла при создании.

## 4. Изменяемые файлы

### 4.1. Новые

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/flasher/pin/PinCrackerSteps.hpp` | Интерфейс шагов |
| 2 | `Flasher/flasher/pin/PinCracker.hpp` | Базовый класс с алгоритмом |
| 3 | `Flasher/src/pin/PinCracker.cpp` | Реализация run() |
| 4 | `Flasher/flasher/pin/UDSPinCrackerSteps.hpp` | UDS-реализация |
| 5 | `Flasher/src/pin/UDSPinCrackerSteps.cpp` | Реализация |
| 6 | `Flasher/flasher/pin/PinCrackerStorage.hpp` | Интерфейс хранилища ПИНов |
| 7 | `Flasher/src/pin/PinCrackerStorage.cpp` | NullPinCrackerStorage + combine |
| 8 | `Flasher/src/pin/FilePinCrackerStorage.cpp` | Persistent-хранилище (файл) |
| 9 | `Flasher/flasher/pin/PinCrackerStepsFactory.hpp` | Фабрика шагов |
| 10 | `Flasher/src/pin/PinCrackerStepsFactory.cpp` | Реализация фабрики |

### 4.2. Изменяемые

| № | Файл | Изменение |
|---|---|---|
| 8 | `Common/common/protocols/UDSPinFinder.hpp` | Удалить (заменить на PinCracker + UDSPinCrackerSteps) |
| 9 | `Common/src/protocols/UDSPinFinder.cpp` | Удалить (HFSM2 больше не нужен) |
| 10 | `VolvoFlasher/src/VolvoFlasher.cpp` | `findPin2()` → `PinCracker` + `UDSPinCrackerSteps` |
| 11 | `Flasher/flasher/pin/PinCracker.hpp` | Переписать: заглушка → полный класс с run() |
| 12 | `Flasher/src/pin/PinCracker.cpp` | Переписать: заглушка → реализация алгоритма |

### 4.3. Не изменяются

- `UDSProtocolCommonSteps` — остаётся как есть
- `D2ProtocolCommonSteps` — остаётся как есть
- `KWPProtocolCommonSteps` — остаётся как есть
- `J2534ChannelProvider` — остаётся как есть

## 5. Порядок реализации

### Шаг 1: Создать базовые типы (без изменения существующего кода)

1. `Flasher/flasher/pin/PinCrackerSteps.hpp` — интерфейс
2. `Flasher/flasher/pin/PinCracker.hpp` — класс с run()
3. `Flasher/src/pin/PinCracker.cpp` — реализация алгоритма
4. `Flasher/flasher/pin/PinCrackerStorage.hpp` — интерфейс хранилища + `NullPinCrackerStorage`
5. `Flasher/src/pin/PinCrackerStorage.cpp` — вспомогательные реализации (`InMemoryPinCrackerStorage`, `combine`)
6. `Flasher/src/pin/FilePinCrackerStorage.cpp` — сохранение/загрузка из файла

### Шаг 2: UDS-реализация

7. `Flasher/flasher/pin/UDSPinCrackerSteps.hpp` — объявление
8. `Flasher/src/pin/UDSPinCrackerSteps.cpp` — делегирует UDSProtocolCommonSteps
9. `Flasher/flasher/pin/PinCrackerStepsFactory.hpp` — фабрика
10. `Flasher/src/pin/PinCrackerStepsFactory.cpp` — пока только UDS

### Шаг 3: Перевести VolvoFlasher на PinCracker + UDSPinCrackerSteps

11. `VolvoFlasher.cpp`: `findPin2()` → создаёт `PinCracker` с `UDSPinCrackerSteps`
12. `findPin()` — либо удалить, либо перевести на тот же механизм

### Шаг 4: Удалить старый код

13. Удалить `UDSPinFinder.hpp` / `.cpp` из `Common/common/protocols/`
14. Убрать мертвый код из CMakeLists.txt (ссылки на UDSPinFinder)

### Шаг 5 (опционально): D2/KWP реализации

15. `Flasher/src/pin/KWPPinCrackerSteps.cpp`
16. `Flasher/src/pin/D2PinCrackerSteps.cpp`
17. Обновить фабрику

## 6. Критерии готовности

1. `PinCrackerSteps` — интерфейс с методами для всех платформо-зависимых шагов
2. `PinCracker::run()` — содержит общий алгоритм:
   - открытие каналов
   - preAuth (один раз)
   - keepAlive (на время цикла)
   - цикл перебора с tryPin
   - остановка keepAlive
   - postAuth (один раз)
3. `UDSPinCrackerSteps` — реализует все шаги через существующие `UDSProtocolCommonSteps`
4. `UDSPinCrackerSteps::tryPin()` — вызывает `UDSProtocolCommonSteps::authorize()`
5. `VolvoFlasher pin` использует `PinCracker` + `UDSPinCrackerSteps` вместо `UDSPinFinder`
6. Скорость перебора не ниже текущей (колбэки состояния + pin/sec)
7. `PinCrackerStorage` — интерфейс с `isChecked()`, `markChecked()`, `markRange()`
8. `NullPinCrackerStorage` — реализация по умолчанию (no-op, без потери производительности)
9. `InMemoryPinCrackerStorage` — хранит проверенные ПИНы в памяти (опционально — `FilePinCrackerStorage` для persistence)
10. `PinCracker::run()` пропускает ПИНы, уже отмеченные в хранилище, и отмечает каждый проверенный
11. Возможность добавить `D2PinCrackerSteps` / `KWPPinCrackerSteps` без изменения `PinCracker`
12. Старый `UDSPinFinder` удалён; заглушка `PinCracker.hpp` переписана
13. Сборка: 0 ошибок
