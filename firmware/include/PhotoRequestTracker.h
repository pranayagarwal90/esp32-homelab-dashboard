#pragma once
#include <stdint.h>

// Pure main-task bookkeeping for the single photo request slot, shared with
// host tests. Every want() gets a new generation; a result is current only if
// its generation is still the one wanted, so superseded or cancelled
// downloads are discarded and never drawn.

enum class PhotoPurpose : uint8_t { None, Page, Screensaver };

class PhotoRequestTracker {
 public:
  // Generation for requests nobody waits on (the boot photo-list prefetch).
  static constexpr uint32_t UNTRACKED = 0;

  // Replaces any earlier desire; returns its generation.
  uint32_t want(PhotoPurpose purpose) {
    if (++lastGeneration == UNTRACKED) ++lastGeneration;
    desired = purpose;
    desiredGeneration = lastGeneration;
    return desiredGeneration;
  }

  // Drops the desire if it is for purpose (page left, screensaver woken).
  void cancel(PhotoPurpose purpose) {
    if (desired == purpose) desired = PhotoPurpose::None;
  }

  PhotoPurpose wanted() const { return desired; }
  uint32_t wantedGeneration() const { return desiredGeneration; }
  bool busy() const { return inFlight; }
  bool needsRequest() const { return desired != PhotoPurpose::None && !inFlight; }

  void sent() { inFlight = true; }

  // Every result frees the slot (one request at a time). True if the result
  // still answers the current desire.
  bool complete(uint32_t generation) {
    inFlight = false;
    return desired != PhotoPurpose::None && generation != UNTRACKED &&
           generation == desiredGeneration;
  }

  // The main task acted on the desire; nothing more to fetch for it.
  void fulfil() { desired = PhotoPurpose::None; }

 private:
  PhotoPurpose desired = PhotoPurpose::None;
  uint32_t desiredGeneration = UNTRACKED;
  uint32_t lastGeneration = UNTRACKED;
  bool inFlight = false;
};

enum class PhotoAction : uint8_t {
  Discard,              // stale/cancelled: free and draw nothing
  RequestImage,         // list arrived for a waiting desire; fetch the image
  ShowNoPhotos,         // Photos page: "NO PHOTOS"
  ShowPagePhoto,        // Photos page: JPEG + controls
  ShowPageError,        // Photos page: "PHOTO ERROR"
  ShowScreensaverPhoto, // screensaver: JPEG, advance index
  ScreensaverFallback   // screensaver: redraw clock
};

// Mirrors the old synchronous flow: after a list fetch (successful or not) an
// empty list means "no photos"; otherwise the image is fetched next.
inline PhotoAction decidePhotoAction(bool isList, bool ok, bool current,
                                     PhotoPurpose purpose, int photoCount) {
  if (!current || purpose == PhotoPurpose::None) return PhotoAction::Discard;
  bool page = purpose == PhotoPurpose::Page;
  if (isList) {
    if (photoCount > 0) return PhotoAction::RequestImage;
    return page ? PhotoAction::ShowNoPhotos : PhotoAction::ScreensaverFallback;
  }
  if (page) return ok ? PhotoAction::ShowPagePhoto : PhotoAction::ShowPageError;
  return ok ? PhotoAction::ShowScreensaverPhoto : PhotoAction::ScreensaverFallback;
}
