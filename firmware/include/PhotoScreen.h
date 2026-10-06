#pragma once

// Photo viewer and photo source for the screensaver. Owns the photo list and
// current index. Downloads run on the PhotoClient worker; JPEG decoding and
// all drawing happen here on the main task.
void setupPhotoDecoder();
// Asynchronous boot prefetch of the photo list (no drawing).
void requestPhotoList();
// Starts loading the current screensaver photo. Returns false (nothing
// requested) when the photo list is known to be empty. The outcome arrives via
// onScreensaverPhotoResult().
bool requestScreensaverPhoto();
bool isScreensaverPhotoPending();
void drawPhotosPage();
void handlePhotosTouch(int x, int y);
// Call every loop(): drops requests for pages no longer shown, applies
// finished downloads and submits the next request.
void updatePhotos();
// Read-only view of the loaded photo list (Settings > Wallpaper).
bool photoListReady();
int photoListCount();
const char* photoListName(int index);
