# AUX HVAC Protocol Library for ESP32

Проект для управления кондиционерами с протоколом AUX через UART. Протестировано на **ESP32-C3** с кондиционером **Energolux Lausanne SAS12L4-A**, но должно работать с любыми кондиционерами, использующими протокол AUX.

## Подключение

Кондиционеры AUX используют UART на **4800 бод, 8E1** (8 бит данных, четность Even, 1 стоп-бит).

| ESP32-C3 | Кондиционер (AUX) |
|----------|-------------------|
| GPIO RX  | TX кондиционера   |
| GPIO TX  | RX кондиционера   |
| GND      | GND               |

> ⚠️ **Важно:** Уровень сигналов обычно 3.3V или 5V. Проверьте спецификацию вашего кондиционера. При необходимости используйте level shifter.

## Установка

Скопируйте файлы `AuxAC.h` и `AuxAC.cpp` в папку вашего проекта или добавьте как библиотеку в PlatformIO/Arduino IDE.

## Использование

```cpp
#include "AuxAC.h"

// Назначение GPIO для ESP32-C3 Super Mini
// Используйте GPIO 4 (RX) и GPIO 5 (TX) через конвертер уровней 3.3V <-> 5V!
#define AC_RX_PIN 4
#define AC_TX_PIN 5

// Создаем объект, указывая Serial порт
AuxAC ac(Serial1);

void setup() {
    Serial.begin(115200);
    
    // Инициализация UART для связи с кондиционером
    // RX pin, TX pin
    ac.begin(AC_RX_PIN, AC_TX_PIN);  // измените пины под вашу плату
}

void loop() {
    // Обрабатываем входящие пакеты (вызывать часто!)
    bool hasChanges = ac.handle();
    
    if (hasChanges) {
        ACState state = ac.getState();
        Serial.printf("Temp: %.1f°C, Mode: %d, Power: %s\n", 
                     state.indoorTemp, 
                     (int)state.mode, 
                     state.power ? "ON" : "OFF");
    }
    
    // Пример управления
    // ac.setPower(true);
    // ac.setMode(ACMode::MODE_COOL);
    // ac.setTemperature(22);
    // ac.setFan(ACFan::FAN_AUTO);
    
    delay(100);
}
```

## API

### Управление

```cpp
ac.setPower(bool on);           // Вкл/выкл
ac.setMode(ACMode mode);        // Режим: AUTO, COOL, DRY, HEAT, FAN
ac.setTemperature(uint8_t temp);// Температура 16-32°C
ac.setFan(ACFan speed);         // Скорость вентилятора: AUTO, LOW, MEDIUM, HIGH
ac.setTurbo(bool on);           // Турбо режим
ac.setHealth(bool on);          // Ионизатор/здоровье
ac.setSleep(bool on);           // Ночной режим
ac.setDisplay(bool on);         // Вкл/выкл дисплей
```

### Получение состояния

```cpp
ACState state = ac.getState();

struct ACState {
    bool power;                 // Питание
    ACMode mode;                // Режим работы
    ACFan fan;                  // Скорость вентилятора
    uint8_t targetTemp;         // Целевая температура
    bool turbo;                 // Турбо
    bool health;                // HEALTH/ионизатор
    bool sleep;                 // SLEEP режим
    bool displayOn;             // Дисплей включен
    float indoorTemp;           // Температура внутри
    float outdoorTemp;          // Температура снаружи
    uint8_t compressorPower;    // Мощность компрессора 0-100%
};
```

### Обработчик

```cpp
bool hasChanges = ac.handle();  // Вызывать в loop(), возвращает true если состояние изменилось
```

## Примечания

- Библиотека автоматически отправляет PING каждые 3 секунды для поддержания соединения
- Все команды отправляются немедленно при вызове методов управления
- Состояние обновляется при получении пакетов от кондиционера

## Лицензия

MIT