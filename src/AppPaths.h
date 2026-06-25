// another new header file to hold utility function(s)
// and variables that control the resulting, out-
// -written files from the program, and their operating
// system specific path, depending on the file's nature 
// (i.e. log file, config file, cache file, etc.)

#pragma once
#include <filesystem>
#include <cstdlib>
#include <cstring>

namespace appPaths {

inline std::filesystem::path getDataDir(const std::string& appName) {
    namespace fs = std::filesystem;

#ifdef _WIN32
    const char* localApp = std::getenv("LOCALAPPDATA");
    if (localApp) {
        return fs::path(localApp) / appName;
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return fs::path(userProfile) / "AppData" / "Local" / appName;
    }
    return fs::path(".") / appName;

#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Library" / "Application Support" / appName;
    }
    return fs::path(".") / appName;

#else
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData && std::strlen(xdgData) > 0) {
        return fs::path(xdgData) / appName;
    }
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / ".local" / "share" / appName;
    }
    return fs::path(".") / appName;
#endif
}

}