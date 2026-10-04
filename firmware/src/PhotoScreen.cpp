#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include "PhotoScreen.h"
#include "ApiConfig.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"

#define MAX_PHOTOS 20
static String photoNames[MAX_PHOTOS];
static int photoCount = 0;
static int photoIndex = 0;
static bool photoListLoaded = false;

static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void setupPhotoDecoder() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tftOutput);
}

void fetchPhotoList() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(6000);
  http.setReuse(false);
  http.begin(API_PHOTOS);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("Photo list HTTP error: %d\n", code);
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  if (err) {
    Serial.print("Photo list JSON error: ");
    Serial.println(err.c_str());
    http.end();
    return;
  }

  photoCount = 0;
  for (JsonVariant item : doc["photos"].as<JsonArray>()) {
    if (photoCount >= MAX_PHOTOS) break;
    photoNames[photoCount++] = item.as<String>();
  }
  photoListLoaded = true;
  if (photoIndex >= photoCount) photoIndex = 0;
  http.end();
}

void ensurePhotoListLoaded() {
  if (!photoListLoaded) fetchPhotoList();
}

bool hasPhotos() {
  return photoCount > 0;
}

static bool showPhoto(int index, bool overlayControls) {
  if (!photoListLoaded) fetchPhotoList();
  if (photoCount == 0 || index < 0 || index >= photoCount) return false;

  String url = String(API_BASE) + "/photos/" + photoNames[index];
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  http.begin(url);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("Photo HTTP error: %d\n", code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0 || len > 130000) {
    Serial.printf("Photo size invalid: %d bytes\n", len);
    http.end();
    return false;
  }

  uint8_t* buffer = (uint8_t*)malloc(len);
  if (!buffer) {
    Serial.println("Not enough RAM for JPEG");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int total = 0;
  unsigned long deadline = millis() + 10000;

  while (total < len && millis() < deadline) {
    int available = stream->available();
    if (available > 0) {
      int toRead = min(available, len - total);
      int read = stream->readBytes(buffer + total, toRead);
      if (read > 0) total += read;
    } else {
      delay(1);
    }
  }

  bool ok = false;
  if (total == len) {
    tft.fillScreen(TFT_BLACK);
    TJpgDec.drawJpg(0, 0, buffer, len);
    ok = true;
  }

  free(buffer);
  http.end();

  if (overlayControls) {
    tft.fillRect(0, 208, 106, 32, TFT_DARKGREY);
    tft.fillRect(106, 208, 108, 32, TFT_DARKGREY);
    tft.fillRect(214, 208, 106, 32, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(36, 220); tft.print("< PREV");
    tft.setCursor(145, 220); tft.print("BACK");
    tft.setCursor(250, 220); tft.print("NEXT >");
  }

  return ok;
}

bool showScreensaverPhoto() {
  if (!showPhoto(photoIndex, false)) return false;
  photoIndex = (photoIndex + 1) % photoCount;
  return true;
}

void drawPhotosPage() {
  app.currentPage = PAGE_PHOTOS;
  if (!photoListLoaded) fetchPhotoList();

  if (photoCount == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(60, 80);
    tft.print("NO PHOTOS");
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(32, 120);
    tft.print("Add photos on HomeServer");
    drawBackBar(nullptr, "BACK", nullptr);
    return;
  }

  if (!showPhoto(photoIndex, true)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(45, 90);
    tft.print("PHOTO ERROR");
    drawBackBar(nullptr, "BACK", nullptr);
  }
}

void handlePhotosTouch(int x, int y) {
  if (y >= 205) {
    if (x < 106 && photoCount > 0) {
      photoIndex = (photoIndex - 1 + photoCount) % photoCount;
      drawPhotosPage();
    } else if (x < 214) {
      showPage(PAGE_MORE);
    } else if (photoCount > 0) {
      photoIndex = (photoIndex + 1) % photoCount;
      drawPhotosPage();
    }
  }
}
