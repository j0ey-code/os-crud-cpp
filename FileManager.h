#pragma once
#include "Result.h"
#include "FileInfo.h"
#include "Logger.h"
#include "Confirmation.h"
#include <filesystem>
#include <functional>
#include <string>

class FileManager {
public:
    /*  custom Constructor takes a Logger reference and
        a dry-run flag. The Logger is created externally
        using its class / object within the main.cpp, so
        main.cpp is controlling where the log file lives */
    explicit FileManager(Logger& logger, ConfirmCallback confirm,
                         bool dryRun = false);

    // CREATE
    Result createFile(const std::filesystem::path& filepath,
                      const std::string& initialContent = "");
    Result createDirectory(const std::filesystem::path& dirpath);

    // READ
    Result readInfo(const std::filesystem::path& target, FileInfo& out);

    // UPDATE
    Result rename(const std::filesystem::path& oldPath,
                  const std::filesystem::path& newPath);

    // DELETE
    Result remove(const std::filesystem::path& target, bool useTrash);

    // COPY & MOVE
    Result copy(const std::filesystem::path& source, const std::filesystem::path& destination);
    
    Result move(const std::filesystem::path& source, const std::filesystem::path& destination);

    // RECURSIVE LISTING 
    Result listTree(const std::filesystem::path& dirpath, std::string& output, int maxDepth = -1);

    // dry-run flag check
    bool isDryRun() const { return m_dryRun; }

private:
    Logger& m_logger;
    ConfirmCallback m_confirm;
    bool m_dryRun;

    // Internal validation that both create and update operations need
    Result validateParentExists(const std::filesystem::path& p);

    /*  Helper function which checks the dry-run flag, logs the operation, 
        and returns a Result. Every public method calls this at the point 
        where it would perform its side effect. */
    //  return Result "execute or simulate"
    Result execOrSim(const std::string& operation,
                     const std::string& target,
                     const std::string& desc,
                     std::function<Result()> action);
};

