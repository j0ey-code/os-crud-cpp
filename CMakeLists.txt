cmake_minimum_required(VERSION 3.15)
project(FileAgent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(filemgr
    src/main.cpp
    src/FileManager.cpp
    src/PlatformOps.cpp
    src/Logger.cpp
)

if(WIN32)
    target_link_libraries(filemgr PRIVATE shell32)

    # UTF-8 process code page, so non-ASCII filenames survive argv and
    # std::string round-trips instead of being "best-fit" mangled.
    if(MSVC)
        target_sources(filemgr PRIVATE src/win/filemgr.manifest)
        # Sources contain UTF-8 literals (tree glyphs, prompts).
        target_compile_options(filemgr PRIVATE /utf-8)
    else()
        enable_language(RC)
        target_sources(filemgr PRIVATE src/win/filemgr.rc)
    endif()
endif()
