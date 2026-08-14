# ТЗ: Улучшение алгоритма разбора ответов в `D2Request::process()`

## 1. Цель

Устранить дефекты и уязвимости разбора ответов D2-протокола в `Common/src/protocols/D2Request.cpp` (бесконечный цикл, отсутствие валидации seriesId, UB на пустом requestId), ужесточить разбор до защитного (throw на нарушения протокола после совпавшего эха), реструктурировать цикл в явный state machine и покрыть новое поведение тестами. Публичный API `process()` **не меняется**.

---

## 2. Архитектура

### 2.1. Формат ответа (из `docs/tech_specs/d2_protocol_implementation.md:134-178`)

**Первый фрейм** (8 байт CAN, 1 байт заголовок + 7 данных):

```
[0] = header
[1] = ecuId                    (должен совпадать с запрошенным)
[2] = requestId[0] + 0x40      (должен совпадать)
[3..2+restRequestSize] = requestId[1..] (должен совпадать, restRequestSize = requestId.size()-1)
[3+restRequestSize..] = полезные данные ответа
```

**Значения header** (таблица из спеки:130-132):

| Бит 0x80 | Бит 0x40 | Значение | Смысл |
|---|---|---|---|
| 1 | 1 | `0xC8..0xCF` | single-frame (последний + первый) |
| 1 | 0 | `0x88..0x8F` | первый фрейм multi-frame серии |
| 0 | 1 | `0x48..0x4F` | последний фрейм серии (длина = `header - 0x48`) |
| 0 | 0 | `0x09→0x0A→…→0x0F→0x08→…` | middle (seriesId, инкремент по модулю 8) |

**Ограничения формата:**
- Эхо requestId занимает `requestId.size() + 1` байт потока данных ответа и **может выходить за пределы первого CAN-фрейма** — при длинном requestId эхо продолжается в кадрах серии (разбор накапливает эхо, копирование данных начинается после его завершения). Ограничения на длину requestId нет.
- Каждый фрейм серии несёт ≤ 7 байт полезных данных.

**Формат ошибки (несостыковка, см. Шаг 7):**
- impl `checkD2Error` (`Common/src/Util.cpp:741-747`, удалён): `data[2] == 0x7F && data[1] == ecuId`, код в `data[3 + requestId.size()]`, guard `dataSize >= requestId.size() + 4` — рассчитан на один кадр, при длинном requestId ошибку не детектировал.
- спека (`d2_protocol_implementation.md:168-178`): «фиксированные позиции» `[4]=0x7F, [5]=ecuId, [6]=код`.
- тест-хелпер `makeErrorResponse` (`Common/test/D2RequestTest.cpp:48-55`): строится по спеке, противоречит impl → тест `ErrorResponse` с текущей реализацией **не бросит** `D2Error`.

### 2.2. Было

```
process(channel, timeout, sendMessagesDelay)
  ─ send: все фреймы D2Message.getFrames() + задержка
  ─ receive: while (true) ──── без лимитов
      firstMessage? ── checkD2Error; эхо-проверка (data[1], data[2], rest);
                       нет совпадения → continue (скип кадра)
                       inSeries = !(header & 0x40)
      inSeries? ────── dataOffset=1; header & 0x40 → resize + конец серии
      копирование result += frame[dataOffset..]
```

Флаги `firstMessage`/`inSeries` + `while(true)`, мёртвая проверка `size() < 1` (строка 82).

### 2.3. Стало

```
process(channel, timeout, sendMessagesDelay)
  ─ preconditions: requestId не пуст (throw)
  ─ send: как было
  ─ receive: state machine:
      state = WaitFirst ──── маркер (data[1]==ecuId, data[2]∈{0x7F, requestId[0]+0x40})
                              → throw/continue/skip; header & 0x80 обязателен,
                              диапазон 0x88..0x8F / 0xC8..0xCF
      эхо-накопление ─────── эхо (requestId.size()+1 байт) собирается и валидируется
                              по кадрам серии; single-frame без полного эха → throw
      state = WaitSeries ─── seriesId == expected (0x09→0x0A→…→0x0F→0x08→…),
                              иначе throw; последний фрейм 0x48..0x4F → конец
      лимиты: maxFrameCount, maxResponseSize → throw
  ─ result: payload, собранный после завершения эха
```

**Участники:**

| Компонент | Файл | Роль |
|---|---|---|
| `Common` | `Common/src/protocols/D2Request.cpp` | Приём + разбор ответа: эхо-накопление, ошибки (основной объект изменений) |
| `Common` | `Common/src/Util.cpp` | Удалён `checkD2Error` (однофреймовый контракт; детекция ошибки встроена в `D2Request::process`) |
| `Common` | `Common/test/D2RequestTest.cpp` | Тесты (Boost.Test, `-DBUILD_TESTS=ON`) |
| `Common` | `Common/src/protocols/D2Message.cpp:28-53` | `generateCanFrames` — источник истины по фреймингу (не меняется) |
| Docs | `docs/tech_specs/d2_protocol_implementation.md` | Обновить секции «Формат ответа» / «Формат ошибки» |

---

## 3. Проблемы (полный список)

| № | Проблема | Локация | Статус |
|---|---|---|---|
| 1 | **Бесконечный цикл / безлимитная память**: `while(true)` без лимита кадров и объёма. Чужой/повреждённый кадр, прошедший эхо-проверку с `header & 0x40 == 0`, — серия копится бесконечно (`result.reserve` растёт без границ) | `D2Request.cpp:57` | ✓ |
| 2 | **seriesId не проверяется**: кадры `0x09→0x0A→…` принимаются без контроля порядка — потеря/дублирование/перестановка кадров молча даёт искажённые данные | `D2Request.cpp:81-95` | ✓ |
| 3 | **Заголовок первого фрейма не валидируется**: бит `0x80` и диапазоны `0x88..0x8F`/`0xC8..0xCF` игнорируются; кадр серии теоретически может быть принят как первый | `D2Request.cpp:66-79` | ✓ |
| 4 | **Пустой requestId → UB**: `requestId.size() - 1` (underflow `size_t`) и `requestId[0]` (чтение из пустого вектора) | `D2Request.cpp:55,73` | ✓ |
| 5 | **Эхо длинного requestId не собирается**: разбор требовал полного эха в первом фрейме, иначе `continue` → на реальной шине таймаут. Тест `LongRequestIdEcho` «проходил» только на невозможном 13-байтном кадре от mock. **Решено: эхо накапливается по кадрам серии** | `D2Request.cpp:70-72` + тест | ✓ |
| 6 | **Мёртвый код**: `response.data.size() < 1` всегда false; флаги `firstMessage`/`inSeries` → трудночитаемый цикл | `D2Request.cpp:57,82` | ✓ |
| 7 | **Расхождение формата ошибки** между impl `checkD2Error`, спекой и тестом `makeErrorResponse`; `checkD2Error` не детектировал ошибку при requestId > 4 байт. **Решено: `checkD2Error` удалён, детекция встроена в разбор** | `Util.cpp:741-747` (удалён), спека:168-178, тест:48-55 | ✓* |
| 8 | **CAN ID кадра не проверяется** (страховка поверх J2534 PASS_FILTER) — **проверка добавлена, затем отменена**: реальные ответы D2 приходят с CAN ID ≠ 0xFFFFE (своим ID ЭБУ), проверка блокировала весь трафик; мок с id 0xFFFFE давал ложную зелёность тестов. Фильтрация — только протокольная (маркер/эхо/seriesId) | `D2Request.cpp` (чек удалён) | ✓ |

\* — impl-формат уточнён по железу (см. Шаг 7): реальный ответ ошибки — маркер `0x7F` + эхо номера сервиса `requestId[0]`, регион 3 байта `[ecuId, 0x7F, requestId[0]]`, код — `result[3]`; framed-формат с полным эхом отвергнут.

---

## 4. Очередь реализации

### Шаг 1 (✓ выполнен): Guard пустого `requestId`

**Файл:** `Common/src/protocols/D2Request.cpp`

- Конструкторы `D2Request` (все 4): пустой `requestId` → `std::invalid_argument("D2Request: empty requestId")`.
- Ограничения на длину requestId **нет** — эхо собирается по кадрам серии (см. Шаг 6).

**Критерий:** пустой requestId бросает исключение до отправки, без UB.

---

### Шаг 2 (✓ выполнен): Лимиты серии

**Файл:** `Common/src/protocols/D2Request.cpp`

- `static constexpr size_t maxResponseSize = 64 * 1024;` — макс. объём `result`.
- `maxFrameCount = maxResponseSize / 7 + 4;` — макс. число принятых кадров (каждый несёт ≤ 7 байт).
- Нарушение любого лимита → `std::runtime_error("D2 response too large")` / `("too many frames")`.
- Счётчики проверяются на каждой итерации receive-цикла.

**Обоснование лимита:** чтение памяти D2-ридерами идёт чанками по 132 байта (`Flasher/src/D2ReaderTF80.cpp:38`) ≈ 20 кадров серии на запрос; 64 КБ с большим запасом покрывает любые реальные ответы.

**Критерий:** зацикленный/мусорный трафик завершается исключением; худший случай по времени ограничен `maxFrameCount × timeout`.

---

### Шаг 3 (✓ выполнен): Валидация seriesId

**Файл:** `Common/src/protocols/D2Request.cpp`

- После первого фрейма серии `expectedSeriesId = 0x09`.
- Для каждого middle-фрейма: `header == expectedSeriesId` — иначе `std::runtime_error("Unexpected seriesId ...")`.
- Инкремент: `expectedSeriesId = 0x08 + ((header - 0x08 + 1) & 0x07)` (последовательность `0x09→…→0x0F→0x08→…`).
- Последний фрейм (`header & 0x40`): `header < 0x48` или `> 0x4F` → `std::runtime_error("Wrong data length in series")`; заявленная длина `header - 0x48` обязана поместиться в кадр.

**Критерий:** потерянный/дублированный/переставленный кадр серии → исключение, а не тихое искажение данных.

---

### Шаг 4 (✓ выполнен): Валидация первого фрейма

**Файл:** `Common/src/protocols/D2Request.cpp`

- `header & 0x80` обязателен: без бита → кадр не является первым — `continue` (чужой трафик, эхо-проверка не пройдена).
- Маркер: `data[1] == ecuId`, `data[2]` ∈ `{0x7F, requestId[0] + 0x40}` — иначе `continue` (чужой трафик).
- После совпавшего маркера: header вне диапазонов `0x88..0x8F` (серия) / `0xC8..0xCF` (single) → `std::runtime_error("Invalid header of first D2 response frame")`.
- Single-frame ответ (`header & 0x40`) без полного эха в одном кадре → `std::runtime_error` ("D2 response ended before requestId echo completed").

**Критерий:** кадр серии не может быть принят как первый; single-frame ответ обязан содержать полное эхо.

---

### Шаг 5 (✓ выполнен): Семантика throw/continue (CAN ID-чек отменён)

**Файл:** `Common/src/protocols/D2Request.cpp`

- **CAN ID не проверяется** (чек `frame.id != D2Message::CanId` добавлен, затем **отменён**): ответы D2 приходят с CAN ID ≠ 0xFFFFE, проверка блокировала реальный трафик. J2534 PASS_FILTER канала всё равно всепроходной (маска `{0x00,0x00,0x00,0x01}`, `Common/src/Util.cpp:250-252`), поэтому фильтрация — только протокольная.
- Семантика закрепляется: **continue** — несовпавший маркер/эхо, пустой кадр, кадр < 3 байт, кадр без бита `0x80` до маркер-проверки; **throw** — любые нарушения после совпавшего маркера, лимиты, таймаут.

**Критерий:** поведение фильтрации чужого трафика (continue) сохранено; протокольные нарушения на «своём» ответе завершаются исключением.

---

### Шаг 6 (✓ выполнен): Рефакторинг в state machine + сборка эха по кадрам

**Файл:** `Common/src/protocols/D2Request.cpp`

Ответ трактуется как поток данных = data-байты первого фрейма + data-байты кадров серии. Позиции: `[0]=ecuId, [1]=requestId[0]+0x40 (или 0x7F), [2..size]=requestId[1..]`, эхо = `size + 1` байт. Весь поток копится в `result`, эхо-префикс валидируется по мере поступления и удаляется одним `erase` в конце (граница эхо/payload не ведётся по кадрам):

```cpp
enum class ParseState { WaitFirst, WaitSeries };

ParseState state = ParseState::WaitFirst;
bool isError = false;
size_t echoRegionSize = 0;      // size+1 для обычного ответа, 3 для ошибки ([ecuId, 0x7F, requestId[0]])
bool echoComplete = false;
uint8_t expectedSeriesId = 0x09;
size_t frameCount = 0;
std::vector<uint8_t> result;

while (true) {
    receive; frameCount; фильтр (empty);
    // CAN ID не проверяется — ответы приходят с ID ≠ 0xFFFFE
    if (state == WaitFirst) {
        // классификация: header & 0x80, size >= 3, ecuId, маркер (0x7F
        // или requestId[0]+0x40) → иначе continue (чужой трафик)
        // header вне 0x88..0x8F / 0xC8..0xCF → throw
        isError / echoRegionSize / expectedSeriesId / state = WaitSeries
    }
    else if (header & 0x40) {   // последний фрейм серии
        header 0x48..0x4F; frameDataSize = header - 0x48; иначе throw
    }
    else {                      // серийный кадр
        header == expectedSeriesId (иначе throw); инкремент seriesId
    }

    // общий шаг: append frameDataSize байт в result + лимит размера
    // валидация эхо-префикса [before, min(size, echoRegionSize)):
    //   рассинхрон → сброс в WaitFirst, result.clear(), continue
    //   region завершён → echoComplete = true
    // isError && собран регион [ecuId, 0x7F, requestId[0]] → throw D2Error(result[3]) (код; нет байта → 0)
    if (endSeries) { if (!echoComplete) throw; break; }
}
result.erase(begin, begin + echoRegionSize);   // срезать эхо
return result;                                  // move, без копий
```

- Мёртвая проверка `size() < 1` удаляется.
- Ошибки приём/разбор логируются через `LOG_MODULE(ERROR)`.
- `checkD2Error` удалён из `Common/src/Util.{hpp,cpp}` — детекция D2-ошибки (маркер `0x7F`) встроена в разбор; регион ошибки = 3 байта `[ecuId, 0x7F, requestId[0]]` (эхо номера сервиса восстановлено, полного эха requestId нет — см. Шаг 7), `D2Error` бросается сразу после первого кадра.
- Итоговая стоимость против версии с `consumeData`: один `memmove` эхо-префикса (единицы байт) на весь запрос вместо ведения offset-границы в каждом кадре — плоский цикл без лямбды.

**Критерий:** цикл читается как state machine без флагов; успешные сценарии (single, серия 2/3/N кадров, эхо с rest) ведут себя как раньше; длинное эхо собирается по кадрам серии.

---

### Шаг 7 (✓ выполнен): Формат ошибки — уточнён по железу, регион 3 байта (эхо номера сервиса)

**Файлы:** `Common/src/Util.cpp` (удалён `checkD2Error`), `Common/src/protocols/D2Request.cpp`, `Common/test/D2RequestTest.cpp:48-55`, `docs/tech_specs/d2_protocol_implementation.md:190-204`

1. Ground truth получен по результатам debug-run (`f2dfb04` + последующие правки): реальный отрицательный ответ D2 — маркер `0x7F` на `data[2]` первого фрейма, после него эхом идёт номер сервиса `requestId[0]`, затем код ошибки. **Полного эха requestId в ответе нет** (только номер сервиса). Ранее предполагавшийся framed-формат (`size + 3` байта региона, код — последний байт) не подтвердился: при длинном requestId разбор ждал несуществующий регион → таймаут вместо `D2Error`.
2. Итоговый формат: `[0]=header, [1]=ecuId, [2]=0x7F, [3]=requestId[0], [4]=код` — регион ошибки = 3 байта потока `[ecuId, 0x7F, requestId[0]]` (проверка номера сервиса восстановлена), `echoRegionSize = isError ? 3 : requestIdSize + 1`.
3. `checkD2Error` (однофреймовый контракт, guard `dataSize >= size + 4` — не детектировал ошибку при `size > 4`) **удалён** из `Util.{hpp,cpp}`; детекция встроена в `D2Request::process` — маркер `0x7F` на `data[2]`.
4. Throw: `if (isError && result.size() >= echoRegionSize) throw D2Error(result.size() > echoRegionSize ? result[echoRegionSize] : 0)` — как только собран регион (сразу после первого кадра). Код исключения = `result[3]` (код ошибки ЭБУ после эха номера сервиса); если байт кода отсутствует (кадр без кода) → `D2Error(0)`. Guard'ы `result.size() >= 3` / `> 3` исключают OOB на коротких кадрах; зависимость от длины requestId отсутствует.
5. Тесты `ErrorResponse` / `ErrorWrongEcuIdInError` / `ErrorResponseCorrectCode` / `ErrorResponseLongRequestId` и хелпер `makeErrorResponse` приведены к новому формату (эхо только номера сервиса, код = `result[3]`); добавлен `ErrorResponseWithoutCode` (кадр ошибки без байта кода → `D2Error(0)`).

**Критерий:** детекция ошибки и `D2Error` работают при любой длине requestId и на коротких кадрах — без таймаута и OOB; спека (`d2_protocol_implementation.md` «Формат ошибки») синхронизирована с impl.

---

### Шаг 8 (✓ выполнен): Тесты

**Файл:** `Common/test/D2RequestTest.cpp`

Новые кейсы (стиль существующих, `BOOST_*`):

| № | Кейс | Ожидание |
|---|---|---|
| 1 | Серия > 8 фреймов (wrap seriesId `0x0F→0x08→0x09`) | данные собраны полностью |
| 2 | Потерянный кадр (seriesId 0x0A вместо 0x09) | `std::runtime_error` |
| 3 | Дублированный кадр (0x09, 0x09) | `std::runtime_error` |
| 4 | Серия без финального кадра (до лимита) | `std::runtime_error` |
| 5 | Ответ > maxResponseSize / > maxFrameCount | `std::runtime_error` |
| 6 | Пустой requestId | `std::invalid_argument` |
| 7 | Длинный requestId (9 байт): эхо выходит за первый кадр | данные собраны полностью |
| 8 | Эхо заканчивается ровно на границе кадра | данные собраны полностью |
| 9 | single-frame ответ без полного эха | `std::runtime_error` |
| 10 | Серия закончилась до завершения эха | `std::runtime_error` |
| 11 | Ошибка с длинным requestId — эхо только номера сервиса, регион 3 байта | `D2Error` с корректным кодом |
| 12 | Ответ с CAN ID ≠ 0xFFFFE (валидный маркер) | данные разобраны (ID не проверяется) |
| 13 | Первый фрейм с `header & 0x80 == 0` после совпавшего маркера | `std::runtime_error` |
| 14 | `ErrorResponse` (после Шага 7) | `D2Error` с корректным кодом |
| 15 | Ошибка без байта кода (короткий кадр `[ecuId][0x7F][requestId[0]]`) | `D2Error` с кодом 0 |

**Критерий:** все кейсы таблицы покрыты и зелёные при `-DBUILD_TESTS=ON`.

---

### Шаг 9 (✓ выполнен): Обновление документации

**Файлы:** `docs/tech_specs/d2_protocol_implementation.md`, `README.md`, `AGENTS.md`

- Спека: секции «Формат ответа» / «Формат ошибки» — поток данных ответа, сборка эха по кадрам, ограничения (лимиты серии), семантика throw/continue.
- Списки tech-specs в `README.md` и `AGENTS.md` — добавить новый документ (станет 12).

**Критерий:** спека не противоречит коду; списки документов актуальны.

---

## 5. Все файлы

| № | Файл | Шаг |
|---|---|---|
| 1 | `Common/src/protocols/D2Request.cpp` | 1, 2, 3, 4, 5, 6, 7 |
| 2 | `Common/src/Util.cpp` / `Common/common/Util.hpp` | 7 (удалён `checkD2Error`) |
| 3 | `Common/test/D2RequestTest.cpp` | 7, 8 |
| 4 | `Flasher/test/MockICanChannel.hpp` | 8 (приведение к интерфейсу) |
| 5 | `docs/tech_specs/d2_protocol_implementation.md` | 7, 9 |
| 6 | `docs/tech_specs/d2_request_parser_hardening.md` | — (этот документ) |
| 7 | `README.md` | 9 (без изменений — списка спеки нет) |
| 8 | `AGENTS.md` | 9 |

---

## 6. Критерии готовности

1. ✓ Пустой requestId → `std::invalid_argument` без UB
2. ✓ requestId любой длины — эхо собирается по кадрам серии (ограничений нет)
3. ✓ Превышение `maxResponseSize` / `maxFrameCount` → `std::runtime_error`
4. ✓ Неправильный seriesId / потерянный / дублированный кадр → `std::runtime_error`
5. ✓ Первый фрейм без `0x80` или вне `0x88..0x8F`/`0xC8..0xCF` после совпадения маркера → `std::runtime_error`
6. ✓ Кадр < 3 байт и несовпавший маркер/эхо → continue (фильтрация трафика сохранена); CAN ID ответа не проверяется (реальные ответы ≠ 0xFFFFE)
7. ✓ Формат ошибки уточнён по железу: регион 3 байта `[ecuId, 0x7F, requestId[0]]`, `D2Error(result[3])` бросается сразу после первого кадра — без таймаута и OOB при любой длине requestId
8. ✓ Серия > 8 кадров (wrap seriesId) парсится корректно
9. ✓ Тесты Шага 8 покрывают все кейсы; старые тесты зелёные (`-DBUILD_TESTS=ON`)
10. ✓ Успешные сценарии: single-frame, multi-frame 2/3/N кадров — поведение идентично текущему
11. ✓ Публичный API `D2Request` не изменён (сигнатура `process()` прежняя)
12. ✓ Сборка 0 ошибок (Release, x64; x86 — по CI)

## 7. Примечания по реализации

1. **Валидация requestId** (пустой) выполняется во всех 4 конструкторах `D2Request`, а не в `process()` — это покрывает все пути создания запроса и гарантирует fail-fast до отправки. Ограничение длины ≤ 5 **снято**: эхо накапливается по кадрам серии.
2. **Формат ошибки** (уточнён по железу, debug-run): реальный отрицательный ответ — маркер `0x7F` на `data[2]` первого фрейма + эхо номера сервиса `requestId[0]` + код ошибки; полного эха requestId нет. Framed-формат (`size + 3` байта, код в конце региона) отвергнут — старый разбор ждал несуществующий регион и уходил в таймаут при длинном requestId. Итог: `echoRegionSize = isError ? 3 : requestIdSize + 1`, `throw D2Error(result.size() > echoRegionSize ? result[echoRegionSize] : 0)` (код = `result[3]`, при отсутствии байта кода → 0). `checkD2Error` удалён; спека и тесты приведены к impl (хелпер `makeErrorResponse` + тесты `ErrorResponse*`, добавлен `ErrorResponseWithoutCode`).
3. **MockICanChannel.hpp** приведён к текущему интерфейсу `ICanChannel` (добавлена 3-параметрическая `receive(vector&, size_t, unsigned long)`) — без этого тесты не компилировались (проект тестов не собирался с момента введения батч-receive).
4. **Тест MultiFrameResponse2Frames**: исправлен header последнего фрейма `0x49` → `0x4A` (0x49 заявляет 1 байт данных при 2 переданных — несоответствие в старом тесте, который не исполнялся).
5. **Тест LongRequestIdEcho** заменён на `LongRequestIdEchoSpansFrames` (9 байт, эхо в двух кадрах), `LongRequestIdEchoBoundary` (граница кадра), `FiveByteRequestIdWorks` (граничный случай) и `ErrorResponseLongRequestId` (ошибка при длинном requestId: эхо только номера сервиса, код = `result[3]`).
6. **Упрощение разбора (финальная итерация)**: лямбда `consumeData` с арифметикой границы «эхо/payload» удалена — весь поток ответа копится в `result`, эхо-префикс валидируется инкрементально и срезается одним `result.erase` перед возвратом (move, без копий). Один общий блок «append + валидация + лимит» вместо трёх вызовов; завершение серии объединено через флаг `endSeries`. Поведение не изменилось: 62 теста CommonTests + 11 FlasherTests зелёные.
7. **CAN ID ответа не проверяется** (итог): чек `response.id != D2Message::CanId` (0xFFFFE), добавленный в рамках харденинга, **отменён по результатам железа** — ответы D2 приходят с CAN ID ≠ 0xFFFFE (своим ID ЭБУ), чек блокировал реальный трафик (таймаут). Тесты не ловили проблему, т.к. mock отдавал кадры с id 0xFFFFE. J2534 PASS_FILTER канала всепроходной (`Util.cpp:250-252`), фильтрация — только протокольная (header/маркер/эхо/seriesId). Тест `WrongCanIdSkip` переписан в `DifferentResponseIdAccepted`. Пустой кадр логируется `LOG_MODULE(ERROR)` с дампом (по решению — не понижать до DEBUG).
7. **Длинное эхо в бою**: `D2Messages::createReadTCMTF80DataByAddr` переведён на requestId `{0xB4, 0x21, 0x34, addr(4)}` (8 байт, двухкадровый запрос, `size` — в params) — эхо ответа (9 байт) собирается разбором по кадрам серии; ручные сдвиги в `D2ReaderTF80` (`additionalShift = 4`) и TF80-логировании (`erase(4)`) удалены. В `D2ReaderTF80` добавлены защитные правки: пустой ответ → `std::runtime_error` (иначе `j += 0` — бесконечный цикл), вставка ограничена `min(requestSize, response.size())`. **Проверено на железе:** TF80 действительно эхом возвращает полный requestId (addr) — чтение памяти и прошивки TF80 выполнено успешно.
