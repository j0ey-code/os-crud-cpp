#include "Logger.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

Logger::Logger(const std::filesystem::path& logPath)
    : m_path(logPath)
    , m_stream(logPath, std::ios::app)  // app = append mode
{
}

Logger::~Logger() {
    if (m_stream.is_open()) {
        m_stream.close();
    }
}

void Logger::log(const std::string& operation,
                 const std::string& target,
                 const std::string& outcome) {
    if (!m_stream.is_open()) return;

    // Format: [2026-06-14 15:30:42] CREATE  /home/user/test.txt  OK: Created
    m_stream << "[" << timestamp() << "] "
             << operation << "  "
             << target << "  "
             << outcome << "\n";

    // Flush immediately so the log survives a crash.
    // Buffered writes are faster but useless if the program
    // dies before the buffer is written to disk — and a file
    // management tool that deletes things is exactly the kind
    // of program where you want crash-safe logging.
    m_stream.flush();
}

std::string Logger::readAll() const {
    std::ifstream in(m_path);
    if (!in) return "(no log file found)";

    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::filesystem::path Logger::getPath() const {
    return m_path;
}

std::string Logger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
    #ifdef _WIN32
        localtime_s(&tm_buf, &time);     // Windows: destination first, then source
    #else
        localtime_r(&time, &tm_buf);     // POSIX: source first, then destination
    #endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
