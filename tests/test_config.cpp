#include "../src/config.h"
#include "test_harness.h"

#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

// Redirect config to a temp directory for the duration of each test.
static std::string g_tmpDir;

static void setupTmpConfig() {
  g_tmpDir = "/tmp/test_config_" + std::to_string(getpid());
  setenv("XDG_CONFIG_HOME", g_tmpDir.c_str(), 1);
}

static void teardownTmpConfig() {
  fs::remove_all(g_tmpDir);
  unsetenv("XDG_CONFIG_HOME");
}

// ── shellEscapeSingleQuoted ───────────────────────────────────────────────────

static void test_escape_empty_string() {
  REQUIRE(shellEscapeSingleQuoted("") == "");
}

static void test_escape_no_quotes() {
  REQUIRE(shellEscapeSingleQuoted("hello world") == "hello world");
}

static void test_escape_single_quote() {
  // "it's" → "it'\'s"
  REQUIRE(shellEscapeSingleQuoted("it's") == "it'\\''s");
}

static void test_escape_multiple_quotes() {
  // "a'b'c" → "a'\''b'\''c"
  REQUIRE(shellEscapeSingleQuoted("a'b'c") == "a'\\''b'\\''c");
}

static void test_escape_only_quotes() {
  // "''" → "'\\'''\\''", i.e. each ' becomes '\''
  REQUIRE(shellEscapeSingleQuoted("''") == "'\\'''\\''");
}

// ── configLoad / configSave ───────────────────────────────────────────────────

static void test_load_missing_key_returns_empty() {
  setupTmpConfig();
  REQUIRE(configLoad("nonexistent") == "");
  teardownTmpConfig();
}

static void test_save_load_roundtrip() {
  setupTmpConfig();
  configSave("my_key", "my_value");
  REQUIRE(configLoad("my_key") == "my_value");
  teardownTmpConfig();
}

static void test_save_overwrites_existing_key() {
  setupTmpConfig();
  configSave("key", "first");
  configSave("key", "second");
  REQUIRE(configLoad("key") == "second");
  teardownTmpConfig();
}

static void test_save_preserves_other_keys() {
  setupTmpConfig();
  configSave("alpha", "1");
  configSave("beta",  "2");
  configSave("alpha", "updated");
  REQUIRE(configLoad("alpha") == "updated");
  REQUIRE(configLoad("beta")  == "2");
  teardownTmpConfig();
}

static void test_save_multiple_keys() {
  setupTmpConfig();
  configSave("x", "10");
  configSave("y", "20");
  configSave("z", "30");
  REQUIRE(configLoad("x") == "10");
  REQUIRE(configLoad("y") == "20");
  REQUIRE(configLoad("z") == "30");
  teardownTmpConfig();
}

static void test_value_with_equals_sign() {
  // Values may contain '=' — only the first '=' is the delimiter.
  setupTmpConfig();
  configSave("url", "host=localhost");
  REQUIRE(configLoad("url") == "host=localhost");
  teardownTmpConfig();
}

static void test_load_after_overwrite_returns_single_value() {
  // After overwriting, the file must contain exactly one entry for the key.
  setupTmpConfig();
  configSave("dup", "a");
  configSave("dup", "b");
  configSave("dup", "c");
  REQUIRE(configLoad("dup") == "c");
  teardownTmpConfig();
}

// ── configPath ────────────────────────────────────────────────────────────────

static void test_config_path_uses_xdg_config_home() {
  setenv("XDG_CONFIG_HOME", "/tmp/my_xdg", 1);
  fs::path p = configPath();
  unsetenv("XDG_CONFIG_HOME");
  REQUIRE(p.string().find("/tmp/my_xdg") == 0);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
  run("escape empty string",             test_escape_empty_string);
  run("escape no quotes",                test_escape_no_quotes);
  run("escape single quote",             test_escape_single_quote);
  run("escape multiple quotes",          test_escape_multiple_quotes);
  run("escape only quotes",              test_escape_only_quotes);
  run("load missing key returns empty",  test_load_missing_key_returns_empty);
  run("save/load roundtrip",             test_save_load_roundtrip);
  run("save overwrites existing key",    test_save_overwrites_existing_key);
  run("save preserves other keys",       test_save_preserves_other_keys);
  run("save multiple keys",              test_save_multiple_keys);
  run("value with equals sign",          test_value_with_equals_sign);
  run("overwrite yields single value",   test_load_after_overwrite_returns_single_value);
  run("configPath uses XDG_CONFIG_HOME", test_config_path_uses_xdg_config_home);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
