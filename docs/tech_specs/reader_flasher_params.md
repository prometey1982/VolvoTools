# ТЗ: Рефакторинг параметров читателей и флешеров

## 1. Цель

Заменить монолитный `FlasherParameters` на систему с интерфейсами-провайдерами (`ReaderParametersProviderBase`, `FlasherParametersProviderBase`), где фабрика сама запрашивает у провайдера только те параметры, которые нужны конкретному читателю/флешеру. Исключить runtime-ошибки из-за отсутствия полей и костыли вроде `additionalData`/`cmInfo`.

## 2. Текущая архитектура (было)

```cpp
struct FlasherParameters {
    common::CarPlatform carPlatform;
    uint32_t ecuId;
    std::string additionalData;           // костыль: VAG compression/encryption кодируется строкой
    std::shared_ptr<SBLProviderBase> sblProvider;
    const common::VBF flash;
    std::unique_ptr<common::CompressorBase> compressor;
    std::unique_ptr<common::EncryptorBase> encryptor;
};
```

Использовался всеми флешерами (`D2FlasherBase`, `UDSFlasher`, `KWPFlasher`) и читателем `D2Reader`.

| № | Проблема |
|---|---|
| 1 | **Нет обязательности полей** — `D2Reader` требует `sblProvider`, но поле могло быть `nullptr` |
| 2 | **`additionalData` — строковый костыль** — VAG compression/encryption кодировался строкой |
| 3 | **`cmInfo` — строковый костыль** — выбор AW55/TF80 по строке |
| 4 | **Одна структура на всех** — флешеру VAG не нужен `sblProvider`, `D2Reader` не нужен `pin` |
| 5 | **`unique_ptr` в struct** — move-only, нельзя скопировать |

## 3. Целевая архитектура (стало)

### 3.1. Вспомогательные типы данных

```cpp
// ParamsTypes.hpp
struct ReadRange { uint32_t startAddr; size_t size; };
using ReadRanges = std::vector<ReadRange>;

struct AuthorizationParams { uint64_t pin; };
struct BootloaderParams { common::VBF bootloader; };  // VBF, не SBLProviderBase*
struct EncryptionParams {
    common::CompressionType compression;
    common::EncryptionType encryption;
};
```

### 3.2. Config-структуры

```cpp
// FlasherConfigs.hpp
struct D2FlasherConfig {
    common::VBF bootloader;     // предзарезолвленный VBF (не SBLProviderBase*)
    const common::VBF flash;
};

struct UDSFlasherConfig {
    std::array<uint8_t, 5> pin;
    common::VBF bootloader;     // предзарезолвленный VBF
    const common::VBF flash;
};

struct KWPFlasherConfig {
    common::VBF bootloader;
    std::array<uint8_t, 5> pin;
    const common::VBF flash;
    common::CompressionType compressionType;
};

struct VAGFlasherConfig {
    std::array<uint8_t, 5> pin;
    const common::VBF flash;
    common::CompressionType compressionType;
    common::EncryptionType encryptionType;
};
```

**Ключевое отличие от первой версии:** `bootloader` — готовый `VBF`, а не `SBLProviderBase*`. Бутлоадер резолвится ДО создания config-а, в фабрике или CLI.

### 3.3. Provider base классы

```cpp
// ReaderParametersProviderBase.hpp
class ReaderParametersProviderBase {
public:
    ReaderParametersProviderBase(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& cmInfo);
    common::CarPlatform getCarPlatform() const;
    uint32_t getEcuId() const;
    const std::string& getCmInfo() const;
    virtual ReadRanges getReadRanges() const = 0;      // вектор диапазонов
    virtual std::optional<AuthorizationParams> getAuthParams() const;
    virtual std::optional<BootloaderParams> getBootloaderParams() const;
};

// FlasherParametersProviderBase.hpp
class FlasherParametersProviderBase {
public:
    FlasherParametersProviderBase(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& cmInfo);
    common::CarPlatform getCarPlatform() const;
    uint32_t getEcuId() const;
    const std::string& getCmInfo() const;
    virtual const common::VBF& getFlashData() const = 0;
    virtual std::optional<AuthorizationParams> getAuthParams() const;
    virtual std::optional<BootloaderParams> getBootloaderParams() const;
    virtual std::optional<EncryptionParams> getEncryptionParams() const;
};
```

### 3.4. FlasherBase

```cpp
class FlasherBase: public FlasherCallbackHolder {
public:
    FlasherBase(j2534::J2534 &j2534, common::CarPlatform carPlatform, uint32_t ecuId);
    static size_t getProgressFromVBF(const common::VBF& vbf);
    common::CarPlatform getCarPlatform() const;
    uint32_t getEcuId() const;
    // ... start, state, progress, callbacks
protected:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
};
```

Каждый флешер хранит свою config-структуру. `D2FlasherBase` делегирует выполнение в `D2FlasherImpl` (отдельный .cpp с HFSM2):

```
D2FlasherBase → D2FlasherImpl::run() → HFSM2: WakeUp → FallAsleep → StartPBL → LoadSBL → StartSBL → [колбэк] → WakeUp → Done
```

### 3.5. ReaderBase

```cpp
class ReaderBase: public FlasherCallbackHolder {
public:
    ReaderBase(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId, ReadRanges ranges);
    const std::vector<std::vector<uint8_t>>& buffers() const;  // вектор буферов
    FlasherState getCurrentState() const;
    void start();
protected:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const ReadRanges _ranges;                     // вектор диапазонов
    common::J2534ChannelProvider _channelProvider;
    std::vector<std::vector<uint8_t>> _buffers;   // буфер на каждый диапазон
    virtual void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) = 0;
};
```

### 3.6. D2FlasherImpl — общий HFSM2 для D2ReaderChecksum и D2FlasherBase

```cpp
class D2FlasherImpl {
public:
    D2FlasherImpl(channels, carPlatform, ecuId, bootloader,
                  stateUpdater, progressUpdater, eraseCallback, writeCallback);
    void run();   // запускает FSM, ждёт Done/Error
    // ... setMaximumFlashProgressValue, getMaximumProgress, isFailed ...
};
```

Используется:
- `D2FlasherBase` — с eraseStep/writeStep колбэками (прошивка)
- `D2ReaderChecksum` — с no-op erase и write-читателем (чтение)

### 3.7. Фабрики

```cpp
// ReaderFactory
static std::unique_ptr<ReaderBase> create(j2534::J2534&, const ReaderParametersProviderBase&);

// FlasherFactory
static std::unique_ptr<FlasherBase> create(j2534::J2534&, const FlasherParametersProviderBase&);
```

Диспетчеризация:
- D2 ECM → `D2ReaderChecksum` (Ranges)
- D2 TCM (cmInfo=="aw55_p2") → `D2ReaderAW55` (Ranges)
- D2 TCM (cmInfo=="tf80_p2") → `D2ReaderTF80` (Ranges)
- UDS → `UDSReader` (Ranges + PIN)
- D2 flash → `D2Flasher` (BootloaderParams)
- UDS flash → `UDSFlasher` (BootloaderParams + PIN опционален)
- VAG → `UDSFlasher` (PIN + EncryptionParams)

## 4. Итоговый список файлов

### 4.1. Новые

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/flasher/ParamsTypes.hpp` | `ReadRange`, `ReadRanges`, `AuthorizationParams`, `BootloaderParams`, `EncryptionParams` |
| 2 | `Flasher/flasher/ReaderParametersProviderBase.hpp` | Provider base для читателей |
| 3 | `Flasher/flasher/FlasherParametersProviderBase.hpp` | Provider base для флешеров |
| 4 | `Flasher/flasher/FlasherConfigs.hpp` | `D2FlasherConfig`, `UDSFlasherConfig`, `KWPFlasherConfig` |
| 5 | `Flasher/flasher/ReaderBase.hpp` | Базовый класс читателя (ReadRanges + _buffers) |
| 6 | `Flasher/src/ReaderBase.cpp` | Реализация ReaderBase |
| 7 | `Flasher/flasher/ReaderFactory.hpp` | Фабрика читателей |
| 8 | `Flasher/src/ReaderFactory.cpp` | Реализация ReaderFactory |
| 9 | `Flasher/flasher/D2ReaderChecksum.hpp` | D2-чтение через checksum (заменил D2Reader) |
| 10 | `Flasher/src/D2ReaderChecksum.cpp` | Использует D2FlasherImpl |
| 11 | `Flasher/flasher/UDSReader.hpp` | UDS-чтение через 0x23 |
| 12 | `Flasher/src/UDSReader.cpp` | Реализация UDSReader |
| 13 | `Flasher/flasher/D2ReaderAW55.hpp` | AW55 TCM чтение |
| 14 | `Flasher/src/D2ReaderAW55.cpp` | Реализация |
| 15 | `Flasher/flasher/D2ReaderTF80.hpp` | TF80 TCM чтение |
| 16 | `Flasher/src/D2ReaderTF80.cpp` | Реализация |
| 17 | `Flasher/flasher/FlasherFactory.hpp` | Переписан — новая сигнатура |
| 18 | `Flasher/src/FlasherFactory.cpp` | Реализация FlasherFactory |
| 19 | `Flasher/src/D2FlasherImpl.hpp` | HFSM2 + D2-шаги (отдельная единица трансляции) |
| 20 | `Flasher/src/D2FlasherImpl.cpp` | Реализация D2FlasherImpl + FSM states |

### 4.2. Изменённые

| № | Файл | Изменение |
|---|---|---|
| 21 | `Flasher/flasher/FlasherBase.hpp` | Убран `FlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId)` |
| 22 | `Flasher/src/FlasherBase.cpp` | Конструктор без `FlasherParameters` |
| 23 | `Flasher/flasher/D2FlasherBase.hpp` | Конструктор: `(j2534, CarPlatform, ecuId, D2FlasherConfig&&)` |
| 24 | `Flasher/src/D2FlasherBase.cpp` | D2FlasherImpl + HFSM2 вынесены в D2FlasherImpl.cpp. `startImpl` делегирует `impl.run()` |
| 25 | `Flasher/flasher/D2Flasher.hpp` | Конструктор: `(j2534, CarPlatform, ecuId, D2FlasherConfig&&)` |
| 26 | `Flasher/src/D2Flasher.cpp` | `getConfig().flash` вместо `getFlasherParameters().flash` |
| 27 | `Flasher/flasher/UDSFlasher.hpp` | Убраны `UDSFlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId, UDSFlasherConfig&&)` |
| 28 | `Flasher/src/UDSFlasher.cpp` | `UDSFlasherImpl` переписан под `UDSFlasherConfig` |
| 29 | `Flasher/flasher/KWPFlasher.hpp` | Убраны `KWPFlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId, KWPFlasherConfig&&)` |
| 30 | `Flasher/src/KWPFlasher.cpp` | `KWPFlasherImpl` переписан под `KWPFlasherConfig` |
| 31 | `VolvoFlasher/src/VolvoFlasher.cpp` | `readFlash()`, `UDSFlash()`, `D2Flash()` — создают config напрямую |

### 4.3. Удалённые

| № | Файл | Причина |
|---|---|---|
| 32 | `Flasher/flasher/FlasherParameters.hpp` | Struct удалён из FlasherBase.hpp |
| 33 | `Flasher/flasher/UDSMemoryReader.hpp` | Заменён на `UDSReader` |
| 34 | `Flasher/src/UDSMemoryReader.cpp` | Заменён на `UDSReader` |
| 35 | `Flasher/flasher/D2Reader.hpp` | Заменён на `D2ReaderChecksum` |
| 36 | `Flasher/src/D2Reader.cpp` | Заменён на `D2ReaderChecksum` |

## 5. Ключевые архитектурные решения

| Решение | Мотивация |
|---|---|
| `BootloaderParams` содержит `VBF`, а не `SBLProviderBase*` | Бутлоадер резолвится до создания config-а. Config-ы не зависят от SBLProviderBase |
| `ReaderBase` принимает `ReadRanges` (вектор) | Поддержка нескольких диапазонов чтения с отдельными буферами |
| `D2FlasherImpl` — отдельный .cpp с HFSM2 | Одна копия FSM для D2FlasherBase и D2ReaderChecksum |
| Config-структуры без `unique_ptr` | Копируемые, можно хранить по значению |
| Provider base с default-реализациями (`std::nullopt`) | Наследник переопределяет только то, что нужно |

## 6. Критерии готовности

1. ✓ `ParamsTypes.hpp` содержит `ReadRange`, `ReadRanges`, `AuthorizationParams`, `BootloaderParams`, `EncryptionParams`
2. ✓ `ReaderParametersProviderBase` — non-virtual геттеры + virtual `getReadRanges()`, `getAuthParams()`, `getBootloaderParams()`
3. ✓ `FlasherParametersProviderBase` — то же + `getFlashData()`, `getEncryptionParams()`
4. ✓ `FlasherConfigs.hpp` — отдельные struct для D2, UDS, KWP, VAG с полем `bootloader` типа `VBF`
5. ✓ `FlasherBase` не содержит `_flasherParameters` — хранит `_carPlatform` + `_ecuId`
6. ✓ `ReaderBase` — базовый класс с потоком, прогрессом, `ReadRanges`, `_buffers`
7. ✓ `D2FlasherImpl` — отдельная единица трансляции, содержит HFSM2 + `run()`
8. ✓ `ReaderFactory::create()` диспетчеризует по `(platform, ecuId, cmInfo)`
9. ✓ `FlasherFactory::create()` диспетчеризует по `(platform, ecuId)`
10. ✓ `D2Reader` → `D2ReaderChecksum`, `UDSMemoryReader` → `UDSReader`
11. ✓ `FlasherParameters`, `UDSFlasherParameters`, `KWPFlasherParameters` удалены
12. ✓ Сборка: `cmake --build build --config Release` — 0 ошибок
