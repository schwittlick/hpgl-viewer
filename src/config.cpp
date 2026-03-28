#include "config.h"

#include <cstdlib>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

std::filesystem::path configPath() {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  fs::path base = xdg ? fs::path(xdg) : fs::path(getenv("HOME")) / ".config";
  return base / "hpgl-viewer" / "config";
}

std::string configLoad(const std::string &key) {
  std::ifstream f(configPath());
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind(key + "=", 0) == 0)
      return line.substr(key.size() + 1);
  }
  return {};
}

void configSave(const std::string &key, const std::string &value) {
  fs::path path = configPath();
  fs::create_directories(path.parent_path());

  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  bool found = false;
  while (std::getline(in, line)) {
    if (line.rfind(key + "=", 0) == 0) {
      lines.push_back(key + "=" + value);
      found = true;
    } else {
      lines.push_back(line);
    }
  }
  in.close();
  if (!found)
    lines.push_back(key + "=" + value);

  std::ofstream out(path);
  for (auto &l : lines)
    out << l << "\n";
}

std::string shellEscapeSingleQuoted(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else           out += c;
  }
  return out;
}
