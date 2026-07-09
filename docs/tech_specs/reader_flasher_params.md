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
struct AuthorizationParams { uint64_t pin; };
struct BootloaderParams { std::shared_ptr<SBLProviderBase> sblProvider; };
struct EncryptionParams {
    common::CompressionType compression;
    common::EncryptionType encryption;
};
```

### 3.2. Config-структуры

```cpp
// FlasherConfigs.hpp
struct D2FlasherConfig {
    std::shared_ptr<SBLProviderBase> sblProvider;
    const common::VBF flash;
};

struct UDSFlasherConfig {
    std::array<uint8_t, 5> pin;
    std::shared_ptr<SBLProviderBase> sblProvider;
    const common::VBF flash;
};

struct KWPFlasherConfig {
    std::shared_ptr<SBLProviderBase> sblProvider;
    std::array<uint8_t, 5> pin;
    const common::VBF flash;
    common::CompressionType compressionType;
};
```

### 3.3. Provider base классы

```cpp
// ReaderParametersProviderBase.hpp
class ReaderParametersProviderBase {
public:
    ReaderParametersProviderBase(common::CarPlatform carPlatform, uint32_t ecuId, const std::string& cmInfo);
    common::CarPlatform getCarPlatform() const;
    uint32_t getEcuId() const;
    const std::string& getCmInfo() const;
    virtual ReadRange getReadRange() const = 0;
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

### 3.4. FlasherBase — без FlasherParameters

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

Каждый конкретный флешер хранит свою config-структуру:

```cpp
class D2Flasher : public D2FlasherBase {
    D2FlasherConfig _config;
};

class UDSFlasher : public FlasherBase {
    const UDSFlasherConfig _config;
};

class KWPFlasher : public FlasherBase {
    const KWPFlasherConfig _config;
};
```

### 3.5. ReaderBase

```cpp
class ReaderBase: public FlasherCallbackHolder {
public:
    ReaderBase(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId, ReadRange range);
    const std::vector<uint8_t>& buffer() const;
    FlasherState getCurrentState() const;
    void start();
protected:
    const common::CarPlatform _carPlatform;
    const uint32_t _ecuId;
    const ReadRange _range;
    common::J2534ChannelProvider _channelProvider;
    std::vector<uint8_t> _buffer;
    virtual void startImpl(std::vector<std::unique_ptr<ICanChannel>>& channels) = 0;
};
```

### 3.6. Фабрики

```cpp
class ReaderFactory {
    static std::unique_ptr<ReaderBase> create(
        j2534::J2534& j2534,
        const ReaderParametersProviderBase& params);
    static bool isD2Platform(common::CarPlatform p);
    static bool isUDSPlatform(common::CarPlatform p);
};

class FlasherFactory {
    static std::unique_ptr<FlasherBase> create(
        j2534::J2534& j2534,
        const FlasherParametersProviderBase& params);
    static bool isD2Platform(common::CarPlatform p);
    static bool isUDSPlatform(common::CarPlatform p);
};
```

Фабрики диспетчеризуют по (platform, ecuId, cmInfo) и сами запрашивают данные у provider-а:

- D2 ECM → `D2Reader` с `BootloaderParams`
- D2 TCM (cmInfo=="aw55") → `D2ReaderAW55` 
- D2 TCM (cmInfo=="tf80_p2") → `D2ReaderTF80`
- UDS → `UDSReader` с `AuthorizationParams` (PIN)
- D2 flash → `D2Flasher` с `BootloaderParams`
- UDS flash → `UDSFlasher` с `BootloaderParams` + `AuthorizationParams` (PIN опционален)
- VAG → `UDSFlasher` с `AuthorizationParams` + `EncryptionParams`

### 3.7. CLI — inline провайдеры

`CLIReaderProvider` и `CLIFlasherProvider` создаются прямо внутри функций `readFlash()`, `UDSFlash()`, `D2Flash()` в `VolvoFlasher.cpp`. Они наследуют соответствующий `*ProviderBase`, передают базовые ключи в конструктор и переопределяют data-методы.

```cpp
// Чтение
CLIReaderProvider provider(platform, ecuId, "", start, size, pin, sbl);
auto reader = ReaderFactory::create(j2534, provider);
reader->start();
// ... reader->buffer()

// Прошивка
UDSFlasherConfig config{ pinArray, sblProvider, flash };
UDSFlasher flasher(j2534, carPlatform, ecuId, std::move(config));
flasher.start();
```

## 4. Изменённые файлы

### 4.1. Новые файлы

| № | Файл | Описание |
|---|---|---|
| 1 | `Flasher/flasher/ParamsTypes.hpp` | `ReadRange`, `AuthorizationParams`, `BootloaderParams`, `EncryptionParams` |
| 2 | `Flasher/flasher/ReaderParametersProviderBase.hpp` | Provider base для читателей |
| 3 | `Flasher/flasher/FlasherParametersProviderBase.hpp` | Provider base для флешеров |
| 4 | `Flasher/flasher/FlasherConfigs.hpp` | `D2FlasherConfig`, `UDSFlasherConfig`, `KWPFlasherConfig` |
| 5 | `Flasher/flasher/ReaderBase.hpp` | Базовый класс читателя |
| 6 | `Flasher/src/ReaderBase.cpp` | Реализация ReaderBase |
| 7 | `Flasher/flasher/ReaderFactory.hpp` | Фабрика читателей |
| 8 | `Flasher/src/ReaderFactory.cpp` | Реализация ReaderFactory |
| 9 | `Flasher/flasher/D2Reader.hpp` | Переписан (наследует ReaderBase) |
| 10 | `Flasher/src/D2Reader.cpp` | Переписан (startImpl с D2-шагами) |
| 11 | `Flasher/flasher/UDSReader.hpp` | Новый — UDS чтение через 0x23 |
| 12 | `Flasher/src/UDSReader.cpp` | Реализация UDSReader |
| 13 | `Flasher/flasher/D2ReaderAW55.hpp` | Новый — AW55 TCM чтение |
| 14 | `Flasher/src/D2ReaderAW55.cpp` | Реализация |
| 15 | `Flasher/flasher/D2ReaderTF80.hpp` | Новый — TF80 TCM чтение |
| 16 | `Flasher/src/D2ReaderTF80.cpp` | Реализация |
| 17 | `Flasher/flasher/FlasherFactory.hpp` | Переписан — новая сигнатура |
| 18 | `Flasher/src/FlasherFactory.cpp` | Реализация FlasherFactory |

### 4.2. Изменённые файлы

| № | Файл | Изменение |
|---|---|---|
| 19 | `Flasher/flasher/FlasherBase.hpp` | Убран `FlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId)` |
| 20 | `Flasher/src/FlasherBase.cpp` | Конструктор без `FlasherParameters` |
| 21 | `Flasher/flasher/D2FlasherBase.hpp` | Конструктор: `(j2534, CarPlatform, ecuId, D2FlasherConfig&&)` |
| 22 | `Flasher/src/D2FlasherBase.cpp` | Заменён `getFlasherParameters()` на `_config` |
| 23 | `Flasher/flasher/D2Flasher.hpp` | Конструктор: `(j2534, CarPlatform, ecuId, D2FlasherConfig&&)` |
| 24 | `Flasher/src/D2Flasher.cpp` | `getConfig().flash` вместо `getFlasherParameters().flash` |
| 25 | `Flasher/flasher/UDSFlasher.hpp` | Убраны `UDSFlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId, UDSFlasherConfig&&)` |
| 26 | `Flasher/src/UDSFlasher.cpp` | `UDSFlasherImpl` переписан под `UDSFlasherConfig` |
| 27 | `Flasher/flasher/KWPFlasher.hpp` | Убраны `KWPFlasherParameters`. Конструктор: `(j2534, CarPlatform, ecuId, KWPFlasherConfig&&)` |
| 28 | `Flasher/src/KWPFlasher.cpp` | `KWPFlasherImpl` переписан под `KWPFlasherConfig` |
| 29 | `VolvoFlasher/src/VolvoFlasher.cpp` | `readFlash()`, `UDSFlash()`, `D2Flash()` — создают config напрямую |

## 5. Статус реализации

| Шаг | Статус |
|---|---|
| A: ParamsTypes + provider base классы | ✓ |
| B: ReaderFactory + ReaderBase + читатели | ✓ |
| C: CLI readFlash через ReaderFactory | ✓ |
| D: FlasherConfigs + FlasherBase без FlasherParameters | ✓ |
| E: UDSFlash/D2Flash под новые config-ы | ✓ |
| F: Удалён `FlasherParameters`, `additionalData`, `UDSFlasherParameters`, `KWPFlasherParameters` | ✓ |

## 6. Критерии готовности

1. ✓ `ParamsTypes.hpp` содержит `ReadRange`, `AuthorizationParams`, `BootloaderParams`, `EncryptionParams`
2. ✓ `ReaderParametersProviderBase` — non-virtual геттеры для базовых ключей + virtual data-методы
3. ✓ `FlasherParametersProviderBase` — то же для флешеров
4. ✓ `FlasherConfigs.hpp` — отдельные struct для D2, UDS, KWP
5. ✓ `FlasherBase` не содержит `_flasherParameters` — хранит `_carPlatform` + `_ecuId`
6. ✓ `ReaderBase` — базовый класс с потоком, прогрессом, буфером
7. ✓ `ReaderFactory::create()` диспетчеризует по (platform, ecuId, cmInfo)
8. ✓ `FlasherFactory::create()` диспетчеризует по (platform, ecuId)
9. ✓ `FlasherParameters`, `UDSFlasherParameters`, `KWPFlasherParameters` удалены
10. ✓ Сборка: `cmake --build build --config Release` — 0 ошибок
