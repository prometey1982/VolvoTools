# ISOTP Transmitter FSM (HFSM2)

## Состояния конечного автомата передатчика

| Состояние | Описание |
|-----------|----------|
| `Idle` | Ожидание команды отправки |
| `SendSingleFrame` | Отправка SF (короткое сообщение ≤ 7 байт) |
| `SendFirstFrame` | Отправка FF (первый кадр длинного сообщения) |
| `WaitFlowControl` | Ожидание FC от получателя |
| `SendConsecutiveFrame` | Отправка очередного CF, учёт BlockSize и STmin |
| `TxCompleted` | Отправка завершена успешно |
| `TxError` | Аварийное состояние (таймаут, исключение, неверный FC) |

## Диаграмма переходов

```mermaid
stateDiagram-v2
    [*] --> Idle : initial entry
    
    Idle --> SendSingleFrame : SendRequest && size <= 7
    Idle --> SendFirstFrame : SendRequest && size > 7
    
    SendSingleFrame --> TxCompleted : frame sent
    
    SendFirstFrame --> WaitFlowControl : FF sent
    
    WaitFlowControl --> SendConsecutiveFrame : RxFrame(FC, CTS)
    WaitFlowControl --> WaitFlowControl : RxFrame(FC, WAIT), restart timer
    WaitFlowControl --> TxError : RxFrame(FC, OVERFLOW)
    WaitFlowControl --> TxError : Timeout(N_Bs)
    
    SendConsecutiveFrame --> SendConsecutiveFrame : TxConfirmation OK && more data && block not full
    SendConsecutiveFrame --> WaitFlowControl : TxConfirmation OK && blockSize > 0 && blockCounter >= blockSize
    SendConsecutiveFrame --> TxCompleted : TxConfirmation OK && all data sent
    SendConsecutiveFrame --> TxError : TxConfirmation FAIL
    SendConsecutiveFrame --> TxError : Timeout(N_Cs)
    SendConsecutiveFrame --> WaitFlowControl : ReadyToSend after STmin
    
    TxCompleted --> Idle : auto
    
    Idle --> Idle : Reset
    WaitFlowControl --> Idle : Reset
    SendConsecutiveFrame --> Idle : Reset
    TxError --> Idle : Reset
    TxError --> TxError : any other event
```

## Легенда событий

| Событие | Описание |
|---------|----------|
| `SendRequest` | Пользователь вызывает `send()` |
| `RxFrame(FC)` | Получен кадр FlowControl (CTS, WAIT, OVERFLOW) |
| `TxConfirmation` | Подтверждение отправки CAN-кадра (success/fail) |
| `Timeout(N_Bs)` | Истекло время ожидания FC |
| `Timeout(N_Cs)` | Истекло время между CF (STmin) |
| `ReadyToSend` | Внутреннее событие после паузы STmin |
| `Reset` | Пользователь вызывает `reset()` |