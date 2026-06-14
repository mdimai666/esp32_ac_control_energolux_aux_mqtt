#pragma once
#include <Arduino.h>

// https://github.com/GrKoR/AUX_HVAC_Protocol

enum class ACMode : uint8_t {
  MODE_AUTO = 0,
  MODE_COOL = 1,
  MODE_DRY = 2,
  MODE_HEAT = 3,
  MODE_FAN = 4
};

enum class ACFan : uint8_t {
  FAN_AUTO = 0,
  FAN_LOW = 1,
  FAN_MEDIUM = 2,
  FAN_HIGH = 3
};

struct ACState {
  bool power = false;
  ACMode mode = ACMode::MODE_AUTO;
  ACFan fan = ACFan::FAN_AUTO;
  uint8_t targetTemp = 0;  // 16-32°C
  bool turbo = false;       // TURBO режим
  bool health = false;      // HEALTH (ионизатор)
  bool sleep = false;       // SLEEP режим
  bool displayOn = true;    // состояние дисплея
  float indoorTemp = 0.0f;
  float outdoorTemp = 0.0f;
  int16_t outdoorTempRaw = 0;
  uint8_t compressorPower = 0;  // 0-100%

  // Оператор равенства
  bool operator==(const ACState& other) const {
    return power == other.power && mode == other.mode && fan == other.fan && targetTemp == other.targetTemp && turbo == other.turbo && health == other.health && sleep == other.sleep && displayOn == other.displayOn && fabs(indoorTemp - other.indoorTemp) < 0.01f &&  // для float
           fabs(outdoorTemp - other.outdoorTemp) < 0.01f &&                                                                                                                                                                                                              // для float
           outdoorTempRaw == other.outdoorTempRaw && compressorPower == other.compressorPower;
  }

  // Оператор неравенства
  bool operator!=(const ACState& other) const {
    return !(*this == other);
  }
};

class AuxAC {
private:
  HardwareSerial* _serial;
  ACState _state;
  uint8_t _txBuf[34];  // максимум 34 байта
  uint8_t _rxBuf[34];
  uint8_t _requestCounter = 0;
  unsigned long _lastPoll = 0;
  const unsigned long POLL_INTERVAL = 3000;

  // CRC16 для AUX (как описано в документации)
  uint16_t calculateCRC16(uint8_t* data, uint8_t len) {
    uint32_t sum = 0;
    // Если длина нечетная, дополняем нулем
    uint8_t paddedLen = len + (len % 2);

    for (uint8_t i = 0; i < paddedLen; i += 2) {
      uint16_t word = (i < len) ? data[i] : 0;
      word <<= 8;
      word |= (i + 1 < len) ? data[i + 1] : 0;
      sum += word;
    }

    // Складываем старшие и младшие 16 бит
    while (sum >> 16) {
      sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Побитовая инверсия
    return ~sum & 0xFFFF;
  }

  // Построение заголовка пакета
  void buildHeader(uint8_t type, uint8_t bodyLen, bool isOutgoing) {
    memset(_txBuf, 0, sizeof(_txBuf));
    _txBuf[0] = 0xBB;                      // START
    _txBuf[1] = 0x00;                      // неизвестно
    _txBuf[2] = type;                      // TYPE
    _txBuf[3] = isOutgoing ? 0x80 : 0x00;  // wifi? (0x80 для исходящих)
    _txBuf[4] = isOutgoing ? 0x01 : 0x00;  // для исходящих 0x01
    _txBuf[5] = 0x00;
    _txBuf[6] = bodyLen;  // LEN
    _txBuf[7] = 0x00;
  }

  void sendPacket(uint8_t len) {
    uint16_t crc = calculateCRC16(_txBuf, len - 2);
    _txBuf[len - 2] = (crc >> 8) & 0xFF;  // CRC1 (старший)
    _txBuf[len - 1] = crc & 0xFF;         // CRC2 (младший)
    _serial->write(_txBuf, len);

    // Отладка
    Serial.print("SEND: ");
    for (uint8_t i = 0; i < len; i++) {
      Serial.printf("%02X ", _txBuf[i]);
    }
    Serial.println();
  }

  // Отправка PING (обязательно для поддержания связи)
  void sendPing() {
    buildHeader(0x01, 8, true);
    // Тело ping-пакета (8 байт) - фиксированное
    _txBuf[8] = 0x1C;
    _txBuf[9] = 0x27;
    _txBuf[10] = 0x00;
    _txBuf[11] = 0x00;
    _txBuf[12] = 0x00;
    _txBuf[13] = 0x00;
    _txBuf[14] = 0x00;
    _txBuf[15] = 0x00;
    sendPacket(18);  // 8 заголовок + 8 тело + 2 CRC = 18
  }

  // Запрос статуса (TYPE=0x06, CMD=0x11 - статус внутреннего блока)
  void requestStatus() {
    buildHeader(0x06, 2, true);
    _txBuf[8] = 0x11;               // CMD = запрос статуса внутреннего блока
    _txBuf[9] = ++_requestCounter;  // счетчик запросов
    sendPacket(12);                 // 8 заголовок + 2 тело + 2 CRC = 12
  }

  // Формирование команды управления (TYPE=0x06, CMD=0x01)
  void sendControlCommand() {
    buildHeader(0x06, 15, true);  // тело 15 байт

    // Байты тела для CMD=0x01 (управление)
    _txBuf[8] = 0x01;               // CMD = управление
    _txBuf[9] = ++_requestCounter;  // счетчик запросов

    // Далее идет тело как в TYPE=0x07 CMD=0x11 (статус внутреннего блока)
    // Байт 10: TS (целевая температура + вертикальные шторки)
    uint8_t targetTempRaw = (_state.targetTemp - 8) << 3;
    _txBuf[10] = targetTempRaw;  // пока без десятых

    // Байт 11: SL (горизонтальные шторки)
    _txBuf[11] = 0x00;  // SWING выключен

    // Байт 12: Td+TMR
    _txBuf[12] = 0x00;

    // Байт 13: SP+TH (скорость вентилятора + часы таймера)
    uint8_t fanValue = 0;
    switch (_state.fan) {
      case ACFan::FAN_AUTO: fanValue = 0xA0; break;
      case ACFan::FAN_LOW: fanValue = 0x60; break;
      case ACFan::FAN_MEDIUM: fanValue = 0x40; break;
      case ACFan::FAN_HIGH: fanValue = 0x20; break;
    }
    _txBuf[13] = fanValue;

    // Байт 14: TB+MT+TM (TURBO, MUTE, минуты таймера)
    _txBuf[14] = _state.turbo ? 0x40 : 0x00;

    // Байт 15: MO (режим работы)
    uint8_t modeValue = 0;
    switch (_state.mode) {
      case ACMode::MODE_AUTO: modeValue = 0x00; break;
      case ACMode::MODE_COOL: modeValue = 0x20; break;
      case ACMode::MODE_DRY: modeValue = 0x40; break;
      case ACMode::MODE_HEAT: modeValue = 0x80; break;
      case ACMode::MODE_FAN: modeValue = 0xC0; break;
    }
    if (_state.sleep) modeValue |= 0x04;
    _txBuf[15] = modeValue;

    // Байт 16-17: резерв
    _txBuf[16] = 0x00;
    _txBuf[17] = 0x00;

    // Байт 18: EN (включение, HEALTH, iCLEAN)
    _txBuf[18] = 0x00;
    if (_state.power) {
      _txBuf[18] |= 0x20;  // POW бит
    }
    if (_state.health) {
      _txBuf[18] |= 0x04;  // HL2 бит (HEALTH)
    }

    // Байт 19: резерв
    _txBuf[19] = 0x00;

    // Байт 20: FL (дисплей, антиплесень)
    _txBuf[20] = _state.displayOn ? 0x10 : 0x00;

    // Байт 21-22: резерв
    _txBuf[21] = 0x00;
    _txBuf[22] = 0x00;

    sendPacket(25);  // 8 заголовок + 15 тело + 2 CRC = 25
  }

  String byteToBinString(byte b) {
    String result = "";
    for (int i = 7; i >= 0; i--) {
      result += ((b >> i) & 1) ? "1" : "0";
    }
    return result;
  }

  // Парсинг входящего пакета TYPE=0x07
  bool parseStatusPacket(uint8_t* buf, uint8_t len) {
    if (buf[2] != 0x07) return false;

    uint8_t cmd = buf[9];

    if (cmd == 0x11 && len >= 25) {
      // Статус внутреннего блока (25 байт)
      ACState oldState = _state;

      // Байт 18: включение и HEALTH
      uint8_t en = buf[18];
      _state.power = (en & 0x20) != 0;
      // webSerial.print("pb=" + byteToBinString(en));
      _state.health = (en & 0x04) != 0;

      // Байт 15: режим и SLEEP
      uint8_t mo = buf[15];
      uint8_t modeRaw = (mo >> 5) & 0x07;
      switch (modeRaw) {
        case 0: _state.mode = ACMode::MODE_AUTO; break;
        case 1: _state.mode = ACMode::MODE_COOL; break;
        case 2: _state.mode = ACMode::MODE_DRY; break;
        case 4: _state.mode = ACMode::MODE_HEAT; break;
        case 6: _state.mode = ACMode::MODE_FAN; break;
      }
      _state.sleep = (mo & 0x04) != 0;

      // Байт 13: скорость вентилятора
      uint8_t sp = (buf[13] >> 5) & 0x07;
      switch (sp) {
        case 5: _state.fan = ACFan::FAN_AUTO; break;    // 0b101
        case 3: _state.fan = ACFan::FAN_LOW; break;     // 0b011
        case 2: _state.fan = ACFan::FAN_MEDIUM; break;  // 0b010
        case 1: _state.fan = ACFan::FAN_HIGH; break;    // 0b001
      }

      // Байт 10-12: целевая температура
      uint8_t targetRaw = (buf[10] >> 3) & 0x1F;
      _state.targetTemp = 8 + targetRaw;
      if (buf[12] & 0x80) _state.targetTemp += 1;  // +0.5°C

      // Байт 20: дисплей
      _state.displayOn = (buf[20] & 0x10) != 0;

      // Байт 14: TURBO
      _state.turbo = (buf[14] & 0x40) != 0;

      // Байт 15-31: комнатная температура
      // В зависимости от модели, температура может быть в разных местах
      if (len > 31) {
        uint8_t tint = buf[15];  // целая часть
        uint8_t tid = buf[31];   // дробная часть (младшие 4 бита)
        if (tint >= 0x20) {
          _state.indoorTemp = (tint - 0x20) + ((tid & 0x0F) / 10.0f);
        }
      }

      return memcmp(&oldState, &_state, sizeof(ACState)) != 0;
    }

    if (cmd == 0x21 && len >= 34) {
      // Статус внешнего блока (34 байта)
      // Байт 20: температура внешнего блока (oT)
      _state.outdoorTempRaw = buf[20];
      if (_state.outdoorTempRaw > 0x20) {
        _state.outdoorTemp = _state.outdoorTempRaw - 0x20;
      }

      // Байт 24: мощность компрессора (iPwr)
      if (buf[24] <= 100) {
        _state.compressorPower = buf[24];
      }
    }

    return false;
  }

public:
  AuxAC(HardwareSerial& uart)
    : _serial(&uart) {}

  void begin(int8_t rxPin, int8_t txPin) {
    _serial->begin(4800, SERIAL_8E1, rxPin, txPin);
  }

  // Основной обработчик UART (вызывать в loop)
  // return has changes
  bool handle() {
    // Отправляем PING каждые 3 секунды (обязательно для работы)
    if (millis() - _lastPoll >= POLL_INTERVAL) {
      _lastPoll = millis();
      sendPing();
      requestStatus();  // также запрашиваем статус
    }

    bool hasChanges = false;

    // Чтение входящих пакетов
    while (_serial->available()) {
      uint8_t b = _serial->read();
      static uint8_t buffer[34];
      static uint8_t index = 0;
      static bool inPacket = false;

      if (!inPacket && b == 0xBB) {
        inPacket = true;
        index = 0;
        buffer[index++] = b;
      } else if (inPacket) {
        buffer[index++] = b;

        // Минимальная длина: заголовок 8 + CRC 2 = 10
        if (index >= 10 && index >= (8 + buffer[6] + 2)) {
          uint8_t totalLen = 8 + buffer[6] + 2;
          if (totalLen <= sizeof(buffer)) {
            uint16_t calcCrc = calculateCRC16(buffer, totalLen - 2);
            uint16_t recvCrc = (buffer[totalLen - 2] << 8) | buffer[totalLen - 1];

            if (calcCrc == recvCrc) {
              // Отладка
              // Serial.print("RECV: ");
              // for (uint8_t i = 0; i < totalLen; i++) {
              //   Serial.printf("%02X ", buffer[i]);
              // }
              // Serial.println();

              auto oldValues = _state;

              parseStatusPacket(buffer, totalLen);

              hasChanges = (oldValues.targetTemp != 0) && (_state != oldValues);
            }
          }
          inPacket = false;
        }

        // Защита от переполнения
        if (index >= sizeof(buffer)) {
          inPacket = false;
        }
      }
    }

    return hasChanges;
  }

  // Публичные методы управления
  void setPower(bool on) {
    _state.power = on;
    sendControlCommand();
  }

  void setMode(ACMode mode) {
    _state.mode = mode;
    sendControlCommand();
  }

  void setTemperature(uint8_t temp) {
    if (temp < 16) temp = 16;
    if (temp > 32) temp = 32;
    _state.targetTemp = temp;
    sendControlCommand();
  }

  void setFan(ACFan speed) {
    _state.fan = speed;
    sendControlCommand();
  }

  void setTurbo(bool on) {
    _state.turbo = on;
    sendControlCommand();
  }

  void setHealth(bool on) {
    _state.health = on;
    sendControlCommand();
  }

  void setSleep(bool on) {
    _state.sleep = on;
    sendControlCommand();
  }

  void setDisplay(bool on) {
    _state.displayOn = on;
    sendControlCommand();
  }

  ACState getState() const {
    return _state;
  }
};