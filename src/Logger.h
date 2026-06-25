#pragma once
#include <string>
#include <fstream>
#include <filesystem>

class Logger {
public:
    // Opens (or creates) the log file at the given path.
    // Appends by default so history accumulates across sessions.
    explicit Logger(const std::filesystem::path& logPath);
    ~Logger();

    void log(const std::string& operation,
             const std::string& target,
             const std::string& outcome);

    // Returns the full log contents as a string for display
    std::string readAll() const;

    // Returns the path so the UI can tell the user where it lives
    std::filesystem::path getPath() const;

private:
    std::filesystem::path m_path;
    std::ofstream m_stream;

    std::string timestamp() const;
};
