#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The dashboard backend's base URL, "http://host[:port]", and the API
// endpoints built from it. Pure (no Arduino dependencies), host-tested.
//
// Accepted: http:// + an IPv4 address or host name + an optional port, with
// an optional trailing slash. Surrounding whitespace is trimmed; the scheme and
// host are lower-cased. Rejected: a missing scheme (no guessing), https://
// (the HTTP clients are plain TCP), credentials, IPv6, and any path, query or
// fragment: the address is the base only, never ".../api/status".

constexpr size_t BACKEND_URL_MAX = 128; // Including the terminator.

enum class BackendUrlError : uint8_t { None, Empty, TooLong, Scheme, Https, Host, Port, Path };

inline const char* backendUrlErrorText(BackendUrlError error) {
  switch (error) {
    case BackendUrlError::None: return "";
    case BackendUrlError::Empty: return "Enter the dashboard server address.";
    case BackendUrlError::TooLong: return "The dashboard server address is too long.";
    case BackendUrlError::Scheme: return "The dashboard server address must start with http://";
    case BackendUrlError::Https: return "https:// is not supported; use http://";
    case BackendUrlError::Host: return "The dashboard server name or IP address is not valid.";
    case BackendUrlError::Port: return "The port must be a number from 1 to 65535.";
    case BackendUrlError::Path: return "Enter only the server address, without a path such as /api/status.";
  }
  return "";
}

inline bool backendUrlSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline char backendUrlLower(char c) {
  return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

inline bool backendUrlPrefix(const char* text, size_t length, const char* prefix) {
  size_t n = strlen(prefix);
  if (length < n) return false;
  for (size_t i = 0; i < n; i++) {
    if (backendUrlLower(text[i]) != prefix[i]) return false;
  }
  return true;
}

// An IPv4 address (four 0-255 parts) or a DNS name: letters, digits, '-' and
// '.', labels of 1-63 characters not starting or ending with '-'. All-numeric
// dotted names must be valid IPv4.
inline bool validBackendHost(const char* host, size_t length) {
  if (length == 0 || length > 253) return false;
  bool numeric = true;
  size_t label = 0;
  for (size_t i = 0; i <= length; i++) {
    char c = i < length ? host[i] : '.';
    if (c == '.') {
      if (label == 0 || label > 63 || host[i - 1] == '-') return false;
      label = 0;
      continue;
    }
    bool digit = c >= '0' && c <= '9';
    bool letter = backendUrlLower(c) >= 'a' && backendUrlLower(c) <= 'z';
    if (!digit && !letter && c != '-') return false;
    if (c == '-' && label == 0) return false;
    numeric &= digit;
    label++;
  }
  if (!numeric) return true;
  int parts = 0, value = 0, digits = 0;
  for (size_t i = 0; i <= length; i++) {
    char c = i < length ? host[i] : '.';
    if (c == '.') {
      if (digits == 0 || value > 255) return false;
      parts++;
      value = digits = 0;
    } else {
      value = value * 10 + (c - '0');
      if (++digits > 3) return false;
    }
  }
  return parts == 4;
}

// Normalizes input into out ("http://host[:port]"). On error out is "".
inline BackendUrlError normalizeBackendUrl(const char* input, char* out, size_t outSize) {
  if (out && outSize) out[0] = '\0';
  if (!input || !out || outSize < BACKEND_URL_MAX) return BackendUrlError::Empty;
  while (backendUrlSpace(*input)) input++;
  size_t length = strlen(input);
  while (length > 0 && backendUrlSpace(input[length - 1])) length--;
  if (length == 0) return BackendUrlError::Empty;
  if (length >= BACKEND_URL_MAX) return BackendUrlError::TooLong;
  if (backendUrlPrefix(input, length, "https://")) return BackendUrlError::Https;
  if (!backendUrlPrefix(input, length, "http://")) return BackendUrlError::Scheme;

  const char* authority = input + 7;
  size_t rest = length - 7;
  size_t end = 0;
  while (end < rest && authority[end] != '/' && authority[end] != '?' && authority[end] != '#') end++;
  // Only an empty path or a single trailing "/" is accepted.
  if (end < rest && !(authority[end] == '/' && end + 1 == rest)) return BackendUrlError::Path;

  size_t colon = end;
  for (size_t i = 0; i < end; i++) {
    char c = authority[i];
    if (c == '@' || c == '[' || c == ']' || backendUrlSpace(c)) return BackendUrlError::Host;
    if (c == ':') {
      if (colon != end) return BackendUrlError::Host; // More than one ':' (IPv6).
      colon = i;
    }
  }
  if (!validBackendHost(authority, colon)) return BackendUrlError::Host;

  unsigned long port = 0;
  if (colon != end) {
    size_t digits = end - colon - 1;
    if (digits == 0 || digits > 5) return BackendUrlError::Port;
    for (size_t i = colon + 1; i < end; i++) {
      if (authority[i] < '0' || authority[i] > '9') return BackendUrlError::Port;
      port = port * 10 + (authority[i] - '0');
    }
    if (port == 0 || port > 65535) return BackendUrlError::Port;
  }

  size_t n = 0;
  memcpy(out, "http://", 7);
  n = 7;
  for (size_t i = 0; i < colon; i++) out[n++] = backendUrlLower(authority[i]);
  out[n] = '\0';
  if (port) snprintf(out + n, outSize - n, ":%lu", port);
  return BackendUrlError::None;
}

inline bool validBackendUrl(const char* url) {
  char normalized[BACKEND_URL_MAX];
  return normalizeBackendUrl(url, normalized, sizeof(normalized)) == BackendUrlError::None &&
         strcmp(normalized, url) == 0;
}

// --- Endpoints ----------------------------------------------------------------------

constexpr const char* API_STATUS_PATH = "/api/status";
constexpr const char* API_PHOTOS_PATH = "/api/photos";
constexpr const char* API_AI_STATUS_PATH = "/api/ai/status";
constexpr const char* API_AI_EXPLAIN_PATH = "/api/ai/explain";
constexpr const char* PHOTO_FILE_PATH = "/photos/";

constexpr size_t API_URL_MAX = BACKEND_URL_MAX + 24;    // Longest fixed path: /api/ai/explain.
constexpr size_t PHOTO_URL_MAX = BACKEND_URL_MAX + 96;  // /photos/ + a file name.

// base + path + suffix (path starts with '/'; base has no trailing slash).
// False, with out = "", when base is empty or the result does not fit.
inline bool joinBackendUrl(char* out, size_t size, const char* base, const char* path, const char* suffix = "") {
  if (!out || size == 0) return false;
  out[0] = '\0';
  if (!base || !base[0] || !path) return false;
  int written = snprintf(out, size, "%s%s%s", base, path, suffix ? suffix : "");
  if (written < 0 || size_t(written) >= size) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// For display: the address without "http://", cut to maxChars with "...".
inline void formatBackendForDisplay(const char* base, char* out, size_t size, size_t maxChars) {
  if (!out || size == 0) return;
  const char* shown = base && base[0] ? base : "Not set";
  if (backendUrlPrefix(shown, strlen(shown), "http://")) shown += 7;
  size_t length = strlen(shown);
  if (maxChars >= size) maxChars = size - 1;
  if (length <= maxChars) {
    snprintf(out, size, "%s", shown);
  } else if (maxChars >= 3) {
    snprintf(out, size, "%.*s...", int(maxChars - 3), shown);
  } else {
    snprintf(out, size, "%.*s", int(maxChars), shown);
  }
}
