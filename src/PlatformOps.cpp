#include "PlatformOps.h"
#include <fstream>
#include <cstdlib>
// for obtaining current time && timestamping
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace platform {

Result moveToTrash(const std::filesystem::path& target) {
#ifdef _WIN32
    // Windows: use SHFileOperationW with FOF_ALLOWUNDO
    // The ALLOWUNDO flag is what makes it go to the Recycle Bin
    // rather than being permanently destroyed.
    std::wstring widePath = target.wstring();
    widePath.push_back(L'\0'); // SHFileOperation needs double-null termination

    SHFILEOPSTRUCTW op = {};
    op.wFunc  = FO_DELETE;
    op.pFrom  = widePath.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

    int result = SHFileOperationW(&op);
    if (result != 0) {
        return Result::fail("SHFileOperation failed with code "
                            + std::to_string(result));
    }
    return Result::ok("Moved to Recycle Bin");

#elif defined(__APPLE__)
    // macOS: the simplest reliable approach is calling
    // osascript to invoke Finder's "move to trash" AppleScript,
    // or using the NSFileManager API via Objective-C++.
    // For a pure-C++ project, the osascript route works:
    std::string cmd = "osascript -e 'tell application \"Finder\" "
                      "to delete POSIX file \""
                      + target.string() + "\"'";
    int ret = std::system(cmd.c_str());
    return ret == 0
        ? Result::ok("Moved to Trash")
        : Result::fail("macOS trash command failed");

#else
    // Linux: freedesktop.org trash spec.
    // Move file to ~/.local/share/Trash/files/
    // and write a .trashinfo metadata file to ~/.local/share/Trash/info/
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    if (!home) return Result::fail("Cannot determine HOME directory");

    fs::path trashFiles = fs::path(home) / ".local/share/Trash/files";
    fs::path trashInfo  = fs::path(home) / ".local/share/Trash/info";

    std::error_code ec;
    fs::create_directories(trashFiles, ec);
    fs::create_directories(trashInfo, ec);

    std::string filename = target.filename().string();
    fs::rename(target, trashFiles / filename, ec);
    if (ec) {
        return Result::fail("Failed to move to trash: " + ec.message());
    }

    // obtain time for timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    std::ostringstream ts;
    ts << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");

    // write the .trashinfo file so the desktop environment
    // knows where the file came from and when it was deleted
    std::ofstream info(trashInfo / (filename + ".trashinfo"));
    info << "[Trash Info]\n"
         << "Path=" << target.string() << "\n"
         << "DeletionDate=" << ts.str() << "\n";

    return Result::ok("Moved to Trash");
#endif
}

}