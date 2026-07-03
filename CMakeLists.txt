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
endif()