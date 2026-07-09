#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SensirionI2cScd4x.h>
#include <math.h>
#include "config.h"

#define SHT21_ADDR 0x40
#define SHT21_CMD_TEMP_NOHOLD 0xF3
#define SHT21_CMD_HUM_NOHOLD 0xF5

Adafruit_BMP085 bmp;
SensirionI2cScd4x scd4x;

bool bmpOk = false;

// CRC-8 validation function for SHT21 (polynomial 0x131)
uint8_t crc8(uint8_t msb, uint8_t lsb)
{
  uint32_t data = ((uint32_t)msb << 8) | lsb;
  for (uint8_t bit = 0; bit < 16; bit++)
  {
    if (data & 0x8000)
    {
      data = (data << 1) ^ 0x131;
    }
    else
    {
      data <<= 1;
    }
  }
  return (uint8_t)(data >> 8);
}

uint16_t readSHT21Raw(uint8_t command, uint16_t delayMs)
{
  const uint8_t maxRetries = 5;
  for (uint8_t retry = 0; retry < maxRetries; retry++)
  {
    if (retry > 0) delay(100);
    
  Wire.beginTransmission(SHT21_ADDR);
  Wire.write(command);
    if (Wire.endTransmission() != 0)
  {
      if (retry < maxRetries - 1) { delay(50); continue; }
    return 0xFFFF;
  }

  delay(delayMs);

    if (Wire.requestFrom((uint8_t)SHT21_ADDR, (uint8_t)3) < 3)
  {
      if (retry < maxRetries - 1) { delay(100); continue; }
    return 0xFFFF;
  }

  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  uint8_t crc = Wire.read();

    if (crc8(msb, lsb) == crc)
  {
      uint16_t raw = ((uint16_t)msb << 8) | lsb;
      return raw & ~0x0003;
    }
    
    if (retry < maxRetries - 1) delay(150);
  }
    return 0xFFFF;
}

float readSHT21TemperatureC()
{
  uint16_t raw = readSHT21Raw(SHT21_CMD_TEMP_NOHOLD, 85);
  if (raw == 0xFFFF)
    return NAN;
  return -46.85 + 175.72 * (float)raw / 65536.0;
}

float readSHT21Humidity()
{
  uint16_t raw = readSHT21Raw(SHT21_CMD_HUM_NOHOLD, 29);
  if (raw == 0xFFFF)
    return NAN;
  float rh = -6.0 + 125.0 * (float)raw / 65536.0;
  if (rh < 0)
    rh = 0;
  if (rh > 100)
    rh = 100;
  return rh;
}

float avg3(float a, float b, float c)
{
  float sum = 0;
  int count = 0;
  if (!isnan(a))
  {
    sum += a;
    count++;
  }
  if (!isnan(b))
  {
    sum += b;
    count++;
  }
  if (!isnan(c))
  {
    sum += c;
    count++;
  }
  if (count == 0)
    return NAN;
  return sum / count;
}

float avg2(float a, float b)
{
  float sum = 0;
  int count = 0;
  if (!isnan(a))
  {
    sum += a;
    count++;
  }
  if (!isnan(b))
  {
    sum += b;
    count++;
  }
  if (count == 0)
    return NAN;
  return sum / count;
}

bool reconnectWiFi()
{
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_RECONNECT_TIMEOUT)
  {
    delay(500);
  }

  return (WiFi.status() == WL_CONNECTED);
}

void sendToServer(float temp, float hum, float press, uint16_t co2)
{
  // Check WiFi connection and try to reconnect if needed
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Attempting reconnection...");
    if (!reconnectWiFi())
    {
      Serial.println("Failed to reconnect to WiFi. Skipping data transmission.");
      return;
    }
  }

  // Prepare JSON payload
  String payload = "{";

  if (isnan(temp))
  {
    payload += "\"temperature\":0,";
  }
  else
  {
    payload += "\"temperature\":" + String(temp, 2) + ",";
  }

  if (isnan(hum))
  {
    payload += "\"humidity\":0,";
  }
  else
  {
    payload += "\"humidity\":" + String(hum, 2) + ",";
  }

  if (isnan(press))
  {
    payload += "\"pressure\":0,";
  }
  else
  {
    payload += "\"pressure\":" + String(press, 2) + ",";
  }

  payload += "\"co2\":" + String(co2);
  payload += "}";

  // Retry logic for HTTP POST
  bool success = false;
  for (int attempt = 1; attempt <= HTTP_MAX_RETRIES && !success; attempt++)
  {
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);

    if (!http.begin(SERVER_URL))
    {
      if (attempt < HTTP_MAX_RETRIES) delay(HTTP_RETRY_DELAY);
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", API_KEY);

    int httpCode = http.POST(payload);

      if (httpCode == 200 || httpCode == 201)
      {
      Serial.println();
      Serial.print("Temperature (*C): ");
      Serial.print(temp, 2);
      Serial.println();

      Serial.print("Humidity (%): ");
      Serial.print(hum, 2);
      Serial.println();

      Serial.print("Pressure (hPa): ");
      Serial.print(press, 2);
      Serial.println();

      Serial.print("CO2 (ppm): ");
      Serial.print(co2);
      Serial.println();

      Serial.println();
      Serial.print("Data sent successfully\n");
      Serial.println();
        success = true;
      }
    else if (httpCode > 0)
      {
      Serial.print("HTTP error: ");
      Serial.println(httpCode);
      }
    else if (attempt == HTTP_MAX_RETRIES)
    {
      Serial.print("HTTP failed: ");
      Serial.println(http.errorToString(httpCode));
    }

    http.end();

    if (!success && attempt < HTTP_MAX_RETRIES)
    {
      delay(HTTP_RETRY_DELAY);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Weather Station Starting ===");

  // WiFi connection with timeout
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_RECONNECT_TIMEOUT)
  {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("\nWiFi connected - IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nWiFi connection failed");
  }

  Wire.begin(3, 4);
  delay(100);

  // Initialize sensors (without verification yet)
  bmpOk = bmp.begin();
  scd4x.begin(Wire, 0x62);
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();

  Serial.println("Waiting 10s for sensors...");
  delay(10000);
  
  // Verify all sensors via I2C after stabilization
  // BMP085 - typically at 0x77
  Wire.beginTransmission(0x77);
  if (Wire.endTransmission() == 0)
      {
    Serial.println("BMP085: OK");
  }
  
  // SCD4x - at 0x62
  Wire.beginTransmission(0x62);
  if (Wire.endTransmission() == 0)
      {
    Serial.println("SCD4x: OK");
  }
  
  // SHT21 - at 0x40
  Wire.beginTransmission(SHT21_ADDR);
  if (Wire.endTransmission() == 0)
  {
    Serial.println("SHT21: OK");
  }

  Serial.println("Ready!");
}

void loop()
{
  static unsigned long lastMeasure = 0;
  const unsigned long interval = 3600000UL; // 1 hour (60 minutes * 60 seconds * 1000 ms)

  unsigned long now = millis();

  if (now - lastMeasure < interval && lastMeasure != 0)
  {
    delay(1000);
    return;
  }
  lastMeasure = now;

  float shtTemp = readSHT21TemperatureC();
  float shtRH = readSHT21Humidity();

  float bmpTemp = NAN;
  float bmpPress_hPa = NAN;
  if (bmpOk)
  {
    bmpTemp = bmp.readTemperature();
    bmpPress_hPa = bmp.readPressure() / 100.0;
  }

  uint16_t co2 = 0;
  float scdTemp = NAN;
  float scdRH = NAN;

  uint16_t error = scd4x.readMeasurement(co2, scdTemp, scdRH);
  if (error || co2 == 0xFFFF)
  {
    co2 = 0;
    scdTemp = NAN;
    scdRH = NAN;
  }

  float tempFinal = avg3(shtTemp, bmpTemp, scdTemp);
  float rhFinal = avg2(shtRH, scdRH);
  float pressFinal = bmpPress_hPa;

  sendToServer(tempFinal, rhFinal, pressFinal, co2);
}
