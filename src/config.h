#pragma once

#include <filesystem>
#include <string>

// Returns the path to the persistent config file.
std::filesystem::path configPath();

// Reads the value for key from the config file. Returns "" if not found.
std::string configLoad(const std::string &key);

// Writes (or overwrites) key=value in the config file.
void configSave(const std::string &key, const std::string &value);

// Shell-escape a string for single-quoted insertion: replace ' with '\''
std::string shellEscapeSingleQuoted(const std::string &s);
