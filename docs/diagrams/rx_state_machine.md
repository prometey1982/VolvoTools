# ISOTP Receiver FSM (HFSM2)

## Состояния конечного автомата приёмника

| Состояние | Описание |
|-----------|----------|
| `RxIdle` | Ожидание первого кадра (SF или FF) |
| `ReceiveSF` | Получен SF – немедленно вызывается `onReceived` |
| `ReceiveFF` | Получен FF – отправляется FC, запускается сборка |
| `SendFC` | Повторная отправка Flow Control (при необходимости) |
| `ReceiveCF` | Ожидание и сборка Consecutive Frame (CF) |
| `RxCompleted` | Сообщение полностью собрано |
| `RxError` | Аварийное состояние (ошибка протокола, таймаут) |

## Диаграмма переходов

```mermaid
stateDiagram-v2
    [*] --> RxIdle : initial entry
    
    RxIdle --> ReceiveSF : RxFrame(SF)
    RxIdle --> ReceiveFF : RxFrame(FF)
    RxIdle --> RxError : RxFrame(other)
    
    ReceiveSF --> RxCompleted : store data, call onReceived
    
    ReceiveFF --> ReceiveCF : Send FC(CTS, BS, STmin), start N_Ar timer
    ReceiveFF --> RxError : BufferOverflow (message too large)
    
    ReceiveCF --> RxCompleted : all CF received, call onReceived
    ReceiveCF --> ReceiveCF : more CF expected (re‑enter, restart N_Cr timer)
    ReceiveCF --> RxError : InvalidSequenceNumber
    ReceiveCF --> RxError : Timeout(N_Cr)
    ReceiveCF --> RxError : RxFrame(other)
    
    SendFC --> ReceiveCF : resend FC and return to waiting CF
    
    RxCompleted --> RxIdle : auto
    
    RxIdle --> RxIdle : Reset
    ReceiveFF --> RxIdle : Reset
    ReceiveCF --> RxIdle : Reset
    RxError --> RxIdle : Reset
    RxError --> RxError : any other event
```

## Легенда событий

| Событие | Описание |
|---------|----------|
| `RxFrame(SF)` | Получен Single Frame |
| `RxFrame(FF)` | Получен First Frame |
| `RxFrame(CF)` | Получен Consecutive Frame |
| `Timeout(N_Ar)` | Истекло время ожидания первого CF |
| `Timeout(N_Cr)` | Истекло время между CF |
| `Reset` | Пользователь вызывает `reset()` |