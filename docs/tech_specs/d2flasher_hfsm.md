# ТЗ: Перевод D2FlasherBase на конечные автоматы (hFSM2)

## 1. Цель

Заменить линейный try/catch-оркестратор в `D2FlasherBase::startImpl()` на конечный автомат с библиотекой hFSM2 по аналогии с `UDSFlasher` и `KWPFlasher`.

## 2. Текущая архитектура

```
D2FlasherBase::startImpl()
  └─ последовательный код:
       wakeUp → fallAsleep → startPBL → [loadSBL → startSBL] → eraseStep → writeStep → wakeUp + setDIMTime
  try { ... } catch(...) { wakeUp + setDIMTime + Error }
```

**Текущие участники:**

| Класс | Роль |
|---|---|
| `D2FlasherBase` | Оркестратор. `startImpl()` — линейная последовательность + try/catch |
| `D2Flasher` | Наследует `D2FlasherBase`. Переопределяет `eraseStep()` (вызов `eraseFlash`) и `writeStep()` (вызов `transferData`) |
| `D2Reader` | Наследует `D2FlasherBase`. Переопределяет `eraseStep()` (пусто) и `writeStep()` (побайтовое чтение) |

**Проблемы:**
- try/catch — единственный механизм обработки ошибок; если любой шаг падает, весь блок прерывается
- Невозможно добавить условные или повторяющиеся шаги без вложенности if/for
- `D2ProtocolCommonSteps::wakeUp` и `setDIMTime` дублируются в нормальном пути и в catch
- Нет детального контроля состояний (только `setCurrentState()`)

## 3. Целевая архитектура

```
D2FlasherBase::startImpl()
  └─ создаёт D2FlasherImpl (контекст)
  └─ FSM::Instance fsm{ impl }
  └─ цикл: while(not Done/Error) { fsm.update() }

hFSM2-автомат:
  StartWork (composite, строит план)
    ├─ WakeUpChannels
    ├─ FallAsleep
    ├─ StartPBL
    ├─ LoadSBL          ← no-op если !isSBLRequired()
    ├─ StartSBL         ← no-op если !isSBLRequired()
    ├─ EraseFlash       → D2FlasherImpl::eraseFlash()
    │                    → вызывает _flasher.eraseStep(channel, ecuId)
    ├─ WriteFlash       → D2FlasherImpl::writeFlash()
    │                    → вызывает _flasher.writeStep(channel, ecuId)
    └─ Finish (composite)
         ├─ WakeUpChannels
         ├─ SetDIMTime
         ├─ Done / Error
```

### 3.1. `D2FlasherImpl` — контекст автомата

```cpp
class D2FlasherImpl {
public:
    D2FlasherImpl(
        const std::vector<std::unique_ptr<ICanChannel>>& channels,
        common::CarPlatform carPlatform,
        uint8_t ecuId,
        const common::VBF& bootloader,
        const std::function<void(FlasherState)>& stateUpdater,
        const std::function<void(size_t)>& progressUpdater,
        const std::function<void(ICanChannel&, uint8_t)>& eraseCallback,
        const std::function<void(ICanChannel&, uint8_t)>& writeCallback
    );

    void setMaximumFlashProgressValue(size_t value);
    size_t getMaximumProgress() const;

    void wakeUpChannels();
    void fallAsleep();
    void startPBL();
    void loadSBL();
    void startSBL();
    void eraseFlash();
    void writeFlash();
    void setDIMTime();
    void done();
    void error();

    bool isFailed() const;
    bool isSBLRequired() const;

private:
    void setFailed(const std::string& msg);

    const std::vector<std::unique_ptr<ICanChannel>>& _channels;
    common::CarPlatform _carPlatform;
    uint8_t _ecuId;
    const common::VBF& _bootloader;
    bool _isFailed = false;
    std::string _errorMessage;
    size_t _maximumFlashProgress = 0;
    const std::function<void(FlasherState)> _stateUpdater;
    const std::function<void(size_t)> _progressUpdater;
    const std::function<void(ICanChannel&, uint8_t)> _eraseCallback;
    const std::function<void(ICanChannel&, uint8_t)> _writeCallback;
};
```

`eraseCallback` и `writeCallback` — обёртки вокруг виртуальных `eraseStep()`/`writeStep()` родительского класса, переданные через лямбды из `startImpl()`. Поскольку `startImpl()` вызывается на уже сконструированном объекте (`D2Flasher`/`D2Reader`), виртуальные методы резолвятся корректно.

### 3.2. Состояния автомата

```hfsm2
PeerRoot<
  Composite<              // Основной план (StartWork)
    struct StartWork,
    struct WakeUpChannels,
    struct FallAsleep,
    struct StartPBL,
    struct LoadSBL,       // SBL-блок (опционально)
    struct StartSBL,
    struct EraseFlash,
    struct WriteFlash
  >,
  Composite<              // Завершение (Finish)
    struct Finish,
    struct WakeUpFinish,
    struct SetDIMTime,
    struct Done,
    struct Error
  >
>
```

**StartWork** — настраивает план (SBL-шаги всегда в цепи, но no-op если не нужны):
```
plan.change<WakeUpChannels, FallAsleep>();
plan.change<FallAsleep, StartPBL>();
plan.change<StartPBL, LoadSBL>();
plan.change<LoadSBL, StartSBL>();
plan.change<StartSBL, EraseFlash>();
plan.change<EraseFlash, WriteFlash>();
```

`LoadSBL` и `StartSBL` внутри проверяют `isSBLRequired()`:
```cpp
void enter(PlanControl& control) {
    if (!control.context().isSBLRequired()) {
        return;  // ничего не делать → BaseState::update() вернёт succeed
    }
    // иначе: загрузить/запустить SBL
}
```

Без SBL оба стейта — пустые `enter()`. `BaseState::update()` вызывает `control.succeed()`, и автомат переходит к следующему шагу.

**Каждый стейт** — struct, наследующий `BaseState`, с `enter()` вызывающий соответствующий метод `D2FlasherImpl`.

**BaseState** — общий предок:
```cpp
struct BaseState : public FSM::State {
    void update(FullControl& control) {
        if (!control.context().isFailed())
            control.succeed();
        else
            control.fail();
    }
};
```

**Finish** — по `planSucceeded`/`planFailed` переходит в `WakeUpFinish → SetDIMTime → Done/Error`.

### 3.3. Сравнение с UDSFlasher

| Аспект | UDSFlasher | D2Flasher (новый) |
|---|---|---|
| Параметры в impl | `FlasherParameters&` + `UDSFlasherParameters&` + `canId` | `carPlatform` + `ecuId` + `eraseCallback` + `writeCallback` |
| Bootloader | `sblProvider->getSBL()` внутри impl | `bootloader` передаётся извне (предварительно загружен) |
| Авторизация | Есть (SecurityAccess) | Нет |
| KeepAlive | Есть (периодическое сообщение) | Нет |
| SBL условие | `sblProvider != nullptr` + `!bootloader.chunks.empty()` | `isSBLRequired()` через `_bootloader.chunks.empty()` |
| Обработка ошибок | `setFailed()`, `isFailed()` в `BaseState::update()` | Аналогично |
| WakeUp после ошибки | `Finish → WakeUp → Error` | `Finish → WakeUpChannels → SetDIMTime → Error` |

### 3.4. `startImpl()` — обновлённый

```cpp
void D2FlasherBase::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) {
    const auto carPlatform{ getFlasherParameters().carPlatform };
    const auto ecuId{ getFlasherParameters().ecuId };
    const auto additionalData{ getFlasherParameters().additionalData };
    const auto bootloader = getFlasherParameters().sblProvider->getSBL(carPlatform, ecuId, additionalData);
    if (isBootloaderRequired() && bootloader.chunks.empty()) {
        setCurrentState(FlasherState::Error);
        return;
    }

    D2FlasherImpl impl(channels, carPlatform, static_cast<uint8_t>(ecuId), bootloader,
        [this](FlasherState s) { setCurrentState(s); },
        [this](size_t p) { incCurrentProgress(p); },
        [this](ICanChannel& ch, uint8_t id) { eraseStep(ch, id); },
        [this](ICanChannel& ch, uint8_t id) { writeStep(ch, id); }
    );

    impl.setMaximumFlashProgressValue(getMaximumFlashProgress());
    setMaximumProgress(impl.getMaximumProgress());

    FSM::Instance fsm{ impl };

    while (getCurrentState() != FlasherState::Done &&
           getCurrentState() != FlasherState::Error) {
        fsm.update();
    }
}
```

### 3.5. Расчёт прогресса

`getMaximumProgress()`:
```
100 (wakeUp) + 100 (fallAsleep) + 100 (startPBL) + getProgressFromVBF(bootloader) + getMaximumFlashProgress() + 100 (setDIMTime)
```

Где `getMaximumFlashProgress()` — виртуальный (определён в D2Flasher/D2Reader).

Каждый шаг обновляет прогресс через `_progressUpdater`.

## 4. Изменяемые файлы

### 4.1. Новые (1 файл)

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/src/D2FlasherImpl.hpp` | Объявление `D2FlasherImpl` context-класса. Хранится в `src/` как деталь реализации |

### 4.2. Изменяемые (2 файла)

| № | Файл | Изменение |
|---|---|---|
| 1 | `Flasher/src/D2FlasherBase.cpp` | Полная перезапись. Добавить `D2FlasherImpl`, hFSM2 machine, state structs. Заменить try/catch на цикл fsm.update() |
| 2 | `Flasher/flasher/D2FlasherBase.hpp` | Удалён мёртвый код (`canWakeUp()`, `cleanErrors()`). Удалён неиспользуемый `#include "FlasherCallback.hpp"` |

### 4.3. Без изменений

`D2Flasher.cpp`, `D2Reader.cpp`, `D2Flasher.hpp`, `D2Reader.hpp` — без изменений. eraseStep/writeStep остаются виртуальными, hFSM2 вызывает их через колбэки.

## 5. Тестирование

В проекте есть зависимость `boost/1.86.0` — доступен `Boost.Test`. Используется вместо кастомных макросов (RegistryTest — legacy, его подход не тиражируется).

### 5.1. Структура

```
Flasher/test/
├── CMakeLists.txt            # Test runner, линкует Boost::unit_test_framework
├── D2FlasherTest.cpp        # 11 Boost.Test кейсов
├── MockICanChannel.hpp      # Mock-канал + MockChannelWrapper
```

Директория `Flasher/test/` рассчитана на добавление тестов для всех компонентов Flasher.

Подключается в `Flasher/CMakeLists.txt`:
```cmake
option(BUILD_FLASHER_TESTS "Build Flasher unit tests" OFF)
if(BUILD_FLASHER_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

`Flasher/test/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(FlasherTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS unit_test_framework)
find_package(Easyloggingpp REQUIRED)

add_executable(FlasherTests D2FlasherTest.cpp)
target_link_libraries(FlasherTests Flasher Common Boost::unit_test_framework easyloggingpp::easyloggingpp)
target_include_directories(FlasherTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_compile_definitions(FlasherTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

add_test(NAME D2Flasher COMMAND FlasherTests)
```

### 5.2. Mock-канал

`Flasher/test/MockICanChannel.hpp` содержит два класса:
- `MockICanChannel` — реализует `ICanChannel`, считает вызовы, умеет эмулировать ошибки
- `MockChannelWrapper` — обёртка `MockICanChannel&` в `unique_ptr<ICanChannel>` (для вставки в `vector<unique_ptr<ICanChannel>>`)

### 5.3. Реализованные тесты

| № | Тест | Что проверяет |
|---|---|---|
| 1 | `SBLDetection` | `isSBLRequired()` с пустым и полным бутлоадером |
| 2 | `WakeUpSendsOnAllChannels` | wakeUp вызывает send на всех каналах |
| 3 | `FallAsleepFailure` | `failOnPeriodic = true` → `isFailed()` |
| 4 | `FallAsleepSuccess` | успешный fallAsleep |
| 5 | `EraseFlashCallsCallback` | erase callback вызывается |
| 6 | `EraseFlashThrow` | исключение в erase → `isFailed()` |
| 7 | `WriteFlashCallsCallback` | write callback вызывается |
| 8 | `WriteFlashThrow` | исключение в write → `isFailed()` |
| 9 | `DoneState` | `done()` устанавливает `FlasherState::Done` |
| 10 | `ErrorState` | `error()` устанавливает `FlasherState::Error` |
| 11 | `ProgressUpdates` | `getMaximumProgress()` > 0 |

### 5.4. Сборка и запуск

```powershell
# Конфигурация
cmake -B build -DBUILD_FLASHER_TESTS=ON

# Сборка
cmake --build build --config Release

# Запуск через CTest (из build/)
ctest -C Release --output-on-failure -R D2Flasher

# Или напрямую
.\build\Flasher\test\Release\FlasherTests.exe
```

## 6. Статус реализации

| № | Шаг | Статус |
|---|---|---|
| 1 | Написать `D2FlasherImpl` в `D2FlasherBase.cpp` | ✓ |
| 2 | Определить hFSM2 machine и state-структуры | ✓ |
| 3 | Переписать `startImpl()` | ✓ |
| 4 | Убрать мёртвый код из hpp | ✓ |
| 5 | Создать `Flasher/src/D2FlasherImpl.hpp` (вынесен в src/ как деталь реализации) | ✓ |
| 6 | Создать `Flasher/test/` с тестами | ✓ |
| 7 | Собрать: 0 ошибок | ✓ |
| 8 | Запустить тесты: 11/11 passed | ✓ |

## 7. Критерии готовности

1. `D2FlasherBase::startImpl()` не содержит линейного кода с try/catch — весь поток управления через hFSM2
2. `FlasherState` обновляется корректно в каждом шаге
3. При ошибке автомат переходит в `Finish → WakeUpChannels → SetDIMTime → Error`
4. При успехе — `Finish → WakeUpChannels → SetDIMTime → Done`
5. Прогресс обновляется в `wakeUp`, `fallAsleep`, `startPBL`, `loadSBL`, `writeFlash`, `setDIMTime`
6. `D2Flasher` и `D2Reader` работают через hFSM2 без изменения их кода
7. Сборка: 0 ошибок, 0 предупреждений ✓
8. Тест `FlasherTests.exe` проходит 11/11 кейсов ✓
