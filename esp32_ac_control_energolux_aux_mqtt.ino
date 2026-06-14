// main.ino
#include <mybase_esp32_OTA_webserial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "AuxAC.h"

const char* ssid = "DD";
const char* password = "J42ghaQY";
// Пароль для OTA: 1234

// Назначение GPIO для ESP32-C3 Super Mini
// Используйте GPIO 4 (RX) и GPIO 5 (TX) через конвертер уровней 3.3V <-> 5V!
#define AC_RX_PIN 4
#define AC_TX_PIN 5

// На юсб vcc кондея 15 вольт! Надо понижать.

AuxAC ac(Serial1);  // Используем второй аппаратный UART

// Настройки MQTT
const char* mqttServer = "192.168.3.6";
const int mqttPort = 1883;  // стандартный порт MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// MQTT Топики
const char* topic_status_json = "/home/energolux/ac/status";
const char* topic_get_request = "/home/energolux/ac/get";
const char* topic_cmd_power = "/home/energolux/ac/cmd/power";
const char* topic_cmd_temp = "/home/energolux/ac/cmd/temp";
const char* topic_cmd_mode = "/home/energolux/ac/cmd/mode";
const char* topic_cmd_fan = "/home/energolux/ac/cmd/fan";
const char* topic_cmd_errors = "/home/energolux/ac/cmd/errors";


void setup() {
  Serial.begin(115200);

  Serial.println("Booting...");
  pinMode(ledPin, OUTPUT);

  setupWiFi(ssid, password);
  setupOTA("esp32c3-ac-control-energolux-aux-mqtt", "1234");

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Инициализация класса кондиционера
  ac.begin(AC_RX_PIN, AC_TX_PIN);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  ArduinoOTA.handle();  // OTA обязателен
  // handleBlink();        // Мигание LED
  webSerial.loop();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Опрашиваем UART кондиционера. Если данные изменились — шлем JSON
  if (ac.handle()) {
    webSerial.print("Состояние изменилось, отправка в MQTT...");
    publishStateJson();
  };

  // Чтение из Serial и отправка в WebSerial
  // if (Serial.available() > 0) {
  //   String serialMsg = Serial.readString();  // Читаем данные из Serial
  //   webSerial.print(serialMsg);              // Перенаправляем в WebSerial
  // }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Подключение к MQTT...");
    if (mqttClient.connect("ESP32C3_Energolux_AUX")) {
      Serial.println("Успешно подключено!");
      // Подписываемся на топики управления
      mqttClient.subscribe(topic_cmd_power);
      mqttClient.subscribe(topic_cmd_temp);
      mqttClient.subscribe(topic_cmd_mode);
      mqttClient.subscribe(topic_get_request);
    } else {
      Serial.print("Ошибка, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Пробуем снова через 5 секунд...");
      delay(5000);
    }
  }
}

// Обработка входящих команд из MQTT
void mqttCallback(char* topic_ch, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  String topic = String(topic_ch);

  webSerial.print(">input mqtt [" + topic + "]: " + message);

  if (topic == topic_get_request) {
    // Принудительный запрос JSON-статуса
    publishStateJson();
    return;
  }

  auto oldValues = ac.getState();

  if (topic == topic_cmd_power) {
    if (message == "ON" || message == "1")
      ac.setPower(true);
    else if (message == "OFF" || message == "0")
      ac.setPower(true);
    else
      mqttClient.publish(topic_cmd_errors, "Unknown power value", true);

  } else if (topic == topic_cmd_temp) {
    int temp = message.toInt();
    if (temp >= 16 && temp <= 32) ac.setTemperature(temp);
  } else if (topic == topic_cmd_mode) {
    if (message == "cool") ac.setMode(ACMode::MODE_COOL);
    else if (message == "heat") ac.setMode(ACMode::MODE_HEAT);
    else if (message == "dry") ac.setMode(ACMode::MODE_DRY);
    else if (message == "fan") ac.setMode(ACMode::MODE_FAN);
    else if (message == "auto") ac.setMode(ACMode::MODE_AUTO);
  } else if (topic == topic_cmd_fan) {
    if (message == "low") ac.setFan(ACFan::FAN_LOW);
    else if (message == "medium") ac.setFan(ACFan::FAN_MEDIUM);
    else if (message == "high") ac.setFan(ACFan::FAN_HIGH);
    else if (message == "auto") ac.setFan(ACFan::FAN_AUTO);
  } else {
    mqttClient.publish(topic_cmd_errors, ("Unknown cmd: " + message).c_str(), true);
    return;
  }

  if (oldValues != ac.getState()) {
    //что что изменили
    publishStateJson();
  }
}

String modeToString(ACMode mode) {
  switch (mode) {
    case ACMode::MODE_COOL: return "cool";
    case ACMode::MODE_HEAT: return "heat";
    case ACMode::MODE_DRY: return "dry";
    case ACMode::MODE_FAN: return "fan";
    default: return "auto";
  }
}

String fanToString(ACFan fan) {
  switch (fan) {
    case ACFan::FAN_LOW: return "low";
    case ACFan::FAN_MEDIUM: return "medium";
    case ACFan::FAN_HIGH: return "high";
    default: return "auto";
  }
}

// Функция отправки состояния в MQTT в формате JSON
void publishStateJson() {
  ACState state = ac.getState();
  JsonDocument doc;

  doc["power"] = state.power ? "ON" : "OFF";
  doc["mode"] = modeToString(state.mode);
  doc["fan_speed"] = fanToString(state.fan);
  doc["target_temp"] = state.targetTemp;
  doc["indoor_temp"] = state.indoorTemp;

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(topic_status_json, buffer, true);  // true - флаг Retain
}