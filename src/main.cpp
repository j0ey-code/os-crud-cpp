#include "FileManager.h"
#include "FileInfo.h"
#include "Logger.h"
#include "AppPaths.h"
#include "Glob.h"
#include <iostream>
#include <string>
#include <vector>
#include <functional>

namespace fs = std::filesystem;

/*  v2.0 turns filemgr from an interactive, menu-driven console
    program into a proper command-line utility: a single command
    per invocation, with a verb (subcommand) plus its arguments.
    This mirrors how tools like `cp`, `mv`, `rm`, and `git` behave
    and makes filemgr scriptable and pipeable.

    The FileManager backend never knew anything about the UI —
    it only takes (Logger&, ConfirmCallback, dryRun) — so the whole
    shift to a CLI lives right here in main.cpp. */

static constexpr const char* kProgram = "filemgr";
static constexpr const char* kVersion = "2.0";

void printFileInfo(const FileInfo& file) {
    std::cout << "  Name:       " << file.name          << "\n"
              << "  Extension:  " << file.extension      << "\n"
              << "  Path:       " << file.absolutePath   << "\n"
              << "  Size:       " << file.sizeBytes << " bytes\n"
              << "  Directory:  " << (file.isDirectory ? "yes" : "no") << "\n";
    if (file.isDirectory) {
        std::cout << "  Children:   " << file.childCount;
        if (!file.childCountComplete) {
            std::cout << " (incomplete — directory not fully readable)";
        }
        std::cout << "\n";
    }
}

/*  Usage text. Sent to stdout for `--help` (an explicit request)
    and to stderr when the user got the invocation wrong. */
void printUsage(std::ostream& os) {
    os << "Usage: " << kProgram << " [global options] <command> [arguments]\n\n"
       << "Commands:\n"
       << "  inst   <path> [--content <text>]   Create a new file\n"
       << "  mkdir  <path>                      Create a directory\n"
       << "  info   <path>                      Show metadata for a file or directory\n"
       << "  rename <old> <new>                 Rename or change a file extension\n"
       << "  cpy    <src> <dst>                 Copy a file or directory (src may use *)\n"
       << "  mov    <src> <dst>                 Move a file or directory (src may use *)\n"
       << "  del    <path>                      Delete permanently (prompts; path may use *)\n"
       << "  trash  <path>                      Send a file or directory to the trash (may use *)\n"
       << "  tree   <path> [--depth <n>]        Print a recursive directory tree\n"
       << "  log                                Print the audit log\n\n"
       << "Wildcards (cpy, mov, del, trash):\n"
       << "  '*' matches any run of characters within the name OR the extension,\n"
       << "  but never across the '.' between them:\n"
       << "    '*.txt'   every .txt file        'file.*'  file with any extension\n"
       << "    '*.*'     any name with an ext   'log*'    extension-less names\n"
       << "  Quote patterns so your shell doesn't expand them first. For cpy and\n"
       << "  mov, the destination must be an existing directory when src uses '*'.\n\n"
       << "Global options:\n"
       << "  -n, --dry-run    Simulate the operation; make no changes on disk\n"
       << "  -y, --yes        Assume \"yes\" to confirmation prompts (for scripts)\n"
       << "  -h, --help       Show this help text and exit\n"
       << "  -V, --version    Show version information and exit\n\n"
       << "Exit codes: 0 success, 1 operation failed, 2 usage error\n";
}

/*  Parse the command line. argv[0] is always the program name, so
    real flags and arguments begin at argv[1]. We do a single pass:
    recognised flags set state, options that take a value consume the
    next token, and everything else is collected as a positional. The
    first positional is the command; the rest are its arguments.

    Global flags may appear anywhere (before or after the command),
    which is what users expect from a modern CLI. */
int main(int argc, char* argv[]) {
    bool dryRun    = false;
    bool assumeYes = false;
    std::string content;            // for `inst --content`
    int depth = -1;                 // for `tree --depth`; -1 == unlimited
    std::vector<std::string> positionals;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-n" || arg == "--dry-run") {
            dryRun = true;
        } else if (arg == "-y" || arg == "--yes") {
            assumeYes = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(std::cout);
            return 0;
        } else if (arg == "-V" || arg == "--version") {
            std::cout << kProgram << " " << kVersion << "\n";
            return 0;
        } else if (arg == "--content") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --content requires a value\n";
                return 2;
            }
            content = argv[++i];
        } else if (arg == "--depth") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --depth requires a value\n";
                return 2;
            }
            try {
                depth = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "Error: --depth must be an integer\n";
                return 2;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            printUsage(std::cerr);
            return 2;
        } else {
            positionals.push_back(arg);
        }
    }

    if (positionals.empty()) {
        std::cerr << "Error: no command given\n\n";
        printUsage(std::cerr);
        return 2;
    }

    const std::string& command = positionals[0];
    const std::size_t argCount = positionals.size() - 1;
    // Convenience: fetch the Nth argument *after* the command.
    auto argAt = [&](std::size_t idx) -> const std::string& {
        return positionals[idx + 1];
    };
    // Guard for commands that take an EXACT number of positional
    // arguments. Too few is an obvious error; too many usually means a
    // quoting mistake (e.g. an unquoted wildcard the shell expanded, or
    // an unescaped space), so we reject rather than silently dropping the
    // extras — which could otherwise hide a mistake on a destructive verb.
    auto needArgs = [&](std::size_t need) -> bool {
        if (argCount < need) {
            std::cerr << "Error: '" << command << "' requires "
                      << need << " argument" << (need == 1 ? "" : "s")
                      << " (got " << argCount << ")\n";
            return false;
        }
        if (argCount > need) {
            std::cerr << "Error: '" << command << "' takes exactly "
                      << need << " argument" << (need == 1 ? "" : "s")
                      << " (got " << argCount << "); "
                      << "quote paths that contain spaces or '*'\n";
            return false;
        }
        return true;
    };

    /*  Place the audit log in the OS-designated data directory
        (XDG on Linux, Application Support on macOS, LOCALAPPDATA
        on Windows) via AppPaths. */
    fs::path dataDir = appPaths::getDataDir("filemgr");
    std::error_code ec;
    fs::create_directories(dataDir, ec);
    if (ec) {
        std::cerr << "Cannot create data directory: " << ec.message() << "\n";
        return 1;
    }

    Logger logger(dataDir / "history.log");

    /*  The confirmation callback is still the UI's responsibility.
        In CLI mode we honour --yes for non-interactive/scripted use,
        and treat EOF (e.g. a closed/piped stdin) as "no" so that an
        unattended run fails safe rather than deleting something. */
    auto confirmFn = [&](const std::string& prompt) -> bool {
        if (assumeYes) return true;
        std::cout << "⚠ Confirm: " << prompt << "\n"
                  << "Proceed? (y/N): ";
        std::string answer;
        if (!std::getline(std::cin, answer)) return false;
        return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
    };

    FileManager mgr(logger, confirmFn, dryRun);

    if (dryRun) {
        // Diagnostic banner goes to stderr so it never pollutes the
        // stdout of data-producing commands (tree/info/log).
        std::cerr << "*** DRY RUN MODE — no changes will be made ***\n";
    }

    /*  Run one operation over a list of expanded paths, reporting each
        sub-result on stderr (so a multi-file op is transparent) and
        folding them into a single summary Result for the exit code. */
    auto runBatch = [&](const std::vector<fs::path>& matches,
                        const std::function<Result(const fs::path&)>& op) -> Result {
        std::size_t done = 0;
        for (const auto& m : matches) {
            Result r = op(m);
            std::cerr << (r.success ? "[OK] " : "[FAIL] ") << r.message << "\n";
            if (r.success) ++done;
        }
        const std::string tally =
            std::to_string(done) + " of " + std::to_string(matches.size());
        return done == matches.size()
                   ? Result::ok(tally + " item(s) processed")
                   : Result::fail(tally + " item(s) succeeded");
    };

    /*  A single-path command (del, trash). If the path holds a wildcard,
        expand it and run the op over every match; otherwise run once. */
    auto globOneArg = [&](const fs::path& target,
                          const std::function<Result(const fs::path&)>& op) -> Result {
        if (!glob::hasWildcard(target.string())) return op(target);
        std::error_code gec;
        auto matches = glob::expand(target, gec);
        if (gec) return Result::fail("Cannot expand pattern '"
                                     + target.string() + "': " + gec.message());
        if (matches.empty())
            return Result::fail("No matches for pattern: " + target.string());
        return runBatch(matches, op);
    };

    /*  A source→destination command (cpy, mov). A wildcard source may
        match many entries, so the destination must be an existing
        directory to receive them (matching `cp *.txt dir/`). */
    auto globTwoArg = [&](const fs::path& src, const fs::path& dst,
                          const std::function<Result(const fs::path&,
                                                     const fs::path&)>& op) -> Result {
        if (!glob::hasWildcard(src.string())) return op(src, dst);
        std::error_code gec;
        auto matches = glob::expand(src, gec);
        if (gec) return Result::fail("Cannot expand pattern '"
                                     + src.string() + "': " + gec.message());
        if (matches.empty())
            return Result::fail("No matches for pattern: " + src.string());
        if (!fs::is_directory(dst))
            return Result::fail("Destination must be an existing directory for a "
                                "wildcard " + command + ": " + dst.string());
        return runBatch(matches,
            [&](const fs::path& m) -> Result { return op(m, dst); });
    };

    /*  `log` produces data on stdout and needs no path argument,
        so handle it up front and return directly. */
    if (command == "log") {
        if (argCount != 0) {
            std::cerr << "Error: 'log' takes no arguments (got "
                      << argCount << ")\n";
            return 2;
        }
        std::cout << logger.readAll();
        return 0;
    }

    Result res;

    if (command == "inst") {
        if (!needArgs(1)) return 2;
        res = mgr.createFile(argAt(0), content);
    } else if (command == "mkdir") {
        if (!needArgs(1)) return 2;
        res = mgr.createDirectory(argAt(0));
    } else if (command == "info") {
        if (!needArgs(1)) return 2;
        FileInfo info;
        res = mgr.readInfo(argAt(0), info);
        if (res.success) printFileInfo(info);
    } else if (command == "rename") {
        if (!needArgs(2)) return 2;
        res = mgr.rename(argAt(0), argAt(1));
    } else if (command == "cpy") {
        if (!needArgs(2)) return 2;
        res = globTwoArg(argAt(0), argAt(1),
            [&](const fs::path& s, const fs::path& d) { return mgr.copy(s, d); });
    } else if (command == "mov") {
        if (!needArgs(2)) return 2;
        res = globTwoArg(argAt(0), argAt(1),
            [&](const fs::path& s, const fs::path& d) { return mgr.move(s, d); });
    } else if (command == "del") {
        if (!needArgs(1)) return 2;
        res = globOneArg(argAt(0),
            [&](const fs::path& t) { return mgr.remove(t, false); });
    } else if (command == "trash") {
        if (!needArgs(1)) return 2;
        res = globOneArg(argAt(0),
            [&](const fs::path& t) { return mgr.remove(t, true); });
    } else if (command == "tree") {
        if (!needArgs(1)) return 2;
        std::string tree;
        res = mgr.listTree(argAt(0), tree, depth);
        if (res.success) std::cout << tree;
    } else {
        std::cerr << "Error: unknown command '" << command << "'\n\n";
        printUsage(std::cerr);
        return 2;
    }

    /*  Status line goes to stderr (diagnostics), keeping stdout clean
        for piping. The exit code is the real machine-readable signal:
        0 on success, 1 on operation failure. */
    std::cerr << (res.success ? "[OK] " : "[FAIL] ") << res.message << "\n";
    return res.success ? 0 : 1;
}
