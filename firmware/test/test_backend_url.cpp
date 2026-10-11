#include "BackendUrl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static BackendUrlError check(const char* input, const char* expected) {
  char out[BACKEND_URL_MAX];
  BackendUrlError error = normalizeBackendUrl(input, out, sizeof(out));
  if (expected) {
    if (error != BackendUrlError::None || strcmp(out, expected) != 0) {
      fprintf(stderr, "'%s' -> '%s' (error %d), expected '%s'\n", input, out, int(error), expected);
      assert(false);
    }
  } else {
    assert(error != BackendUrlError::None && out[0] == '\0');
  }
  return error;
}

static void testAccepted() {
  check("http://192.168.1.50:8090", "http://192.168.1.50:8090");
  check("http://server.local:8090/", "http://server.local:8090");      // Trailing slash removed.
  check("  http://dashboard:8090  ", "http://dashboard:8090");          // Whitespace trimmed.
  check("\thttp://10.0.0.5\r\n", "http://10.0.0.5");                    // Port optional.
  check("HTTP://Dashboard.Example.LAN:80", "http://dashboard.example.lan:80"); // Case normalized.
  check("http://my-server.home.arpa:65535", "http://my-server.home.arpa:65535");
  check("http://host:08090", "http://host:8090");                      // Leading zeros dropped.
  check("http://0.0.0.0:1", "http://0.0.0.0:1");
  check("http://a", "http://a");
}

static void testRejected() {
  assert(check("", nullptr) == BackendUrlError::Empty);
  assert(check("   ", nullptr) == BackendUrlError::Empty);
  assert(check(nullptr, nullptr) == BackendUrlError::Empty);
  // No scheme: explicit, never guessed.
  assert(check("192.168.1.50:8090", nullptr) == BackendUrlError::Scheme);
  assert(check("server.local", nullptr) == BackendUrlError::Scheme);
  assert(check("ftp://server", nullptr) == BackendUrlError::Scheme);
  assert(check("http:/server", nullptr) == BackendUrlError::Scheme);
  // The HTTP clients are plain TCP.
  assert(check("https://example.local", nullptr) == BackendUrlError::Https);
  // A path, query or fragment: the base URL only.
  assert(check("http://server:8090/api/status", nullptr) == BackendUrlError::Path);
  assert(check("http://server:8090/api/", nullptr) == BackendUrlError::Path);
  assert(check("http://server//", nullptr) == BackendUrlError::Path);
  assert(check("http://server?x=1", nullptr) == BackendUrlError::Path);
  assert(check("http://server#top", nullptr) == BackendUrlError::Path);
  // Malformed hosts.
  assert(check("http://", nullptr) == BackendUrlError::Host);
  assert(check("http://:8090", nullptr) == BackendUrlError::Host);
  assert(check("http://user:pw@server", nullptr) == BackendUrlError::Host);
  assert(check("http://[::1]:8090", nullptr) == BackendUrlError::Host);
  assert(check("http://fe80::1", nullptr) == BackendUrlError::Host);
  assert(check("http://my server", nullptr) == BackendUrlError::Host);
  assert(check("http://bad_name", nullptr) == BackendUrlError::Host);
  assert(check("http://-server", nullptr) == BackendUrlError::Host);
  assert(check("http://server-.lan", nullptr) == BackendUrlError::Host);
  assert(check("http://.server", nullptr) == BackendUrlError::Host);
  assert(check("http://server..lan", nullptr) == BackendUrlError::Host);
  assert(check("http://server.", nullptr) == BackendUrlError::Host);
  assert(check("http://256.1.1.1", nullptr) == BackendUrlError::Host);
  assert(check("http://1.2.3", nullptr) == BackendUrlError::Host);
  assert(check("http://1.2.3.4.5", nullptr) == BackendUrlError::Host);
  assert(check("http://8090", nullptr) == BackendUrlError::Host);
  // Ports.
  assert(check("http://server:", nullptr) == BackendUrlError::Port);
  assert(check("http://server:0", nullptr) == BackendUrlError::Port);
  assert(check("http://server:65536", nullptr) == BackendUrlError::Port);
  assert(check("http://server:123456", nullptr) == BackendUrlError::Port);
  assert(check("http://server:80a", nullptr) == BackendUrlError::Port);
  // Length: 127 characters fit (with the terminator), 128 do not.
  char url[200] = "http://";
  size_t n = 7;
  while (n < BACKEND_URL_MAX - 1) {
    url[n] = n % 10 == 9 ? '.' : 'a'; // Labels of up to 9 letters.
    n++;
  }
  url[n] = '\0';
  assert(strlen(url) == BACKEND_URL_MAX - 1 && url[n - 1] != '.');
  check(url, url);
  url[n] = 'z';
  url[n + 1] = '\0';
  assert(check(url, nullptr) == BackendUrlError::TooLong);
  // A buffer smaller than BACKEND_URL_MAX is refused, never overrun.
  char small[16];
  assert(normalizeBackendUrl("http://server", small, sizeof(small)) != BackendUrlError::None);
}

static void testErrorTexts() {
  for (int e = 1; e <= int(BackendUrlError::Path); e++) {
    assert(strlen(backendUrlErrorText(BackendUrlError(e))) > 0);
  }
  assert(strstr(backendUrlErrorText(BackendUrlError::Path), "/api/status") != nullptr);
  assert(strstr(backendUrlErrorText(BackendUrlError::Scheme), "http://") != nullptr);
}

static void testValidStored() {
  assert(validBackendUrl("http://192.168.1.50:8090"));
  assert(!validBackendUrl("http://server/"));      // Valid input, but not normalized.
  assert(!validBackendUrl("http://Server"));
  assert(!validBackendUrl(""));
  assert(!validBackendUrl("garbage"));
}

static void testEndpoints() {
  const char* base = "http://192.168.1.50:8090";
  char out[API_URL_MAX];
  assert(joinBackendUrl(out, sizeof(out), base, API_STATUS_PATH));
  assert(strcmp(out, "http://192.168.1.50:8090/api/status") == 0);
  assert(joinBackendUrl(out, sizeof(out), base, API_PHOTOS_PATH));
  assert(strcmp(out, "http://192.168.1.50:8090/api/photos") == 0);
  assert(joinBackendUrl(out, sizeof(out), base, API_AI_STATUS_PATH));
  assert(strcmp(out, "http://192.168.1.50:8090/api/ai/status") == 0);
  assert(joinBackendUrl(out, sizeof(out), base, API_AI_EXPLAIN_PATH));
  assert(strcmp(out, "http://192.168.1.50:8090/api/ai/explain") == 0);
  char photo[PHOTO_URL_MAX];
  assert(joinBackendUrl(photo, sizeof(photo), base, PHOTO_FILE_PATH, "photo_001.jpg"));
  assert(strcmp(photo, "http://192.168.1.50:8090/photos/photo_001.jpg") == 0);
  // A normalized base never yields a double or missing slash.
  char normalized[BACKEND_URL_MAX];
  normalizeBackendUrl("http://server.local:8090/", normalized, sizeof(normalized));
  assert(joinBackendUrl(out, sizeof(out), normalized, API_STATUS_PATH));
  assert(strcmp(out, "http://server.local:8090/api/status") == 0);
  assert(strstr(out + 7, "//") == nullptr);

  // The longest valid base still fits every endpoint.
  char longest[BACKEND_URL_MAX];
  memset(longest, 'a', sizeof(longest));
  memcpy(longest, "http://", 7);
  longest[BACKEND_URL_MAX - 1] = '\0';
  const char* paths[] = {API_STATUS_PATH, API_PHOTOS_PATH, API_AI_STATUS_PATH, API_AI_EXPLAIN_PATH};
  for (const char* path : paths) assert(joinBackendUrl(out, sizeof(out), longest, path));
  assert(joinBackendUrl(photo, sizeof(photo), longest, PHOTO_FILE_PATH, "photo_123.jpg"));
  // No backend, or no room: false and empty, never truncated.
  assert(!joinBackendUrl(out, sizeof(out), "", API_STATUS_PATH) && out[0] == '\0');
  assert(!joinBackendUrl(out, 20, base, API_STATUS_PATH) && out[0] == '\0');
}

static void testDisplay() {
  char out[40];
  formatBackendForDisplay("http://192.168.1.50:8090", out, sizeof(out), 34);
  assert(strcmp(out, "192.168.1.50:8090") == 0);
  formatBackendForDisplay("", out, sizeof(out), 34);
  assert(strcmp(out, "Not set") == 0);
  formatBackendForDisplay("http://a-very-long-dashboard-host-name.example.lan:8090", out, sizeof(out), 20);
  assert(strlen(out) == 20 && strcmp(out + 17, "...") == 0);
}

int main() {
  testAccepted();
  testRejected();
  testErrorTexts();
  testValidStored();
  testEndpoints();
  testDisplay();
  puts("backend URL tests passed");
  return 0;
}
