## OS CRUD Application v1.0

### Operating System Level CRUD Application in C++, Version 1.0

### FILE MANIFEST

main.cpp           Entry point, menu loop, user I/O
FileManager.h/cpp  Core CRUD logic, execOrSim chokepoint
PlatformOps.h/cpp  OS-specific trash deletion
Logger.h/cpp       Timestamped audit log with immediate flush
Result.h           Success/failure return type
FileInfo.h         File metadata struct
Confirmation.h     Callback type alias for delete approval
AppPaths.h         Cross-platform data directory resolution
CMakeLists.txt     Build configuration

**Desc.:** A small side-project that serves as a cross-platform, file management CLI tool using C++17 that performs CRUD-like operations on files and directories. 
Built with C++17, CMake 3.15+, std::filesystem, and platform-native APIs (Win32 Shell, freedesktop trash spec, macOS AppleScript). No external libraries.

**Purpose:** Built to consolidate multiple Linux filesystem commands within a single, platform-agnostic application.
Reinforced my knowledge / skillset in C++, using filesystem APIs, cross-platform design, and layered program architecture design.

### FEATURES:: 

1. Create Files and Directories
2. Read Metadata in Files and Directories 
3. Rename Files \[and Extensions\] \(**DOES NOT CONVERT FILES!!**\)
4. Delete Permanently or Send to Trash
5. Copy Files and Directories
6. Recursive Directory Tree Listing
7. Audit Logging to Filesystem Specific Logs Location
8. A "Dry-Run" / "Dry-Test" Mode
9. Cross-Platform Compatibility for All 3 Major OSes (Windows, macOS, Linux)
10. Delete File Confirmation Prompts

- Result return type instead of exceptions
    - every operation returns a Result struct w/ a bool success and a std::string message
    - chosen over direct exception handling because filesystem operations fail *regularly*
        - i.e. failure can be an expected outcome, not an exceptional one

- execOrSim() "execute or simulate" function as chokepoint for operations in FileManager.cpp
    - design choice to enforce usage of --dry-run flag for "test" operations
    - *and*, also to reduce side-effects by piping lambda functions thru it which perform the filesystem change(s) within execOrSim()
    - readInfo() *always* executes because reading can never be a destructive operation
    - the function will either run the lambda and logs the result, or "simulates" it as a dry-run and logs a dry-run message

- Delete operations require user approval via callback utilizing the Confirmation.h utility header model

- AppPaths.h handles platform-agnostic log file writing
    - Linux writes to the XDG Base Directory Specification 
        - written to $XDG_DATA_HOME **OR** ~/.local/share/ on Linux distributions
    - macOS *should* write log files to...
        - ~/Library/Application Support/
    - And Windows machines will write the log files to the standard...
        - %LOCALAPPDATA% hidden directory for program logs and metadata

- PlatformOps(.h/.cpp) handles platform / OS-specific "send to trash" deletion
    - this filesystem operation **NOT** supported natively by C++ stdlib

- #pragma once instead of the standard, #ifndef header guards
    - moved beyond pedagogical realm, #pragma once recommended over #ifndef and #endif

### CURRENT ISSUES / LIMITATIONS::

- a "move" operation across filesystems (i.e. from a FAT32 USB to an ext3/4 Linux OS) will create two log entries
    - cross-platform move / rename must fallback and utilize copy operation as well
    - copy() routes itself through execOrSim() independently which generates an additional log entry for the COPY method
    - cross-platform move operation performs and logs both a COPY and a MOVE operation
- renaming file extensions does **NOT** and will **NOT** convert files
- macOS "send to trash" operation ported out to "osascript"
    - could fail if Finder is not available
- no symbolic link awareness or handling of symbolic links
- permissions are displayed as raw enumerated values thru integer(s), rather than in a human-readable format
- last modified time is not in human-readable format either, when invoked and displayed by printFileInfo()
- Linux "send to trash" operation currently performs no de-duplication; not up to filesystem specs!!
    - overwrite if two files of same name are sent to trash, rather than appending duped file

### ARCHITECTURE:: 

- main.cpp serves as our entry point, as per usual
    - owns and manages all direct user interaction
    - read and validates input
    - translates user choices to method calls
- FileManager handles application logic
    - core program logic layer
    - receives (Logger&, ConfirmCallback, bool dryRun) as parameters
    - pairs methods to filesystem operations
    - delegates filesystem changes to lambda functions which pass thru execOrSim() gate
- PlatformOps isolates and coordinates most OS-specific behaviors
    - handles platform specific operations that C++ stdlib cannot
        - i.e. send file to trash
- various header files work as model layer
    - header files serve as models for filesystem info (AppPaths.h, FileInfo.h)
    - or, as models for returned values / states from program functions (Result.h, Confirmation.h)
    - shared model types which provide expanded utility for application 
- chokepoint as execOrSim\(\) "execute or simulate" function in FileManager.cpp
    - all file operation requests flow thru here for consistency and gating

**Dependency Graph:**

main.cpp
  ├── FileManager  →  PlatformOps  →  OS APIs
  ├── Logger
  ├── AppPaths(.h)
  └── (shared types: Result(.h), FileInfo(.h), Confirmation(.h))

### BUILD && EXECUTION INSTRUCTIONS::

First, ensure you have a C++17 compatible compiler installed properly, along with CMake3.15+. 

Then, from the *project's working directory*, run the following commands \(or, the Windows / macOS equivalents\)...

1. mkdir build
2. cd build
3. cmake ..
4. make

This will create the "filemgr" executable within the project's build directory. 

## Changelog

06/23/2026 - first push to GitHub, just to get it out in the repos for now

## TO-DO List

**\[TO-DO\]:: Expand this README.md document significantly.**

**\[TO-DO\]:: Create version 2.0, which will operate with purely a CLI argument parsing feature instead, rather than a text-based GUI menu in the console.**
