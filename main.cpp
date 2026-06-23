#include "FileManager.h"
#include "FileInfo.h"
#include "Logger.h"
#include "AppPaths.h"
#include <iostream>
#include <string>
#include <cstring>

void printFileInfo(const FileInfo& file) {
    std::cout << "  Name:       " << file.name          << "\n"
              << "  Extension:  " << file.extension      << "\n"
              << "  Path:       " << file.absolutePath   << "\n"
              << "  Size:       " << file.sizeBytes << " bytes\n"
              << "  Directory:  " << (file.isDirectory ? "yes" : "no") << "\n";
    if (file.isDirectory) {
        std::cout << "  Children:   " << file.childCount << "\n";
    }
}

/*  Parse --dry-run from CLI args, argc is the count of 
    the arguments, argv is the array. argv[0] is ALWAYS
    the program name itself, so actual flags and args 
    begin at char* argv[1] */
int main(int argc, char* argv[]) {
    bool dryRun = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dry-run") == 0) {
            dryRun = true;
        }
    }

    /* Place the log file next to the executable.
       You could also put it in ~/.local/share/filemgr/
       or respect $XDG_DATA_HOME for a more polished tool
       Logger logger("filemgr.log"); */

    // new platform-agnostic implementation to write output files specifically
    // to the operating system's designated path for said kind of
    // file (log, config, cache, etc.) and the OS itself (Windows, macOS, Linux)
    namespace fs = std::filesystem;
    fs::path dataDir = appPaths::getDataDir("filemgr");

    std::error_code ec;
    fs::create_directories(dataDir, ec);
    if (ec) {
        std::cerr << "Cannot create data directory: " << ec.message() << "\n";
        return 1;
    }

    Logger logger(dataDir / "history.log");

    // the confirmation callback; this is the UI's responsibility
    // FileManager calls this function whenever it needs user approval
    auto confirmFn = [](const std::string& prompt) -> bool {
        std::cout << "⚠ Confirm: " << prompt << "\n"
                  << "Proceed? (y/n): ";
        std::string answer;
        std::getline(std::cin, answer);
        return !answer.empty()
            && (answer[0] == 'y' || answer[0] == 'Y');
    };

    FileManager mgr(logger, confirmFn, dryRun);

    if (dryRun) {
        std::cout << "*** DRY RUN MODE — no changes will be made ***\n";
    }
    std::cout << "Log file: " << logger.getPath() << "\n";

    int choice = 0;

    while (true) {
        std::cout << "\n=== File Agent ===\n"
                  << "1) Create File\n"
                  << "2) Create Directory\n"
                  << "3) Read File / Directory Info\n"
                  << "4) Rename / Change Extension\n"
                  << "5) Delete (Permanent)\n"
                  << "6) Delete (Send to Trash)\n"
                  << "7) Copy File / Directory\n"
                  << "8) Move File / Directory\n"
                  << "9) List Directory Tree\n"
                  << "10) View audit log\n"
                  << "0) Exit\n"
                  << "Choice: ";
        std::cin >> choice;
        if (std::cin.fail()) {  // failure condition for non-integer input
            std::cin.clear();             // reset the fail bit
            std::cin.ignore(10000, '\n'); // discard the bad input
            std::cout << "Invalid input - enter a number.\n";
            continue;
        }
        std::cin.ignore();

        if (choice == 0) break;

        // option 10 does not require a path; "short-circuiter"
        if (choice == 10) {
            std::cout << "\n--- Audit Log ---\n"
                      << logger.readAll()
                      << "--- End ---\n";
            continue;
        }

        std::string path;
        std::cout << "Path: ";
        std::getline(std::cin, path);

        Result res;

        switch (choice) {
            case 1: {
                res = mgr.createFile(path);
                break;
            } 
            case 2: {
                res = mgr.createDirectory(path);
                break;
            }
            case 3: {
                FileInfo info;
                res = mgr.readInfo(path, info);
                if (res.success) printFileInfo(info);
                break;
            }
            case 4: {
                std::string newPath;
                std::cout << "New name/path: ";
                std::getline(std::cin, newPath);
                res = mgr.rename(path, newPath);
                break;
            }
            case 5: {
                res = mgr.remove(path, false);
                break;
            } 
            case 6: {
                res = mgr.remove(path, true);
                break;
            }
            case 7: {
                std::string dest;
                std::cout << "Destination: ";
                std::getline(std::cin, dest);
                res = mgr.copy(path, dest);
                break;
            }
            case 8: {
                std::string dest;
                std::cout << "Destination: ";
                std::getline(std::cin, dest);
                res = mgr.move(path, dest);
                break;
            }
            case 9: {
                std::string tree;
                res = mgr.listTree(path, tree);
                if (res.success) {
                    std::cout << "\n" << tree;
                }
            break;
            }
            default: {
                res = Result::fail("Invalid option: " + std::to_string(choice));
                break;
            }
        
        }
        std::cout << (res.success ? "[OK] " : "[FAIL] ")
            << res.message << "\n";
    }
    return 0;
}
