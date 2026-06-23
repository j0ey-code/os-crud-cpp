#pragma once
#include "Result.h"
#include <filesystem>

namespace platform {
    // Moves a file/directory to the OS trash instead of permanent deletion.
    Result moveToTrash(const std::filesystem::path& target);
}
