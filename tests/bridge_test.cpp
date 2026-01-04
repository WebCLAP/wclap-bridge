#include "wclap-bridge.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("Global initialization", "[bridge]") {
  // Should be able to init/deinit multiple times
  CHECK(wclap_global_init(0) == true);
  wclap_global_deinit();

  CHECK(wclap_global_init(5000) == true);
  wclap_global_deinit();
}

TEST_CASE("Bridge version", "[bridge]") {
  CHECK(wclap_global_init(0) == true);

  const wclap_version_triple_t *version = wclap_bridge_version();
  REQUIRE(version != nullptr);

  // CLAP version should be reasonable (1.x.x as of 2024)
  CHECK(version->major >= 1);

  wclap_global_deinit();
}

TEST_CASE("Open invalid WCLAP", "[bridge]") {
  CHECK(wclap_global_init(0) == true);

  // Opening non-existent path should fail
  void *handle = wclap_open("/nonexistent/path.wclap");
  CHECK(handle == nullptr);

  // Opening a directory that's not a WCLAP should fail
  void *handle2 = wclap_open("/tmp");
  CHECK(handle2 == nullptr);

  wclap_global_deinit();
}

TEST_CASE("Set string prefixes", "[bridge]") {
  CHECK(wclap_global_init(0) == true);

  // Should not crash with various inputs
  wclap_set_strings("wclap:", "[WCLAP] ", "");
  wclap_set_strings("", "", "");
  wclap_set_strings(nullptr, nullptr, nullptr);

  wclap_global_deinit();
}

TEST_CASE("Open with directories", "[bridge]") {
  CHECK(wclap_global_init(0) == true);

  // Opening with invalid WCLAP path but valid aux dirs should still fail
  void *handle = wclap_open_with_dirs(
      "/nonexistent/path.wclap", "/tmp/presets", "/tmp/cache", "/tmp/var");
  CHECK(handle == nullptr);

  wclap_global_deinit();
}

TEST_CASE("Error reporting", "[bridge]") {
  CHECK(wclap_global_init(0) == true);

  // Open invalid path
  void *handle = wclap_open("/nonexistent/path.wclap");

  if (handle != nullptr) {
    // If it somehow succeeded, check error is empty
    char buffer[256] = {0};
    bool hasError = wclap_get_error(handle, buffer, sizeof(buffer));
    CHECK(hasError == false);
    wclap_close(handle);
  }
  // Note: wclap_get_error requires valid handle, can't test with nullptr

  wclap_global_deinit();
}
