#pragma once
#include <Arduino.h>

// Background image network I/O. One FreeRTOS worker downloads the photo list,
// a photo JPEG or (for Radar) any API_BASE path, one at a time; it never
// touches the TFT, JPEG decoder or UI state.
//
// Clients: Photos and Radar each keep at most one job outstanding, and a
// result is delivered only to the client that submitted it.
//
// Ownership:
// - PhotoJob is copied into the request queue; the name is a fixed char array.
// - A List result points at a worker-owned slot. The main task must copy it
//   before submitting the next job; the worker only writes it while a List job
//   is in flight.
// - An Image result transfers a malloc'd JPEG buffer to the main task, which
//   must free() it whether or not it is drawn. Failed jobs carry no buffer.
// - A Fetch result is the same: a malloc'd body of at most job.maxBytes.

constexpr int PHOTO_LIST_MAX = 20;
constexpr size_t PHOTO_NAME_MAX = 64; // Including the terminator.

enum class PhotoJobType : uint8_t { List, Image, Fetch };
enum class WorkerClient : uint8_t { Photos, Radar };

struct PhotoJob {
  uint32_t generation;
  PhotoJobType type;
  char name[PHOTO_NAME_MAX]; // Image: photo file name. Fetch: path under API_BASE.
  WorkerClient client;       // Zero-initialised jobs belong to Photos.
  uint32_t maxBytes;         // Fetch only.
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
  WorkerClient client;
};

void setupPhotoWorker();
// Main task. False if the worker is unavailable (the caller treats the job as
// failed). Only call when no job is in flight.
bool submitPhotoJob(const PhotoJob& job);
// Main task, nonblocking: the next result if it belongs to `client`.
bool receivePhotoResult(WorkerClient client, PhotoResult& result);
