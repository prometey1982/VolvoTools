# ТЗ: Выделение общего D2-кода (FSM + bootloader sequence)

## 1. Цель

Устранить дублирование между `D2FlasherBase` и `D2ReaderChecksum`: оба выполняют одинаковую последовательность D2-шагов (wakeUp → fallAsleep → startPBL → [loadSBL → startSBL] → операция → wakeUp), но каждый реализует её по-своему (один через HFSM2, другой — прямыми вызовами).

## 2. Текущая архитектура (было)

### 2.1. Дублирование

| Шаг | D2FlasherBase | D2ReaderChecksum |
|---|---|---|
| wakeUp | `D2FlasherImpl::wakeUpChannels()` | `D2ProtocolCommonSteps::wakeUp(channels)` |
| fallAsleep | `D2FlasherImpl::fallAsleep()` | `D2ProtocolCommonSteps::fallAsleep(channels)` |
| startPBL | `D2FlasherImpl::startPBL()` | `D2ProtocolCommonSteps::startPBL(channel, ecuId)` |
| loadSBL | `D2FlasherImpl::loadSBL()` | `D2ProtocolCommonSteps::transferData(...)` |
| startSBL | `D2FlasherImpl::startSBL()` | `D2ProtocolCommonSteps::startRoutine(...)` |
| операция | HFSM2 → `eraseCallback` + `writeCallback` | побайтовое чтение |
| wakeUp | `D2FlasherImpl::wakeUpFinish()` | `D2ProtocolCommonSteps::wakeUp(channels)` |

**D2FlasherBase** использует HFSM2-автомат (11 состояний). Автомат и `D2FlasherImpl` находятся в одном `.cpp` (`D2FlasherBase.cpp`, ~370 строк).

**D2ReaderChecksum** дублирует последовательность прямыми вызовами `D2ProtocolCommonSteps`, без HFSM2, без обработки ошибок, с ручным `setCurrentState()`.

### 2.2. Файлы

| Файл | Содержит | Размер |
|---|---|---|
| `Flasher/src/D2FlasherBase.cpp` | `D2FlasherImpl` + HFSM2 states + FSM definition + `D2FlasherBase::startImpl()` | 372 строки |
| `Flasher/src/D2FlasherImpl.hpp` | Только объявление `D2FlasherImpl` | 65 строк |
| `Flasher/src/D2ReaderChecksum.cpp` | Прямые вызовы D2ProtocolCommonSteps | ~100 строк |

## 3. Целевая архитектура (стало)

### 3.1. Выделение D2FlasherImpl в отдельную единицу трансляции

**Новый файл:** `Flasher/src/D2FlasherImpl.cpp`

Содержит:
- Полную реализацию `D2FlasherImpl`
- HFSM2 state-структуры (11 состояний)
- HFSM2 machine definition
- `D2FlasherImpl::run()` — создаёт `FSM::Instance` и цикл `while (!_isDone) { fsm.update(); }`

### 3.2. D2FlasherBase — делегирует D2FlasherImpl

```cpp
void D2FlasherBase::startImpl(channels)
{
    setCurrentProgress(0);
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), _config.bootloader,
        [this](FlasherState s) { setCurrentState(s); },
        [this](size_t p) { incCurrentProgress(p); },
        [this](ICanChannel& ch, uint8_t id) { eraseStep(ch, id); },
        [this](ICanChannel& ch, uint8_t id) { writeStep(ch, id); });
    impl.setMaximumFlashProgressValue(getMaximumFlashProgress());
    setMaximumProgress(impl.getMaximumProgress());
    impl.run();
}
```

### 3.3. D2ReaderChecksum — тоже использует D2FlasherImpl

```cpp
void D2ReaderChecksum::startImpl(channels)
{
    D2FlasherImpl impl(channels, _carPlatform, static_cast<uint8_t>(_ecuId), common::VBF{},
        [this](FlasherState s) { setCurrentState(s); },
        [this](size_t p) { incCurrentProgress(p); },
        [](ICanChannel&, uint8_t) {},  // erase — no-op
        [this](ICanChannel& channel, uint8_t ecuId) {
            for (size_t r = 0; r < _ranges.size(); ++r) {
                auto& buffer = _buffers[r];
                buffer.clear();
                const auto& range = _ranges[r];
                for (uint32_t i = 0; i < range.size; ++i) {
                    const auto currentPos = range.startAddr + i;
                    common::D2ProtocolCommonSteps::jumpTo(channel, ecuId, currentPos);
                    // ... checksum read -> buffer.push_back(data[2])
                }
            }
        });
    size_t totalSize = 0;
    for (const auto& range : _ranges) totalSize += range.size;
    impl.setMaximumFlashProgressValue(totalSize);
    setMaximumProgress(impl.getMaximumProgress());
    impl.run();
}
```

### 3.4. Преимущества

| Аспект | Было | Стало |
|---|---|---|
| Копий bootloader sequence | 2 (D2FlasherBase + D2ReaderChecksum) | 1 (D2FlasherImpl) |
| HFSM2 | Внутри D2FlasherBase.cpp | В отдельном D2FlasherImpl.cpp |
| Обработка ошибок D2ReaderChecksum | Нет (прямые вызовы) | HFSM2 planFailed |
| setCurrentState в D2ReaderChecksum | Ручное управление | Через stateUpdater колбэк |

## 4. Изменяемые файлы

| № | Файл | Изменение |
|---|---|---|
| 1 | `Flasher/src/D2FlasherImpl.cpp` | **Новый** — D2FlasherImpl + HFSM2 + FSM |
| 2 | `Flasher/src/D2FlasherImpl.hpp` | Добавлен метод `run()`, шаги публичны |
| 3 | `Flasher/src/D2FlasherBase.cpp` | D2FlasherImpl + HFSM2 удалены. `startImpl()` → `impl.run()` |
| 4 | `Flasher/src/D2ReaderChecksum.cpp` | Вместо прямых вызовов → `D2FlasherImpl::run()` |

## 5. Порядок реализации

### Шаг 1: Создать D2FlasherImpl.cpp

1. Перенести `D2FlasherImpl` + HFSM2 + FSM из `D2FlasherBase.cpp` в `D2FlasherImpl.cpp`
2. Добавить `D2FlasherImpl::run()`
3. Собрать

### Шаг 2: Упростить D2FlasherBase.cpp

4. Удалить D2FlasherImpl, HFSM2 states, FSM definition
5. `impl.run()` вместо `FSM::Instance fsm{ impl }; while(...) { fsm.update(); }`
6. Собрать

### Шаг 3: Упростить D2ReaderChecksum.cpp

7. Заменить прямые вызовы на `D2FlasherImpl::run()`
8. Собрать

## 6. Критерии готовности

1. ✓ `D2FlasherImpl` — отдельный .cpp + .hpp с HFSM2
2. ✓ `D2FlasherImpl::run()` запускает FSM и ждёт Done/Error
3. ✓ `D2FlasherBase::startImpl()` делегирует `impl.run()`
4. ✓ `D2ReaderChecksum::startImpl()` использует `D2FlasherImpl` (no-op erase, write-читатель)
5. ✓ D2ReaderChecksum получает обработку ошибок через HFSM2
6. ✓ D2ReaderChecksum не содержит прямых вызовов wakeUp/fallAsleep/startPBL/loadSBL/startSBL
7. ✓ Сборка: 0 ошибок
8. ✓ Функциональность D2Flash и D2Read сохранена
