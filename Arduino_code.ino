#include <Arduino.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <DHT.h>
#include <DHT_U.h>

// Project Macros
#define WIFI_SSID             "CHENAB"
#define WIFI_PASSWORD         "44zMf3QqdU&KC3Mv"
#define INFLUXDB_URL          "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN        "JUTk-VGOuDP6kCUAC4mQDmhf-agMJq63TPdNGFRYAmIXedxC9PqxHYprklMV7p76PwQX-MbI4qLQD6tdtQ4QVQ=="
#define INFLUXDB_ORG          "fecdb5c8815b8f28"
#define INFLUXDB_BUCKET       "ESP32"

// Timezone Information
#define TZ_INFO "IST-5:30"
#define DEVICE  "ESP32"

// DHT Sensor Configuration
#define DHTPIN                  21
#define DHTTYPE                 DHT11
#define DHT11_REFRESH_TIME      5000u   // 5 seconds

// PIR Sensor Configuration
#define PIR_SENSOR_PIN          18

// Data Refresh and Transmission Intervals
#define INFLUXDB_SEND_TIME      60000u  // 1 minute
#define SENSOR_BUFFER_SIZE      (INFLUXDB_SEND_TIME / DHT11_REFRESH_TIME)

// Private Variables
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensor("Sensor_Data");
WiFiMulti wifiMulti;

DHT dht(DHTPIN, DHTTYPE);
static uint8_t temp_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static uint8_t humidity_buffer[SENSOR_BUFFER_SIZE] = { 0 };
static uint16_t sensor_buffer_idx = 0;

static uint32_t dht_refresh_timestamp = 0u;
static uint32_t influxdb_send_timestamp = 0u;

static bool pir_state = false;

// Function Prototypes
void System_Init();
void WiFi_Setup();
void DHT11_TaskInit();
void DHT11_TaskMng();
void InfluxDB_TaskInit();
void InfluxDB_TaskMng();
void ReadPIRSensor();

uint8_t Get_HumidityValue();
uint8_t Get_TemperatureValue();

void setup() {
  System_Init();
  WiFi_Setup();
  DHT11_TaskInit();
  InfluxDB_TaskInit();
  pinMode(PIR_SENSOR_PIN, INPUT); // Configure PIR sensor pin
}

void loop() {
  DHT11_TaskMng();
  InfluxDB_TaskMng();
  ReadPIRSensor();
}

// Function Definitions
void System_Init() {
  Serial.begin(115200);
}

void WiFi_Setup() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();
  while (wifiMulti.run() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
    Serial.print(".");
    delay(500);
  }

  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected.");
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }
}

void DHT11_TaskInit() {
  dht.begin();
  dht_refresh_timestamp = millis();
}

void DHT11_TaskMng() {
  uint32_t now = millis();
  if (now - dht_refresh_timestamp >= DHT11_REFRESH_TIME) {
    dht_refresh_timestamp = now;

    float humidity, temperature;
    for (int i = 0; i < 3; i++) {
      humidity = dht.readHumidity();
      temperature = dht.readTemperature();
      if (!isnan(humidity) && !isnan(temperature)) break;
      delay(100);
    }

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    Serial.printf("Humidity: %.1f%%  Temperature: %.1f°C\n", humidity, temperature);

    temp_buffer[sensor_buffer_idx] = (uint8_t)temperature;
    humidity_buffer[sensor_buffer_idx] = (uint8_t)humidity;
    sensor_buffer_idx = (sensor_buffer_idx + 1) % SENSOR_BUFFER_SIZE;
  }
}

void InfluxDB_TaskInit() {
  sensor.addTag("device", DEVICE);
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}

void InfluxDB_TaskMng() {
  uint32_t now = millis();
  if (now - influxdb_send_timestamp >= INFLUXDB_SEND_TIME) {
    influxdb_send_timestamp = now;

    sensor.clearFields();
    sensor.addField("rssi", WiFi.RSSI());
    sensor.addField("temperature", Get_TemperatureValue());
    sensor.addField("humidity", Get_HumidityValue());
    sensor.addField("pir_state", pir_state ? 1 : 0);

    Serial.print("Writing to InfluxDB: ");
    Serial.println(client.pointToLineProtocol(sensor));

    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
  }
}

void ReadPIRSensor() {
  pir_state = digitalRead(PIR_SENSOR_PIN);
  Serial.println(pir_state ? "Motion Detected" : "No Motion");
}

uint8_t Get_HumidityValue() {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < SENSOR_BUFFER_SIZE; i++) {
    sum += humidity_buffer[i];
  }
  return sum / SENSOR_BUFFER_SIZE;
}

uint8_t Get_TemperatureValue() {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < SENSOR_BUFFER_SIZE; i++) {
    sum += temp_buffer[i];
  }
  return sum / SENSOR_BUFFER_SIZE;
}
