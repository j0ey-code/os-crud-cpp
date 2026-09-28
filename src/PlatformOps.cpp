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
#elif defined(__APPLE__)
    #include <spawn.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>
    extern char** environ;
#endif

namespace platform {

Result moveToTrash(const std::filesystem::path& target) {
#ifdef _WIN32
    // Windows: use SHFileOperationW with FOF_ALLOWUNDO
    // The ALLOWUNDO flag is what makes it go to the Recycle Bin
    // rather than being permanently destroyed.
    //
    // ...but the SHFILEOPSTRUCT docs only promise that for a fully
    // qualified path: given a relative one, FO_DELETE is documented to
    // ignore FOF_ALLOWUNDO and delete permanently. Current Windows does
    // recycle relative paths, but we don't bet the user's files on
    // undocumented behaviour, so always hand it an absolute path.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path abs = fs::absolute(target, ec);
    if (ec) return Result::fail("Cannot resolve path: " + ec.message());
    // lexically_normal() also turns '/' into the native '\' separator.
    abs = abs.lexically_normal();
    if (!abs.has_filename()) abs = abs.parent_path();  // drop trailing '\'

    std::wstring widePath = abs.wstring();
    widePath.push_back(L'\0'); // SHFileOperation needs double-null termination

    SHFILEOPSTRUCTW op = {};
    op.wFunc  = FO_DELETE;
    op.pFrom  = widePath.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI
              | FOF_SILENT;

    int result = SHFileOperationW(&op);
    if (result != 0) {
        return Result::fail("SHFileOperation failed with code "
                            + std::to_string(result));
    }
    // With FOF_NOCONFIRMATION/FOF_NOERRORUI the shell may abort part of
    // the operation silently yet still return 0.
    if (op.fAnyOperationsAborted) {
        return Result::fail("Move to Recycle Bin was aborted");
    }
    return Result::ok("Moved to Recycle Bin");

#elif defined(__APPLE__)
    // macOS: the simplest reliable approach is calling
    // osascript to invoke Finder's "move to trash" AppleScript,
    // or using the NSFileManager API via Objective-C++.
    // For a pure-C++ project, the osascript route works.
    //
    // The path must NEVER be spliced into a command line or the script
    // text: a filename is attacker-controlled data (think of an extracted
    // archive), and `x'$(cmd)'.txt` would run `cmd` through the shell.
    // Instead osascript is spawned directly (no shell) and receives the
    // path as its own argv entry, which the script reads as plain text.
    namespace fs = std::filesystem;

    // Finder has no notion of our working directory, so it needs an
    // absolute path. absolute() (not canonical()) keeps a symlink
    // pointing at the link itself rather than its target.
    std::error_code ec;
    fs::path abs = fs::absolute(target, ec);
    if (ec) return Result::fail("Cannot resolve path: " + ec.message());
    const std::string pathArg = abs.lexically_normal().string();

    const char* args[] = {
        "osascript",
        "-e", "on run argv",
        "-e", "set f to POSIX file (item 1 of argv)",
        "-e", "tell application \"Finder\" to delete f",
        "-e", "end run",
        pathArg.c_str(),
        nullptr
    };

    // Finder echoes a reference to the trashed item on stdout; keep that
    // out of filemgr's own output. stderr is left alone so errors such
    // as "Not authorized to send Apple events" still reach the user.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                     "/dev/null", O_WRONLY, 0);

    pid_t pid = 0;
    int spawnErr = posix_spawnp(&pid, "osascript", &actions, nullptr,
                                const_cast<char* const*>(args), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawnErr != 0) {
        return Result::fail(std::string("Could not run osascript: ")
                            + std::strerror(spawnErr));
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            return Result::fail("Lost track of osascript process");
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0
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

    // Resolve the original location to an ABSOLUTE path *before* moving.
    // The freedesktop.org spec requires Path= to be absolute so the
    // desktop's "Restore" knows where the file came from. We use
    // absolute()+lexically_normal() rather than weakly_canonical() so a
    // trashed symlink records its own path, not its target's.
    fs::path origin = fs::absolute(target, ec);
    if (ec) { origin = target; ec.clear(); }
    origin = origin.lexically_normal();

    // De-duplicate the name within the trash. Without this, trashing a
    // second file that shares a basename would have fs::rename silently
    // overwrite — and permanently destroy — the first one. Keep the
    // moved file and its .trashinfo basenames in lock-step.
    const std::string baseName = target.filename().string();
    std::string trashName = baseName;
    // (Non-throwing exists(): if the trash can't even be inspected, fail
    // cleanly rather than abort.)
    for (int i = 1;; ++i) {
        const bool taken =
            fs::exists(trashFiles / trashName, ec) ||
            (!ec && fs::exists(trashInfo / (trashName + ".trashinfo"), ec));
        if (ec) {
            return Result::fail("Cannot access trash: " + ec.message());
        }
        if (!taken) break;
        trashName = baseName + "." + std::to_string(i);
    }

    fs::rename(target, trashFiles / trashName, ec);
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
    std::ofstream info(trashInfo / (trashName + ".trashinfo"));
    info << "[Trash Info]\n"
         << "Path=" << origin.string() << "\n"
         << "DeletionDate=" << ts.str() << "\n";

    return Result::ok("Moved to Trash");
#endif
}

}