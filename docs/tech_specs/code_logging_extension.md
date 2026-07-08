# ТЗ: Расширение прикладного логирования (easyloggingpp)

## 1. Цель

Расширить существующую инфраструктуру логирования на базе easyloggingpp: консольный вывод, ротация логов, per-module уровни, crash-хендлер, трассировка всех протокольных операций, CLOG-миграция и перф-логирование. Текущий `std::cout`-прогресс в `VolvoFlasher` **не заменяется** — только дополняется `LOG(ERROR)` для критических ошибок.

---

## 2. Архитектура

### 2.1. Было

```
startup → common::initLogger("application.log")
                   ↓
    el::Configurations
      Format: "%datetime %level %msg"
      Filename: "application.log"
      (нет консоли, нет ротации, один файл на всё)
```

**Участники:**

| Компонент | Файл | Роль |
|---|---|---|
| `Common` | `Common/common/Util.hpp:101` | Объявление `initLogger()` |
| `Common` | `Common/src/Util.cpp:779-786` | Реализация `initLogger()` |
| `Common` | `Common/src/protocols/UDSProtocolCommonSteps.cpp` | 20+ `LOG(INFO)` / `LOG(ERROR)` |
| `Common` | `Common/src/encryption/EncryptorFactory.cpp` | 1× `LOG(WARNING)` |
| `Flasher` | `Flasher/src/FlasherBase.cpp` | 2× `LOG(ERROR)` |
| `Logger` | `Logger/src/Logger.cpp` | 1× `LOG(ERROR)` |
| `VolvoFlasher` | `VolvoFlasher/src/VolvoFlasher.cpp` | `initLogger("application.log")` |
| `VolvoLogger` | `VolvoLogger/src/VolvoLogger.cpp` | `initLogger("application.log")` |

### 2.2. Стало (текущее, Шаги A+B)

```
startup → common::initLogger("application.log")
                   ↓
         ┌─────────┴──────────┐
         ↓                    ↓
  FileAppender (с ротацией)   ConsoleAppender (в Debug автоматически)
         ↓                    ↓
  application.log (10 MB)     stdout (все уровни в Debug, INFO+ в Release)
         ↓
  + per-module логгеры ────── el::Loggers::getLogger("common"/"flasher"/"logger")
  + VOLVOLOG_DEBUG ────────── env var для включения DEBUG/TRACE
```

**Ключевые изменения (выполнено):**
- Формат: `%datetime %level %msg` → `%datetime %level %fbase:%line %msg`
- Ротация: `MaxLogFileSize` = 10 MB
- Консоль: при `enableConsole = true`
- Debug-сборка (`_DEBUG`): консоль + все уровни принудительно
- Release-сборка: только INFO/WARNING/ERROR/FATAL
- Per-module: зарегистрированы `common`, `flasher`, `logger`
- `VOLVOLOG_DEBUG=common,flasher` — включает DEBUG/TRACE для модулей
- Сигнатура: `initLogger(file, enableConsole=false, debugMode=false)` — обратно совместима
- UDSProtocolCommonSteps: все enter/exit → `LOG(TRACE)`

---

## 3. Проблемы (полный список)

| № | Проблема | Статус |
|---|---|---|
| 1 | **Нет консольного лога** | ✓ Шаг A |
| 2 | **Нет ротации** | ✓ Шаг A |
| 3 | **Один лог-файл, нет per-module** | ✓ Шаг B |
| 4 | **Нет DEBUG/TRACE в коде** | ✓ Шаг B (env var + UDS enter/exit) |
| 5 | **D2ProtocolCommonSteps — 0 LOG** | ✓ Шаг 3 |
| 6 | **KWPProtocolCommonSteps — 0 LOG** | ✓ Шаг 4 |
| 7 | **HFSM2 flasher-ы — ошибки в setFailed() не логируются** | ✓ Шаг 5 |
| 8 | **VolvoFlasher — 59 std::cout, 0 LOG** | ✓ Шаг 2 (критические) |
| 9 | **J2534ChannelAdapter — 0 LOG** | ✓ Шаг 8 |
| 10 | **TP20Session — 0 LOG** | ✓ Шаг 9 |
| 11 | **Perf-логирование** | ✓ Шаг 10 |
| 12 | **Crash-хендлер** | ✗ Заблокирован (conan-рецепт) |
| 13 | **CLOG не используется** | ✓ Шаг 6 |
| 14 | **Нет -v/--verbose в CLI** | ✓ Шаг 7 |
| 15 | **JSON-формат** | ✗ Отложено |
| 16 | **TIMED_SCOPE** | ✗ Отложено (conan-рецепт) |

---

## 4. Очередь реализации

### Шаг A (✓ выполнен): Базовая инфраструктура

- `initLogger()`: консоль, ротация, `_DEBUG`, формат
- `VolvoFlasher.cpp`, `VolvoLogger.cpp` — без изменений

### Шаг B (✓ выполнен): Per-module логгеры + уровни

- Регистрация `common`/`flasher`/`logger`
- `VOLVOLOG_DEBUG` env var
- UDSProtocolCommonSteps enter/exit → `LOG(TRACE)`

---

### Шаг 1 (10 мин): Crash-хендлер

**Файл:** `Common/src/Util.cpp`

Добавить в `initLogger()`:
```cpp
el::Helpers::setCrashHandler(el::CrashHandler::installCrashHandler());
```

Логирует сигналы SIGSEGV, SIGFPE, SIGABRT и т.д.

**Критерий:** При `*(int*)0 = 0;` в логе появляется crash-дамп.

---

### Шаг 2 (✓ выполнен): VolvoFlasher — LOG(ERROR) для критических ошибок

**Файл:** `VolvoFlasher/src/VolvoFlasher.cpp`

Добавлен `LOG_MODULE(ERROR) << ...` параллельно `std::cout` в ~12 местах:
- Ошибки seed/key (readMessages, authByKey)
- Исключения (catch-блоки в main)
- Ошибки startPeriodicMsg (findPin, findPin2)
- Progress, список устройств, FlasherState — не тронуты

**Критерий:** ✓ Критические ошибки дублируются в `application.log` без потери std::cout-вывода.

---

### Шаг 3 (✓ выполнен): D2ProtocolCommonSteps — enter/exit TRACE

**Файл:** `Common/src/protocols/D2ProtocolCommonSteps.cpp`

Добавлен `LOG_MODULE(TRACE)` enter/exit для всех публичных методов (8 функций):
`fallAsleep`, `startPBL`, `wakeUp`, `transferData`, `eraseFlash`, `jumpTo`, `startRoutine`, `setDIMTime`

**Критерий:** ✓ Каждая операция логирует вход и выход.

---

### Шаг 4 (✓ выполнен): KWPProtocolCommonSteps — enter/exit TRACE

**Файл:** `Common/src/protocols/KWPProtocolCommonSteps.cpp`

Добавлен `LOG_MODULE(TRACE)` enter/exit для всех публичных методов (8 функций):
`authorize`, `enterProgrammingSession`, `transferData` (2 overloads), `eraseFlash` (2 overloads), `requestDownload`, `startRoutine`

**Критерий:** ✓ Каждая операция логирует вход и выход.

---

### Шаг 5 (✓ выполнен): HFSM2 flasher-ы — LOG(ERROR) из setFailed()

**Файлы:**
- `Flasher/src/D2FlasherBase.cpp`
- `Flasher/src/UDSFlasher.cpp`
- `Flasher/src/KWPFlasher.cpp`

Добавлен `LOG_MODULE(ERROR) << message` в `setFailed()` каждого Impl.

**Критерий:** ✓ Все ошибки из HFSM2 flasher-ов попадают в `application.log`.

---

### Шаг 6 (30 мин): Единый макрос LOG_MODULE

Один и тот же макрос во всех файлах — `LOG_MODULE(level)`. Имя модуля задаётся `#define` перед включением макроса:

```cpp
// Common/common/Util.hpp (или отдельный логгер-заголовок)
// Сам макрос:
#undef LOG_MODULE
#define LOG_MODULE(level) CLOG(level, LOG_MODULE_NAME)

// В каждом .cpp:
#define LOG_MODULE_NAME "common"
#include "common/Util.hpp"  // или: #define LOG_MODULE(level) CLOG(level, LOG_MODULE_NAME)

// use (везде одинаково):
LOG_MODULE(TRACE) << "transferChunk enter";
```

| Файл | `LOG_MODULE_NAME` |
|---|---|
| `UDSProtocolCommonSteps.cpp` | `"common"` |
| `D2ProtocolCommonSteps.cpp` | `"common"` |
| `KWPProtocolCommonSteps.cpp` | `"common"` |
| `TP20Session.cpp` | `"common"` |
| `J2534ChannelAdapter.cpp` | `"common"` |
| `EncryptorFactory.cpp` | `"common"` |
| `FlasherBase.cpp` | `"flasher"` |
| `D2FlasherBase.cpp` | `"flasher"` |
| `UDSFlasher.cpp` | `"flasher"` |
| `KWPFlasher.cpp` | `"flasher"` |
| `VolvoFlasher.cpp` | `"flasher"` |
| `Logger.cpp` | `"logger"` |

**Критерий:** `VOLVOLOG_DEBUG=common` включает DEBUG только для Common. Код использует `LOG_MODULE(level)` — единое имя, модуль задаётся одной строкой `#define LOG_MODULE_NAME "..."` в начале `.cpp`.

---

### Шаг 7 (15 мин): -v/--verbose CLI флаг

**Файлы:**
- `VolvoFlasher/src/VolvoFlasher.cpp`
- `VolvoLogger/src/VolvoLogger.cpp`

Добавить `-v`/`--verbose` flag в argparse:
```cpp
program.add_argument("-v", "--verbose")
    .help("Enable verbose (debug) logging")
    .default_value(false)
    .implicit_value(true);
```

В main передать в `initLogger`:
```cpp
common::initLogger("application.log", verbose, verbose);
```

**Критерий:** `VolvoFlasher -v` включает консоль + DEBUG/TRACE.

---

### Шаг 8 (15 мин): DEBUG в J2534ChannelAdapter

**Файл:** `Common/src/J2534ChannelAdapter.cpp`

Добавить `LOG(DEBUG)` на ошибки send/receive/startPeriodicMsg:
```cpp
bool J2534ChannelAdapter::send(const CanFrame& frame) {
    PASSTHRU_MSG msg = toPassThruMsg(frame);
    unsigned long numMsgs = 1;
    auto rc = _channel->writeMsgs(&msg, numMsgs, 0);
    if (rc != STATUS_NOERROR) {
        LOG(DEBUG) << "J2534ChannelAdapter::send failed, rc=" << rc;
        return false;
    }
    return true;
}
```

Аналогично для `receive`, `startPeriodicMsg`, `stopPeriodicMsg`, `startMsgFilter`.

**Критерий:** При `VOLVOLOG_DEBUG=common` видны ошибки J2534-вызовов.

---

### Шаг 9 (20 мин): TRACE в TP20Session

**Файл:** `Common/src/protocols/TP20Session.cpp`

Добавить `LOG(TRACE)` на:
- Вход/выход из состояний HFSM2 (~7 состояний)
- Таймауты `connect`, `sendRequest`, `readResponse`

```cpp
void TP20Session::sendRequest() {
    LOG(TRACE) << "TP20Session::sendRequest enter";
    ...
    if (timeout) {
        LOG(DEBUG) << "TP20Session::sendRequest timeout";
    }
    LOG(TRACE) << "TP20Session::sendRequest exit";
}
```

**Критерий:** При `VOLVOLOG_DEBUG=common` видны все переходы TP20Session.

---

### Шаг 10 (20 мин): Перф-логирование (std::chrono)

**Файлы:**
- `Common/common/Util.hpp` — макрос
- `Common/src/protocols/UDSProtocolCommonSteps.cpp`
- `Flasher/src/FlasherBase.cpp`
- `Logger/src/Logger.cpp`

Добавить макрос в `Util.hpp`:
```cpp
#define LOG_SCOPE_DURATION(msg) \
    auto ELPP_UNIQUE_NAME(_perfStart) = std::chrono::steady_clock::now(); \
    ... defer { \
        auto _elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( \
            std::chrono::steady_clock::now() - ELPP_UNIQUE_NAME(_perfStart)).count(); \
        LOG(INFO) << msg << " took " << _elapsed << " ms"; \
    }
```

(Или через ScopeGuard / деструктор локального объекта.)

**Где добавить:**

| Место | Операция |
|---|---|
| `UDSProtocolCommonSteps::transferChunk()` | Замер записи блока |
| `UDSProtocolCommonSteps::transferData()` | Замер всей передачи |
| `UDSProtocolCommonSteps::authorize()` | Замер авторизации |
| `UDSProtocolCommonSteps::eraseFlash()` | Замер стирания |
| `FlasherBase::start()` | Замер полной прошивки |
| `Logger::logFunction()` | Замер одного цикла опроса ЭБУ |

**Критерий:** В `application.log` есть записи вида `transferChunk took 1234 ms`.

---

## 5. Все файлы

| № | Файл | Шаг |
|---|---|---|
| 1 | `Common/common/Util.hpp` | A |
| 2 | `Common/src/Util.cpp` | A, 1 |
| 3 | `Common/src/protocols/UDSProtocolCommonSteps.cpp` | B, 10 |
| 4 | `Common/src/protocols/D2ProtocolCommonSteps.cpp` | 3, 6 |
| 5 | `Common/src/protocols/KWPProtocolCommonSteps.cpp` | 4, 6 |
| 6 | `Common/src/protocols/TP20Session.cpp` | 9 |
| 7 | `Common/src/J2534ChannelAdapter.cpp` | 8 |
| 8 | `Flasher/src/D2FlasherBase.cpp` | 5, 6 |
| 9 | `Flasher/src/UDSFlasher.cpp` | 5, 6 |
| 10 | `Flasher/src/KWPFlasher.cpp` | 5, 6 |
| 11 | `Flasher/src/FlasherBase.cpp` | 6, 10 |
| 12 | `Logger/src/Logger.cpp` | 6, 10 |
| 13 | `VolvoFlasher/src/VolvoFlasher.cpp` | 2, 6, 7 |
| 14 | `VolvoLogger/src/VolvoLogger.cpp` | 7 |

## 6. Критерии готовности

1. ✓ `application.log` — формат `%datetime %level %fbase:%line %msg`
2. ✓ Debug-сборка: консоль + все уровни
3. ✓ Release-сборка: только файл, INFO+
4. ✓ Ротация: 10 MB
5. ✓ `VOLVOLOG_DEBUG=flasher` включает DEBUG для Flasher
6. ✓ UDSProtocolCommonSteps: enter/exit → `LOG(TRACE)`
7. ☐ Шаг 1: Crash-хендлер — заблокирован conan-рецептом
8. ✓ Шаг 2: Критические std::cout в VolvoFlasher продублированы LOG_MODULE(ERROR)
9. ✓ Шаг 3: D2ProtocolCommonSteps enter/exit → LOG_MODULE(TRACE)
10. ✓ Шаг 4: KWPProtocolCommonSteps enter/exit → LOG_MODULE(TRACE)
11. ✓ Шаг 5: setFailed() в HFSM2 flasher-ах → LOG_MODULE(ERROR)
12. ✓ Шаг 6: LOG_MODULE макрос — per-module фильтрация работает
13. ✓ Шаг 7: VolvoFlasher --verbose / VolvoLogger --verbose включает verbose
14. ✓ Шаг 8: J2534ChannelAdapter ошибки → LOG_MODULE(DEBUG)
15. ✓ Шаг 9: TP20Session enter/exit/timeout → LOG_MODULE(TRACE/DEBUG)
16. ✓ Шаг 10: LOG_SCOPE_DURATION в ключевых операциях
17. ✓ VolvoFlasher progress (std::cout) не изменён
18. ✓ SA2.cpp не изменён
19. ✓ Проект собирается: 0 ошибок
