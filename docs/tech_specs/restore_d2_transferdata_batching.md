# ТЗ: Восстановление пакетной отправки данных в D2ProtocolCommonSteps::transferData

## 1. Цель

Восстановить скорость прошивки D2-ECU после рефакторинга. Устранить 4-кратное падение производительности, вызванное отправкой каждого CAN-фрейма отдельным вызовом J2534 API вместо пакетной отправки.

## 2. Диагностика

### 2.1. Текущее узкое место

`D2ProtocolCommonSteps::transferData()` (`Common/src/protocols/D2ProtocolCommonSteps.cpp:192-219`):

```
writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
for (const auto& frame : frames) {          // ← каждый фрейм по отдельности
    channel.clearRx();
    if (!channel.send(frame)) { ... }       // ← один PassThruWriteMsgs на фрейм
    progressCallback(6);
}
writeDataOffsetAndCheckAnswer(channel, ecuId, chunk.writeOffset);
```

Цикл отправляет каждый 8-байтовый CAN-фрейм индивидуальным `PassThruWriteMsgs`. Для прошивки типичного ECM (1-4 MB) это десятки тысяч API-вызовов — J2534-устройство/USB не успевает.

### 2.2. Как было до рефакторинга (commit `50f689a`)

На уровне формирования сообщений — `D2Messages::createWriteDataMsgs()`:

```cpp
const auto MaxMessagesPerMessage = 10;   // до 10 payload-ов на D2Message
std::vector<D2Message> result;
std::vector<D2Message::DataType> resultPayload;

for (size_t i = beginOffset; i < endOffset; i += chunkSize) {
    // формируем 8-байтовый payload: ecuId + 0xA8+size + data
    resultPayload.emplace_back(std::move(payload));
    if (resultPayload.size() >= MaxMessagesPerMessage) {
        result.emplace_back(D2Message(std::move(resultPayload)));
        resultPayload.clear();           // ← граница батча
    }
}
// терминирующий фрейм (0xA8) — в последний resultPayload
resultPayload.emplace_back(terminator);
result.emplace_back(D2Message(std::move(resultPayload)));
```

На уровне отправки:

```cpp
for (const auto binMsg : binMsgs) {
    channel.clearRx();
    const auto passThruMsgs = binMsg.toPassThruMsgs(...);
    unsigned long msgsNum = passThruMsgs.size();   // кол-во PASSTHRU_MSG в батче
    channel.writeMsgs(passThruMsgs, msgsNum, 50000); // один API-вызов на батч
    progressCallback(6 * msgsNum);
}
```

**Один `writeMsgs` содержал несколько `PASSTHRU_MSG`** — J2534-устройство отправляло их на шину последовательно без межкадрового API-зазора.

### 2.3. Что сломалось при рефакторинге

Новая `createWriteDataFrames()` (`D2ProtocolCommonSteps.cpp:117-151`) строит те же группы по 10 payload-ов, но **схлопывает всё в плоский `std::vector<CanFrame>`**:

```cpp
std::vector<CanFrame> result;   // ← плоский список, границы батчей потеряны
// ...
result.emplace_back(D2_CAN_ID, std::move(p), true);  // все фреймы подряд
// ...
return result;
```

Цикл отправки не знает, где были границы, и отправляет по одному.

## 3. Структура батча (спецификация)

Каждый батч содержит **ровно 10 или менее data-фреймов** — без терминирующего фрейма.

Правила группировки:

1. Data-фреймы накапливаются до 10 включительно.
2. При достижении 10 — сформированный батч отправляется.
3. После цикла — оставшиеся data-фреймы (1-9) формируют последний батч.
4. Если data-фреймов ровно 0 — батчей нет (не должно возникать, но на всякий случай не отправлять).

> Терминирующий фрейм `0xA8` (без данных), присутствовавший в старой реализации, **не нужен** — ECU корректно обрабатывает данные без него. Убран для упрощения и ускорения.

## 4. Требования к реализации

### 4.1. Изменить `createWriteDataFrames`

Изменить возвращаемый тип:

```
std::vector<std::vector<CanFrame>> createWriteDataFrames(...)
```

Каждый внутренний `std::vector<CanFrame>` — один батч (≤ 10 data + terminator в последнем).

Логика формирования:

```cpp
std::vector<std::vector<CanFrame>> result;
std::vector<CanFrame> batch;

for (/* каждый data-фрейм */) {
    batch.emplace_back(D2_CAN_ID, payload, true);
    if (batch.size() >= 10) {
        result.push_back(std::move(batch));
        batch.clear();
    }
}
if (!batch.empty()) {
    result.push_back(std::move(batch));
}
```

### 4.2. Изменить цикл отправки в `transferData`

Вместо:

```cpp
for (const auto& frame : frames) {
    channel.clearRx();
    channel.send(frame);
    progressCallback(6);
}
```

Сделать:

```cpp
for (auto& batch : batches) {
    channel.clearRx();
    if (!channel.send(batch)) {
        throw std::runtime_error("write msgs error");
    }
    progressCallback(6 * batch.size());
}
```

Прогресс начисляется за каждый data-фрейм в батче — 6 единиц за фрейм, как и в текущем покадровом цикле.

### 4.3. Очистка RX

`channel.clearRx()` — перед каждым батчем, как в старой реализации.

### 4.4. Таймаут операции отправки

В старой реализации таймаут `writeMsgs` был 50000 мс. В текущей `channel.send()` таймаут не настраивается — `J2534ChannelAdapter::send()` вызывает `writeMsgs` без указания таймаута, используется умолчательное значение **1000 мс** (см. `J2534Channel.hpp:23-30`).

При пакетной отправке до 10 CAN-фреймов за один `PassThruWriteMsgs` 1000 мс может быть недостаточно — J2534-устройство не успеет физически передать все сообщения на шину. Необходимо:

1. **Добавить параметр `timeout`** в `ICanChannel::send()`:

```cpp
virtual bool send(const CanFrame& frame, unsigned long timeout = 1000) = 0;
virtual bool send(const std::vector<CanFrame>& frames, unsigned long timeout = 1000) = 0;
```

Значение по умолчанию `1000` сохраняет обратную совместимость с существующими вызовами.

2. **Обновить `J2534ChannelAdapter::send()`** — передавать `timeout` в `_channel->writeMsgs()`: `writeMsgs(msgs, numMsgs, timeout)`.

3. **В `transferData` передавать таймаут 50000 мс** для пакетной отправки (как было в старой реализации):

```cpp
for (auto& batch : batches) {
    channel.clearRx();
    if (!channel.send(batch, 50000)) {   // ← таймаут 50 сек
        throw std::runtime_error("write msgs error");
    }
    progressCallback(6 * batch.size());
}
```

## 5. Изменяемые файлы

| № | Файл | Изменение |
|---|---|---|
| 1 | `Common/common/ICanChannel.hpp` | Добавить `unsigned long timeout = 1000` в оба `send()` |
| 2 | `Common/common/J2534ChannelAdapter.hpp` | Обновить объявления `send()` |
| 3 | `Common/src/J2534ChannelAdapter.cpp` | Передавать `timeout` в `writeMsgs()` |
| 4 | `Common/src/protocols/D2ProtocolCommonSteps.cpp` | `createWriteDataFrames` — тип возврата, `transferData` — цикл отправки с таймаутом |

> **Примечание:** Если существуют другие реализации `ICanChannel` (ELM327, ESP32, STM32), их `send()` также нужно обновить — добавить параметр `timeout` с игнорированием или использованием по необходимости.

## 6. Оценка эффекта

Типичная прошивка ECM: 1-4 MB данных, ~180K-700K CAN-фреймов.

| Режим | API-вызовов | Оценка времени (1 MB) |
|---|---|---|
| Текущий (пофреймовый) | ~175000 | ~175 сек (3 мин) |
| Пакетный (батчи по 10) | ~16000 (с терминаторами) | ~40 сек |

Ожидаемое ускорение: **~4x**.

## 7. Критерии готовности

1. ✓ `createWriteDataFrames` возвращает `std::vector<std::vector<CanFrame>>` с корректными границами батчей
2. ✓ Каждый батч содержит ≤ 10 data-фреймов, терминирующий фрейм отсутствует
3. ✓ `clearRx()` вызывается перед каждым батчем
4. ✓ Прогресс-колбэк вызывается единожды на батч с суммой 6 за каждый фрейм
5. ✓ `ICanChannel::send()` принимает `unsigned long timeout = 1000`
6. ✓ `J2534ChannelAdapter::send()` передаёт `timeout` в `writeMsgs()`
7. ✓ В `transferData` для пакетной отправки указан таймаут 50000 мс
8. ✓ Сборка: 0 ошибок, 0 warnings
9. ✓ Фактическая скорость прошивки D2-ECU восстановлена до уровня до рефакторинга
