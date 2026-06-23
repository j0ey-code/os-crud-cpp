#pragma once
#include <string>
#include <filesystem>
#include <cstdint>

struct FileInfo {
    std::string name;
    std::string extension;
    std::string absolutePath;
    std::uintmax_t sizeBytes = 0;   // safety default values for member variables
    std::filesystem::file_type type = std::filesystem::file_type::none;
    std::filesystem::perms permissions = std::filesystem::perms::none;
    std::filesystem::file_time_type lastModified;
    bool isDirectory = false;
    std::size_t childCount = 0; // only meaningful for directories
};
