#include <Arduino.h>
#include <TJpg_Decoder.h>
#include "PhotoScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "PhotoClient.h"
#include "PhotoRequestTracker.h"
#include "Screensaver.h"
#include "SettingsStore.h"
#include "UiHelpers.h"

static String photoNames[PHOTO_LIST_MAX];
static int photoCount = 0;
static int photoIndex = 0;
static bool photoListLoaded = false;

static PhotoRequestTracker photoTracker;
static bool listPrefetchPending = false;
static int inFlightIndex = 0;      // Photo index of the in-flight Image job.
static bool inFlightWallpaper = false; // In-flight job is the fixed screensaver wallpaper.
static bool photoPageShown = false; // The TFT currently shows Photos-page content.

static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void setupPhotoDecoder() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tftOutput);
  setupPhotoWorker();
}

static void drawPhotoControls() {
  tft.fillRect(0, 208, 106, 32, TFT_DARKGREY);
  tft.fillRect(106, 208, 108, 32, TFT_DARKGREY);
  tft.fillRect(214, 208, 106, 32, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(36, 220); tft.print("< PREV");
  tft.setCursor(145, 220); tft.print("BACK");
  tft.setCursor(250, 220); tft.print("NEXT >");
}

static void drawNoPhotos() {
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
}

static void drawPhotoError() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(45, 90);
  tft.print("PHOTO ERROR");
  drawBackBar(nullptr, "BACK", nullptr);
}

static void applyPhotoList(const PhotoList& list) {
  photoCount = 0;
  for (int i = 0; i < list.count && i < PHOTO_LIST_MAX; i++) photoNames[photoCount++] = list.names[i];
  photoListLoaded = true;
  if (photoIndex >= photoCount) photoIndex = 0;
}

static void handlePhotoResult(PhotoResult& result);

// Index of the Settings wallpaper in the photo list, or -1 to rotate.
static int wallpaperIndex() {
  const char* wallpaper = settings().wallpaper;
  if (wallpaper[0] == '\0') return -1;
  for (int i = 0; i < photoCount; i++) {
    if (photoNames[i] == wallpaper) return i;
  }
  return -1; // Photo no longer on the server: rotate as before.
}

// Submits the next job if the slot is free: the wanted photo (or the list it
// needs first), otherwise the boot prefetch. Never blocks.
static void pumpPhotoRequests() {
  if (photoTracker.busy()) return;

  PhotoJob job = {};
  if (photoTracker.needsRequest()) {
    job.generation = photoTracker.wantedGeneration();
    if (!photoListLoaded) {
      job.type = PhotoJobType::List;
    } else {
      job.type = PhotoJobType::Image;
      inFlightIndex = photoIndex;
      inFlightWallpaper = false;
      if (photoTracker.wanted() == PhotoPurpose::Screensaver && wallpaperIndex() >= 0) {
        inFlightIndex = wallpaperIndex();
        inFlightWallpaper = true;
      }
      size_t length = photoNames[inFlightIndex].length();
      if (length >= sizeof(job.name)) {
        Serial.printf("Photo name too long: %u bytes\n", (unsigned)length);
        PhotoResult failed = {job.generation, job.type, false, nullptr, nullptr, 0};
        photoTracker.sent();
        handlePhotoResult(failed);
        return;
      }
      memcpy(job.name, photoNames[inFlightIndex].c_str(), length + 1);
    }
  } else if (listPrefetchPending && !photoListLoaded) {
    job.generation = PhotoRequestTracker::UNTRACKED;
    job.type = PhotoJobType::List;
  } else {
    return;
  }
  if (job.type == PhotoJobType::List) listPrefetchPending = false;

  photoTracker.sent();
  if (!submitPhotoJob(job)) {
    // No worker: fail the job now so callers fall back as on a network error.
    PhotoResult failed = {job.generation, job.type, false, nullptr, nullptr, 0};
    handlePhotoResult(failed);
  }
}

static void handlePhotoResult(PhotoResult& result) {
  bool isList = result.type == PhotoJobType::List;
  // Copy the list before anything can submit a new job (slot ownership).
  if (isList && result.ok && result.list) applyPhotoList(*result.list);

  PhotoPurpose purpose = photoTracker.wanted();
  bool current = photoTracker.complete(result.generation);
  PhotoAction action = decidePhotoAction(isList, result.ok, current, purpose, photoCount);
  if (action != PhotoAction::RequestImage && action != PhotoAction::Discard) photoTracker.fulfil();

  switch (action) {
    case PhotoAction::Discard:
    case PhotoAction::RequestImage:
      break;
    case PhotoAction::ShowNoPhotos:
      drawNoPhotos();
      break;
    case PhotoAction::ShowPagePhoto:
      tft.fillScreen(TFT_BLACK);
      TJpgDec.drawJpg(0, 0, result.jpeg, result.jpegLength);
      drawPhotoControls();
      break;
    case PhotoAction::ShowPageError:
      drawPhotoError();
      break;
    case PhotoAction::ShowScreensaverPhoto:
      tft.fillScreen(TFT_BLACK);
      TJpgDec.drawJpg(0, 0, result.jpeg, result.jpegLength);
      // A fixed wallpaper leaves the rotation position alone.
      if (!inFlightWallpaper) photoIndex = (inFlightIndex + 1) % photoCount;
      onScreensaverPhotoResult(true);
      break;
    case PhotoAction::ScreensaverFallback:
      onScreensaverPhotoResult(false);
      break;
  }

  // Every path releases the buffer: drawn, stale, cancelled or failed.
  free(result.jpeg);
  result.jpeg = nullptr;
  pumpPhotoRequests();
}

void updatePhotos() {
  // Cancel before draining, so a result for a page that was just left is
  // discarded rather than drawn over the new page.
  if (app.currentPage != PAGE_PHOTOS) {
    photoPageShown = false;
    photoTracker.cancel(PhotoPurpose::Page);
  }
  if (app.currentPage != PAGE_SCREENSAVER) photoTracker.cancel(PhotoPurpose::Screensaver);

  PhotoResult result;
  if (receivePhotoResult(WorkerClient::Photos, result)) handlePhotoResult(result);
  else pumpPhotoRequests();
}

void requestPhotoList() {
  listPrefetchPending = true;
  pumpPhotoRequests();
}

bool requestScreensaverPhoto() {
  if (photoListLoaded && photoCount == 0) return false;
  photoTracker.want(PhotoPurpose::Screensaver);
  pumpPhotoRequests();
  return true;
}

bool isScreensaverPhotoPending() {
  return photoTracker.wanted() == PhotoPurpose::Screensaver;
}

void drawPhotosPage() {
  app.currentPage = PAGE_PHOTOS;

  if (photoListLoaded && photoCount == 0) {
    photoTracker.cancel(PhotoPurpose::Page);
    photoPageShown = true;
    drawNoPhotos();
    return;
  }

  // On entry, replace the previous page so its buttons are not mistaken for
  // these controls while the photo loads. NEXT/PREV keep the current photo on
  // screen until the new one arrives, as before.
  if (!photoPageShown) {
    tft.fillScreen(TFT_BLACK);
    drawPhotoControls();
    photoPageShown = true;
  }
  photoTracker.want(PhotoPurpose::Page);
  pumpPhotoRequests();
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

bool photoListReady() {
  return photoListLoaded;
}

int photoListCount() {
  return photoCount;
}

const char* photoListName(int index) {
  return index >= 0 && index < photoCount ? photoNames[index].c_str() : "";
}
