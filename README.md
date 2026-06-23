## OS CRUD Application v1.0

### Operating System Level CRUD Application in C++, Version 1

Desc.: A small side-project that serves as a cross-platform, file management CLI tool using C++17 that performs CRUD-like operations on files and directories. 

Purpose: Built to consolidate multiple Linux file commands within a single application.
Reinforced my knowledge / skillset in C++, using filesystem APIs, cross-platform design, and layered program architecture design.

Features: 

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

Architecture:

- main.cpp handles UI and 
- FileManager handles application logic
- PlatformOps isolates and coordinates most OS-specific behaviors
- various header files provide utility through shared types
- chokepoint as execOrSim\(\) "execute or simulate" function in FileManager.cpp
    - all file operation requests flow thru here for consistency and gating

Build Instructions:

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
**\[TO-DO\]:: Create version 2.0, which will operate with purely a CLI argument parsing feature instead, rather than a text-based menu in the console.**
