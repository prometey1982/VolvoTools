# ТЗ: Реализация чтения прошивок из ЭБУ

## 1. Цель

Чтение прошивок разных ЭБУ через единый интерфейс с поддержкой D2 и UDS протоколов.

## 2. Архитектура (текущая)

### 2.1. Диаграмма классов

```mermaid
---
title: Диаграмма класса читателей
---
classDiagram
    FlasherCallbackHolder <|-- ReaderBase
    ReaderBase <|-- D2ReaderChecksum
    ReaderBase <|-- D2ReaderME7
    ReaderBase <|-- D2ReaderDEM
    ReaderBase <|-- UDSReader
    ReaderBase <|-- D2ReaderAW55
    ReaderBase <|-- D2ReaderTF80
    class FlasherCallbackHolder {
        +registerCallback()
        +unregisterCallback()
    }
    class ReaderBase {
        +ReaderBase(j2534, carPlatform, ecuId, ReadRanges)
        +buffers(): vector~vector~uint8_t~~
        +getCurrentProgress(): size_t
        +getMaximumProgress(): size_t
        +getCurrentState(): FlasherState
        +start()
    }
    class D2ReaderChecksum {
        +D2ReaderChecksum(j2534, carPlatform, ecuId, ReadRanges)
    }
    class UDSReader {
        +UDSReader(j2534, carPlatform, ecuId, ReadRanges, pin)
    }
    class D2ReaderAW55 {
        +D2ReaderAW55(j2534, carPlatform, ecuId, ReadRanges)
    }
    class D2ReaderTF80 {
        +D2ReaderTF80(j2534, carPlatform, ecuId, ReadRanges)
    }
    class ReaderFactory {
        +create(j2534, ReaderParametersProviderBase) ReaderBase
    }
```

### 2.2. ReaderBase

Базовый класс для всех читателей. Наследует `FlasherCallbackHolder`. Инкапсулирует поток, прогресс, состояния, буферы.

```cpp
class ReaderBase: public FlasherCallbackHolder {
public:
    ReaderBase(j2534::J2534& j2534, common::CarPlatform carPlatform,
               uint32_t ecuId, ReadRanges ranges);
    virtual ~ReaderBase();

    FlasherState getCurrentState() const;
    size_t getCurrentProgress() const;
    size_t getMaximumProgress() const;
    const std::vector<std::vector<uint8_t>>& buffers() const;
    void start();

protected:
    virtual void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) = 0;
    void setCurrentState(FlasherState state);
    void incCurrentProgress(size_t delta);
    void setMaximumProgress(size_t maxProgress);

    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const ReadRanges _ranges;
    common::J2534ChannelProvider _channelProvider;
    std::vector<std::vector<uint8_t>> _buffers;
};
```

**Ключевые изменения:**
- `ReadRanges` — вектор `ReadRange`, поддержка нескольких диапазонов
- `_buffers` — вектор буферов, по одному на каждый диапазон
- `buffers()` возвращает `vector<vector<uint8_t>>`

### 2.3. D2ReaderChecksum

Чтение по протоколу D2 через checksum. Использует `D2FlasherImpl` (общий HFSM2 с `D2FlasherBase`):
- Передаёт no-op erase-колбэк
- В write-колбэке выполняет побайтовое чтение: `jumpTo(addr)` → запрос checksum → чтение байта
- Не требует bootloader (передаёт пустой `VBF()`)

**Файлы:** `Flasher/flasher/D2ReaderChecksum.hpp`, `Flasher/src/D2ReaderChecksum.cpp`

### 2.4. UDSReader

Чтение по протоколу UDS (ISO 15765). Использует сервис 0x23 (`ReadMemoryByAddress`) блоками по 0x100 байт. Платформы: P3, Ford_UDS, VAG, Haval_UDS. Требует PIN для авторизации.

**Файлы:** `Flasher/flasher/UDSReader.hpp`, `Flasher/src/UDSReader.cpp`

### 2.5. D2ReaderAW55

Специализированный читатель для АКПП AW55-50SN. Использует `D2Messages::createReadDataByOffsetMsg()`.

**Файлы:** `Flasher/flasher/D2ReaderAW55.hpp`, `Flasher/src/D2ReaderAW55.cpp`

### 2.6. D2ReaderTF80

Специализированный читатель для АКПП TF-80SC. Использует `D2Messages::createReadTCMTF80DataByAddr()`.

**Файлы:** `Flasher/flasher/D2ReaderTF80.hpp`, `Flasher/src/D2ReaderTF80.cpp`

### 2.7. ReaderFactory

Фабрика принимает `ReaderParametersProviderBase`, диспетчеризует по `(carPlatform, ecuId, cmInfo)`:

```cpp
// Flasher/src/ReaderFactory.cpp
std::unique_ptr<ReaderBase> ReaderFactory::create(
    j2534::J2534& j2534,
    const ReaderParametersProviderBase& p)
{
    const auto platform = p.getCarPlatform();
    const auto ecuId = p.getEcuId();
    const auto& cmInfo = p.getCmInfo();
    const auto ranges = p.getReadRanges();

    if (isD2Platform(platform)) {
        if (ecuId == to_underlying(common::D2ECUType::ECM_ME))
            return std::make_unique<D2ReaderME7>(j2534, platform, ecuId, ranges, bootloader);
        if (ecuId == to_underlying(common::D2ECUType::DEM))
            return std::make_unique<D2ReaderDEM>(j2534, platform, ecuId, ranges, bootloader);
        if (ecuId == to_underlying(common::D2ECUType::TCM)) {
            if (cmInfo == "aw55_p2")  return D2ReaderAW55(...);
            if (cmInfo == "tf80_p2") return D2ReaderTF80(...);
        }
        return std::make_unique<D2ReaderChecksum>(j2534, platform, ecuId, ranges);
    }
    if (isUDSPlatform(platform)) {
        auto auth = p.getAuthParams();
        if (!auth) throw ...;
        return std::make_unique<UDSReader>(j2534, platform, ecuId, ranges, auth->pin);
    }
    throw std::runtime_error("Unsupported platform/ECU for reading");
}
```

**Примечание:** D2ReaderME7 и D2ReaderDEM требуют bootloader (`getBootloaderParams()`), в отличие от D2ReaderChecksum.

### 2.8. D2ReaderME7

Чтение ECM_ME по протоколу D2. Использует `D2FlasherImpl` (общий HFSM2 с D2FlasherBase):
- Передаёт no-op erase-колбэк
- В write-колбэке выполняет побайтовое чтение: `createReadOffsetMsg2(addr)` → send/recv → извлечение байта из ответа
- Требует bootloader (SBL) для входа в режим прошивки

**Файлы:** `Flasher/flasher/D2ReaderME7.hpp`, `Flasher/src/D2ReaderME7.cpp`

### 2.9. D2ReaderDEM

Чтение DEM по протоколу D2. Аналогичен D2ReaderME7:
- Использует `createReadOffsetMsgDEM` для формирования запросов
- Требует bootloader

**Файлы:** `Flasher/flasher/D2ReaderDEM.hpp`, `Flasher/src/D2ReaderDEM.cpp`

### 2.10. CLI — readFlash через ReaderFactory

В `VolvoFlasher.cpp` создаётся inline `CLIReaderProvider`, вызывается `ReaderFactory::create()`:

```
CLIReaderProvider provider(carPlatform, ecuId, cmInfo, ranges, pin);
auto reader = ReaderFactory::create(j2534, provider);
reader->start();
// ... wait for Done
output.write(reader->buffers()[0].data(), ...);
```

## 3. Файлы

### 3.1. Новые

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/flasher/ParamsTypes.hpp` | `ReadRange`, `ReadRanges`, `AuthorizationParams`, `BootloaderParams` |
| 2 | `Flasher/flasher/ReaderParametersProviderBase.hpp` | Provider base |
| 3 | `Flasher/flasher/FlasherCallbackHolder.hpp` | Базовый класс с callbacks |
| 4 | `Flasher/flasher/ReaderBase.hpp` | Абстрактный базовый класс читателя (ReadRanges) |
| 5 | `Flasher/src/ReaderBase.cpp` | Реализация |
| 6 | `Flasher/flasher/ReaderFactory.hpp` | Фабрика читателей |
| 7 | `Flasher/src/ReaderFactory.cpp` | Реализация фабрики |
| 8 | `Flasher/flasher/D2ReaderChecksum.hpp` | D2-чтение через checksum (ECU 0x7A по умолчанию) |
| 9 | `Flasher/src/D2ReaderChecksum.cpp` | Использует D2FlasherImpl, без bootloader |
| 10 | `Flasher/flasher/D2ReaderME7.hpp` | D2-чтение ECM_ME через createReadOffsetMsg2 |
| 11 | `Flasher/src/D2ReaderME7.cpp` | Использует D2FlasherImpl, требует bootloader |
| 12 | `Flasher/flasher/D2ReaderDEM.hpp` | D2-чтение DEM через createReadOffsetMsgDEM |
| 13 | `Flasher/src/D2ReaderDEM.cpp` | Использует D2FlasherImpl, требует bootloader |
| 14 | `Flasher/flasher/UDSReader.hpp` | UDS-читатель через 0x23 |
| 15 | `Flasher/src/UDSReader.cpp` | Реализация |
| 16 | `Flasher/flasher/D2ReaderAW55.hpp` | AW55 TCM |
| 17 | `Flasher/src/D2ReaderAW55.cpp` | Реализация |
| 18 | `Flasher/flasher/D2ReaderTF80.hpp` | TF80 TCM |
| 19 | `Flasher/src/D2ReaderTF80.cpp` | Реализация |

### 3.2. Изменённые / удалённые

| № | Файл | Изменение |
|---|---|---|
| 20 | `Flasher/src/D2FlasherImpl.hpp` | **Новый** — HFSM2 + D2-шаги (общий для FlasherBase и читателей) |
| 21 | `Flasher/src/D2FlasherImpl.cpp` | **Новый** — реализация |
| 22 | `VolvoFlasher/src/VolvoFlasher.cpp` | `readFlash()` переписан под ReaderFactory |
| 23 | `Flasher/flasher/D2Reader.hpp` | **Удалён** → заменён на D2ReaderChecksum |
| 24 | `Flasher/src/D2Reader.cpp` | **Удалён** → заменён на D2ReaderChecksum |
| 25 | `Flasher/flasher/UDSMemoryReader.hpp` | **Не удалён** — оставлен, но реализация закомментирована (dead code) |
| 26 | `Flasher/src/UDSMemoryReader.cpp` | **Не удалён** — оставлен, реализация закомментирована (dead code) |

## 4. D2FlasherImpl — общий HFSM2

`D2FlasherImpl` находится в `Flasher/src/D2FlasherImpl.{hpp,cpp}`. Содержит:
- HFSM2 автомат (11 состояний: FallAsleep → StartPBL → LoadSBL → StartSBL → Erase → Write → WakeUpFinish → SetDIMTime → Done/Error)
- `run()` — запускает FSM, ждёт Done/Error
- Колбэки: `eraseCallback`, `writeCallback`

Используется:
- `D2FlasherBase` → erase = стирание, write = запись (прошивка)
- `D2ReaderChecksum` → erase = no-op, write = побайтовое чтение через checksum
- `D2ReaderME7` → erase = no-op, write = побайтовое чтение через createReadOffsetMsg2
- `D2ReaderDEM` → erase = no-op, write = побайтовое чтение через createReadOffsetMsgDEM

## 5. Отложенные компоненты

| Компонент | Причина |
|---|---|
| `D2ReaderReadMemoryByAddr` | Чтение через 0xA6 блоками — не реализовано. `D2ReaderChecksum` читает побайтово |

## 6. Критерии готовности

1. ✓ `ReaderBase` — общий предок с `ReadRanges`, `_buffers`, `FlasherCallbackHolder`
2. ✓ `D2ReaderChecksum` читает побайтово через checksum, использует D2FlasherImpl
3. ✓ `D2ReaderME7` читает ECM_ME через createReadOffsetMsg2, использует D2FlasherImpl
4. ✓ `D2ReaderDEM` читает DEM через createReadOffsetMsgDEM, использует D2FlasherImpl
5. ✓ `UDSReader` читает с UDS-платформ через 0x23 блоками
6. ✓ `D2ReaderAW55` / `D2ReaderTF80` читают TCM
7. ✓ `ReaderFactory::create()` диспетчеризует по (platform, ecuId, cmInfo)
8. ✓ `VolvoFlasher read` использует ReaderFactory
9. ✓ Старый `D2Reader` удалён; `UDSMemoryReader` оставлен как dead code (закомментирован)
10. ✓ Сборка: `cmake --build build --config Release` — 0 ошибок
