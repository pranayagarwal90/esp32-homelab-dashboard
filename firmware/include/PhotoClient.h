#pragma once
#include <Arduino.h>

// Background photo network I/O. One FreeRTOS worker downloads the photo list
// or one JPEG at a time; it never touches the TFT, JPEG decoder or UI state.
//
// Ownership:
// - PhotoJob is copied into the request queue; the name is a fixed char array.
// - A List result points at a worker-owned slot. The main task must copy it
//   before submitting the next job; the worker only writes it while a List job
//   is in flight.
// - An Image result transfers a malloc'd JPEG buffer to the main task, which
//   must free() it whether or not it is drawn. Failed jobs carry no buffer.

constexpr int PHOTO_LIST_MAX = 20;
constexpr size_t PHOTO_NAME_MAX = 64; // Including the terminator.

enum class PhotoJobType : uint8_t { List, Image };

struct PhotoJob {
  uint32_t generation;
  PhotoJobType type;
  char name[PHOTO_NAME_MAX];
};

struct PhotoList {
  String names[PHOTO_LIST_MAX];
  int count = 0;
};

struct PhotoResult {
  uint32_t generation;
  PhotoJobType type;
  bool ok;
  const PhotoList* list; // List success only.
  uint8_t* jpeg;         // Image success only; receiver frees.
  size_t jpegLength;
};

void setupPhotoWorker();
// Main task. False if the worker is unavailable (the caller treats the job as
// failed). Only call when no job is in flight.
bool submitPhotoJob(const PhotoJob& job);
// Main task, nonblocking.
bool receivePhotoResult(PhotoResult& result);
