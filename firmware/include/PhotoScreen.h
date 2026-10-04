#pragma once

// Photo viewer and photo source for the screensaver. Downloads are blocking
// and run on the main task. Owns the photo list and current index.
void setupPhotoDecoder();
void fetchPhotoList();
void ensurePhotoListLoaded();
bool hasPhotos();
// Shows the current photo without controls; advances the index on success.
bool showScreensaverPhoto();
void drawPhotosPage();
void handlePhotosTouch(int x, int y);
