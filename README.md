## OS CRUD Application v2.0

### Operating System Level CRUD Application in C++, Version 2.0

### FILE MANIFEST

main.cpp           Entry point, menu loop, user I/O
FileManager.h/cpp  Core CRUD logic, execOrSim chokepoint
PlatformOps.h/cpp  OS-specific trash deletion
Logger.h/cpp       Timestamped audit log with immediate flush
Result.h           Success/failure return type
FileInfo.h         File metadata struct
Confirmation.h     Callback type alias for delete approval
AppPaths.h         Cross-platform data directory resolution
Glob.h             To handle asterisk wildcard character "globbing"
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
    - overwrites if two files of same name are sent to trash, rather than appending duped file

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

## CHANGELOG

- **06/23/2026:**
first push to GitHub, just to get it out in the repos for now

- **07/02/2026:**   
have now moved previous iteration of project to alt GitHub branch on repo as the initial v1.0
updated program implements proper CLI argument parsing functionality, and changes to operation names
additionally, the asterisk (*) character has been added as a wildcard for file names AND / OR extensions
still multiple bugs / errors to clean up, including...
1. High-Prio: **Copy / move directory to own descendant operation produces runaway recursion!!!**
2. High-Prio: Fix Linux filesystem "send to trash" overwrite error on edge case condition of same named files or directories
3. Medi-Prio: **Extra positional arguments in CLI parsing are silently dropped!!**                
4. Medi-Prio: tree operation silently hides subtrees, no report, on denied permissions
5. Medi-Prio: **Move operation treats any and all rename failures as cross-filesystem errors in nature.**
6. Additional, mainly symbolic link specific issues of medium priority (backburner for now)

## v2.0 07/02/2026 PUSH - FURTHER DETAILS 
### Highlights
- **Command-line interface (v2.0).** One verb per invocation (`verb [args]`),
  in the style of `git`/`cp`/`mv`, replacing the old interactive menu. The tool
  is now scriptable and pipeable.
- **`*` filename wildcard** for `cpy`, `mov`, `del`, `trash`.
- **Verb renames** to be less 1:1 with GNU tools: `create is now inst`, `copy is now cpy`,
  `move is now mov`, `rm is now del` (`mkdir`, `info`, `rename`, `trash`, `tree`, `log`
  remain unchanged).

### Design & architecture
- **UI/backend separation.** `FileManager` knows nothing about the CLI - it
  takes `(Logger&, ConfirmCallback, dryRun)` and returns a uniform
  `Result{bool success, string message}`. All CLI logic lives in `main.cpp`.
- **Injected confirmation.** `ConfirmCallback` is a `std::function`; `main`
  supplies a lambda honoring `--yes` and treating EOF/closed stdin as "no"
  (fails safe).
- **Centralized dry-run + audit log** via `FileManager::execOrSim()`; every
  mutating op routes its side effect through it.
- **stdout/stderr discipline.** Data (`tree`/`info`/`log`) to stdout;
  status/diagnostics (`[OK]`/`[FAIL]`, dry-run banner) to stderr.
- **Exit codes** as the machine signal: `0` success, `1` op failed, `2` usage.
- **OS-agnostic paths.** `AppPaths` (log location: XDG / Application Support /
  LOCALAPPDATA) and `PlatformOps` (native trash).

### Wildcard semantics (`src/Glob.h`)
Header-only (`namespace glob`), matching the existing utility-header style.
- `*` is a catch-all **within the name OR the extension**, but **never across
  the `.`** between them (the period is a strict delimiter).
  - `*.txt` matches `notes.txt`, **not** `notes.bak.txt`
  - `file.*` matches `file.md`, `file.cpp`
  - `*.*` any name with an extension
  - bare `*` extension-less names only
- Matching is **case-sensitive** (Linux-like). Expansion is **one directory
  level** (non-recursive) and only in the final path component. Results are
  **sorted**; relative patterns yield relative matches.
- **Integration lives in the CLI layer** (`globOneArg`/`globTwoArg`/`runBatch`
  in `main.cpp`), so the wildcard reuses existing per-file confirmation,
  logging, and dry-run without changes to `FileManager`.
- Wildcard `cpy`/`mov` require the destination to be an existing directory; a
  partial-failure batch reports each item and exits `1`.

## v2.0 07/02/2026 PUSH - REMAINING ISSUES EXPANDED
### Major 1 - Copy/move a directory into its own descendant results in runaway recursion
`cpy box box/inner` (destination inside source) recurses copying-into-itself until the path
hits `ENAMETOOLONG`.
- **Observed:** operation ends in `[FAIL] Copy failed: File name too long`, but only **after
  creating ~818 junk directories nested ~410 levels deep** under `box/inner`. The junk is left
  on disk (partial-failure is not rolled back).
- **Risk:** inode/space exhaustion; deep trees that ordinary `rm` may struggle to remove.
- **Location:** `FileManager::copy` / `FileManager::move` (`src/FileManager.cpp`).

### Major 2 - Trashing two files with the same basename silently destroys the first
Trash `a/dup.txt` (content `FIRST`), then `b/dup.txt` (content `SECOND`).
- **Observed:** both commands exit `0` with `[OK] Moved to Trash`; the trash ends up with a
  **single** `dup.txt` containing `SECOND`. `FIRST` is **permanently lost**, no warning.
- **Related:** `.trashinfo` writes a **relative** `Path=` (observed `Path=trash1.txt`); the
  freedesktop spec requires an absolute path, so desktop "Restore" would misbehave.
- **Location:** Linux branch of `platform::moveToTrash` (`src/PlatformOps.cpp`) — no de-dup /
  no `Path` normalization.

### Minor 1 - Extra positional arguments silently dropped
`filemgr mkdir zz1 zz2` creates **only `zz1`**, exits `0`; `zz2` is discarded with no error.
Same for any command given more positionals than it consumes.
- **Risk:** a scripted `mkdir a b c` (or a mistyped multi-arg call) silently does less than asked
  while reporting success.
- **Location:** command dispatch in `src/main.cpp` — no "too many arguments" check.

### Minor 2 - `tree` / `info` silently hide permission-denied subtrees
With a child directory `chmod 000`, `tree vault` prints the directory name but **omits its contents with no error**, exiting as `0`.
- **Observed:** `secret/` listed, `secret/classified.txt` invisible, `tree rc=0`.
- **Cause:** `directory_iterator(dir, ec)` errors are swallowed into `ec` and never surfaced.
- **Location:** `FileManager::listTree` (`src/FileManager.cpp`).

### Minor 3 - `mov` treats any and all rename failures as cross-filesystem in nature
In `FileManager::move`, any non-zero `ec` from `fs::rename` triggers the copy-then-delete
fallback — not just `EXDEV`. A rename that fails for another reason (e.g. permissions, read-only
destination) will attempt a full copy+delete instead of reporting the real error, and produces a
second (`COPY`) log entry. Could not force a non-EXDEV rename failure in the single-filesystem
sandbox, so this is reported from code review, consistent with the documented caveat.
- **Location:** `src/FileManager.cpp:225-239`.
