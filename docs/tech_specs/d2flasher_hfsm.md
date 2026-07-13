# ТЗ: Перевод D2FlasherBase на конечные автоматы (hFSM2)

## 1. Цель

Заменить линейный try/catch-оркестратор в `D2FlasherBase::startImpl()` на конечный автомат с библиотекой hFSM2 по аналогии с `UDSFlasher` и `KWPFlasher`. Выделить FSM в отдельную единицу трансляции для переиспользования в `D2ReaderChecksum`.

## 2. Текущая архитектура (было)

```
D2FlasherBase::startImpl()
  └─ последовательный код:
       wakeUp → fallAsleep → startPBL → [loadSBL → startSBL] → eraseStep → writeStep → wakeUp + setDIMTime
  try { ... } catch(...) { wakeUp + setDIMTime + Error }
```

**Участники:**

| Класс | Роль |
|---|---|
| `D2FlasherBase` | Оркестратор. `startImpl()` — линейная последовательность + try/catch |
| `D2Flasher` | Наследует `D2FlasherBase`. Переопределяет `eraseStep()` (вызов `eraseFlash`) и `writeStep()` (вызов `transferData`) |
| `D2Reader` | Наследует `D2FlasherBase`. Переопределяет `eraseStep()` (пусто) и `writeStep()` (побайтовое чтение) |

**Проблемы:**
- try/catch — единственный механизм обработки ошибок
- Невозможно добавить условные или повторяющиеся шаги
- wakeUp и setDIMTime дублируются в нормальном пути и в catch
- Нет детального контроля состояний

## 3. Целевая архитектура (стало)

### 3.1. Общая архитектура

```
D2FlasherBase::startImpl()
  └─ создаёт D2FlasherImpl (контекст)
  └─ impl.run()  ← запускает FSM, ждёт Done/Error

D2ReaderChecksum::startImpl()
  └─ создаёт D2FlasherImpl (контекст, no-op erase)
  └─ impl.run()  ← тот же FSM

D2FlasherImpl::run()
  └─ FSM::Instance fsm{ *this }
  └─ while(!_isDone) { fsm.update() }
```

**Ключевое изменение:** `D2FlasherImpl` — отдельный файл (`Flasher/src/D2FlasherImpl.{hpp,cpp}`), используемый и `D2FlasherBase`, и `D2ReaderChecksum`.

### 3.2. `D2FlasherImpl` — контекст автомата

```cpp
// Flasher/src/D2FlasherImpl.hpp
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

    void run();  // запускает FSM, ждёт Done/Error

    void setMaximumFlashProgressValue(size_t value);
    size_t getMaximumProgress() const;

    bool isFailed() const;
    bool isSBLRequired() const;

    void wakeUpChannels();
    void fallAsleep();
    void startPBL();
    void loadSBL();
    void startSBL();
    void eraseFlash();
    void writeFlash();
    void wakeUpFinish();
    void setDIMTime();
    void done();
    void error();

private:
    void setFailed(const std::string& msg);
    // ... _channels, _carPlatform, _ecuId, _bootloader, _isFailed, _isDone, ...
};
```

`eraseCallback` и `writeCallback` — лямбды из `startImpl()`. Для `D2FlasherBase` это `eraseStep`/`writeStep`, для `D2ReaderChecksum` — no-op и побайтовое чтение.

### 3.3. Состояния автомата

```hfsm2
PeerRoot<
  Composite<              // Основной план (StartWork)
    struct StartWork,
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

**StartWork** — настраивает план:
```
plan.change<FallAsleep, StartPBL>();
plan.change<StartPBL, LoadSBL>();
plan.change<LoadSBL, StartSBL>();
plan.change<StartSBL, EraseFlash>();
plan.change<EraseFlash, WriteFlash>();
```

**WakeUpChannels** не выделен в отдельное состояние — пробуждение выполняется внутри `run()` перед запуском FSM (см. D2FlasherImpl).

`LoadSBL` и `StartSBL` — no-op если `!isSBLRequired()` (пустой `enter()` → BaseState::update → succeed).

**Finish** — по `planSucceeded`/`planFailed` переходит в `WakeUpFinish → SetDIMTime → Done/Error`.

### 3.4. `startImpl()` — обновлённый (D2FlasherBase)

```cpp
void D2FlasherBase::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) {
    setCurrentProgress(0);
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), _config.bootloader,
        [this](FlasherState s) { setCurrentState(s); },
        [this](size_t p) { incCurrentProgress(p); },
        [this](ICanChannel& ch, uint8_t id) { eraseStep(ch, id); },
        [this](ICanChannel& ch, uint8_t id) { writeStep(ch, id); }
    );

    impl.setMaximumFlashProgressValue(getMaximumFlashProgress());
    setMaximumProgress(impl.getMaximumProgress());
    impl.run();   // ← вместо ручного цикла fsm.update()
}
```

### 3.5. `startImpl()` — D2ReaderChecksum (использует тот же FSM)

```cpp
void D2ReaderChecksum::startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) {
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), common::VBF(),
        [this](FlasherState s) { setCurrentState(s); },
        [this](size_t p) { incCurrentProgress(p); },
        [](ICanChannel&, uint8_t) {},  // erase — no-op
        [this](ICanChannel& ch, uint8_t id) { /* побайтовое чтение через checksum */ }
    );

    size_t totalSize = /* сумма _ranges.size */;
    impl.setMaximumFlashProgressValue(totalSize);
    setMaximumProgress(impl.getMaximumProgress());
    impl.run();
}
```

### 3.6. Расчёт прогресса

`getMaximumProgress()`:
```
stepCost * 4 + getProgressFromVBF(_bootloader) + getMaximumFlashProgressValue()
```
Где `stepCost = 100` (wakeUp + fallAsleep + startPBL + setDIMTime).

## 4. Изменяемые файлы

### 4.1. Новые

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/src/D2FlasherImpl.hpp` | Объявление `D2FlasherImpl` |
| 2 | `Flasher/src/D2FlasherImpl.cpp` | Реализация `D2FlasherImpl` + hFSM2 machine + 10 состояний + `run()` |

### 4.2. Изменяемые

| № | Файл | Изменение |
|---|---|---|
| 3 | `Flasher/src/D2FlasherBase.cpp` | Удалён `D2FlasherImpl`, hFSM2, state-структуры. `startImpl()` делегирует `impl.run()` |
| 4 | `Flasher/flasher/D2FlasherBase.hpp` | Удалён мёртвый код, убран `#include "FlasherCallback.hpp"` |

### 4.3. Переиспользующие

| № | Файл | Изменение |
|---|---|---|
| 5 | `Flasher/src/D2ReaderChecksum.cpp` | Создаёт `D2FlasherImpl` с no-op erase, вызывает `impl.run()` |
| 6 | `Flasher/src/D2ReaderME7.cpp` | Создаёт `D2FlasherImpl` с no-op erase и write-читателем (побайтово через `createReadOffsetMsg2`) |
| 7 | `Flasher/src/D2ReaderDEM.cpp` | Создаёт `D2FlasherImpl` с no-op erase и write-читателем (побайтово через `createReadOffsetMsgDEM`) |

## 5. Тестирование

Тесты в `Flasher/test/` (`D2FlasherTest.cpp`, 11 кейсов) не изменились — hFSM2 автомат остался тем же, только перемещён в отдельный .cpp.

## 6. Статус реализации

| № | Шаг | Статус |
|---|---|---|
| 1 | Написать `D2FlasherImpl` + hFSM2 + state-структуры | ✓ |
| 2 | Выделить в отдельные `D2FlasherImpl.hpp` / `.cpp` | ✓ |
| 3 | Переписать `D2FlasherBase::startImpl()` на `impl.run()` | ✓ |
| 4 | Убрать мёртвый код из hpp | ✓ |
| 5 | Переиспользовать `D2FlasherImpl` в `D2ReaderChecksum`, `D2ReaderME7`, `D2ReaderDEM` | ✓ |
| 6 | Создать `Flasher/test/` с тестами | ✓ |
| 7 | Собрать: 0 ошибок | ✓ |
| 8 | Запустить тесты: 11/11 passed | ✓ |

## 7. Критерии готовности

1. ✓ `D2FlasherBase::startImpl()` не содержит линейного кода с try/catch — через hFSM2
2. ✓ `FlasherState` обновляется в каждом шаге
3. ✓ При ошибке: `Finish → WakeUpFinish → SetDIMTime → Error`
4. ✓ При успехе: `Finish → WakeUpFinish → SetDIMTime → Done`
5. ✓ Прогресс обновляется в wakeUp, fallAsleep, startPBL, loadSBL, writeFlash, setDIMTime
6. ✓ `D2Flasher`, `D2ReaderChecksum`, `D2ReaderME7`, `D2ReaderDEM` работают через один hFSM2
7. ✓ Сборка: 0 ошибок
8. ✓ Тест `FlasherTests.exe` проходит 11/11 кейсов
