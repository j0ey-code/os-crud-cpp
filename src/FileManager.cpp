#include "FileManager.h"
#include "PlatformOps.h"
#include <fstream>
#include <functional>
#include <algorithm>

namespace fs = std::filesystem;

// CUSTOM CONSTRUCTOR; logger reference and dry-run flag

FileManager::FileManager(Logger& logger, ConfirmCallback confirm,
                         bool dryRun) : m_logger(logger),
                                        m_confirm(std::move(confirm)),
                                        m_dryRun(dryRun) 
{
}



// CREATE

Result FileManager::createFile(const fs::path& filepath,
                               const std::string& initialContent) {
    if (fs::exists(filepath)) {
        return Result::fail("Already exists: " + filepath.string());
    }

    auto parentCheck = validateParentExists(filepath);
    if (!parentCheck.success) return parentCheck;

    return execOrSim("CREATE", filepath.string(),
        "create file: " + filepath.string(),
        [&]() -> Result {
            std::ofstream ofs(filepath);
            if (!ofs) {
                return Result::fail("Could not create: " + filepath.string());
            }
            if (!initialContent.empty()) {
                ofs << initialContent;
            }
            return Result::ok("Created: " + filepath.string());
        });
}

Result FileManager::createDirectory(const fs::path& dirpath) {
    if (fs::exists(dirpath)) {
        return Result::fail("Already exists: " + dirpath.string());
    }

    return execOrSim("MKDIR", dirpath.string(),
        "create directory: " + dirpath.string(),
        [&]() -> Result {
            std::error_code ec;
            fs::create_directories(dirpath, ec);
            if (ec) return Result::fail("Failed: " + ec.message());
            return Result::ok("Directory created: " + dirpath.string());
        });
}

// READ 

Result FileManager::readInfo(const fs::path& target, FileInfo& out) {
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return Result::fail("Does not exist: " + target.string());
    }

    out.name         = target.filename().string();
    out.extension    = target.extension().string();
    out.absolutePath = fs::absolute(target).string();
    out.isDirectory  = fs::is_directory(target, ec);
    out.type         = fs::status(target, ec).type();
    out.permissions  = fs::status(target, ec).permissions();
    out.lastModified = fs::last_write_time(target, ec);

    if (out.isDirectory) {
        out.sizeBytes = 0;
        out.childCount = 0;
        for (auto& _ : fs::directory_iterator(target, ec)) {
            ++out.childCount;
        }
    } else {
        out.sizeBytes  = fs::file_size(target, ec);
        out.childCount = 0;
    }

    // Log the read but always execute — reading changes nothing
    m_logger.log("READ", target.string(), "OK: Info retrieved");
    return Result::ok();
}

// UPDATE 

Result FileManager::rename(const fs::path& oldPath,
                           const fs::path& newPath) {
    if (!fs::exists(oldPath)) {
        return Result::fail("Source not found: " + oldPath.string());
    }
    if (fs::exists(newPath)) {
        return Result::fail("Destination already exists: " + newPath.string());
    }

    // fs::rename handles both renaming and "changing extension"
    // because an extension is just part of the filename string.
    // Renaming "photo.jpg" to "photo.png" doesn't convert the
    // image — it only changes the name. If you need actual
    // format conversion, that's a different concern entirely.
    return execOrSim("RENAME", oldPath.string(),
        "rename " + oldPath.string() + " → " + newPath.string(),
        [&]() -> Result {
            std::error_code ec;
            fs::rename(oldPath, newPath, ec);
            if (ec) return Result::fail("Rename failed: " + ec.message());
            return Result::ok("Renamed to: " + newPath.string());
        });
}

// DELETE 

Result FileManager::remove(const fs::path& target, bool useTrash) {
    if (!fs::exists(target)) {
        return Result::fail("Does not exist: " + target.string());
    }

    std::string verb = useTrash ? "move to trash" : "permanently delete";

    // build a descriptive warning for the confirmation prompt
    // for directories, count children so the user knows the scope
    std::string warning = verb + ": " + target.string();
    if (fs::is_directory(target)) {
        std::size_t count = 0;
        std::error_code ec;
        for (auto& _ : fs::recursive_directory_iterator(target, ec)) {
            ++count;
        }
        warning += " (directory with " + std::to_string(count) + " items)";
    }

    // Ask for confirmation. In dry-run mode, skip the prompt —
    // the user already knows nothing will happen.
    if (!m_dryRun && !m_confirm(warning)) {
        m_logger.log("DELETE", target.string(), "CANCELLED by user");
        return Result::ok("Cancelled.");
    }

    return execOrSim("DELETE", target.string(),
        verb + ": " + target.string(),
        [&]() -> Result {
            if (useTrash) {
                return platform::moveToTrash(target);
            }
            std::error_code ec;
            if (fs::is_directory(target)) {
                fs::remove_all(target, ec);
            } else {
                fs::remove(target, ec);
            }
            if (ec) return Result::fail("Delete failed: " + ec.message());
            return Result::ok("Permanently deleted: " + target.string());
        });
}

// COPY

Result FileManager::copy(const fs::path& source,
                         const fs::path& destination) {
    if (!fs::exists(source)) {
        return Result::fail("Source not found: " + source.string());
    }

    // If the destination is an existing directory, copy INTO it
    // rather than failing. This mirrors how `cp` behaves:
    //   cp notes.txt ~/backup/   →   ~/backup/notes.txt
    fs::path actualDest = destination;
    if (fs::is_directory(destination)) {
        actualDest = destination / source.filename();
    }

    if (fs::exists(actualDest)) {
        return Result::fail("Destination already exists: "
                            + actualDest.string());
    }

    return execOrSim("COPY", source.string(),
        "copy " + source.string() + " → " + actualDest.string(),
        [&]() -> Result {
            std::error_code ec;
            if (fs::is_directory(source)) {
                fs::copy(source, actualDest,
                         fs::copy_options::recursive, ec);
            } else {
                fs::copy_file(source, actualDest, ec);
            }
            if (ec) return Result::fail("Copy failed: " + ec.message());
            return Result::ok("Copied to: " + actualDest.string());
        });
}

// MOVE

Result FileManager::move(const fs::path& source,
                         const fs::path& destination) {
    if (!fs::exists(source)) {
        return Result::fail("Source not found: " + source.string());
    }

    fs::path actualDest = destination;
    if (fs::is_directory(destination)) {
        actualDest = destination / source.filename();
    }

    if (fs::exists(actualDest)) {
        return Result::fail("Destination already exists: "
                            + actualDest.string());
    }

    // fs::rename works as a move when crossing directories.
    // On the same filesystem it's nearly instant (just a metadata
    // update). Across filesystems, some implementations will fail —
    // in that case you'd fall back to copy-then-delete.
    return execOrSim("MOVE", source.string(),
        "move " + source.string() + " → " + actualDest.string(),
        [&]() -> Result {
            std::error_code ec;
            fs::rename(source, actualDest, ec);
            if (ec) {
                auto copyRes = copy(source, actualDest);
                if (!copyRes.success) return copyRes;
                if (fs::is_directory(source)) {
                    fs::remove_all(source, ec);
                } else {
                    fs::remove(source, ec);
                }
                if (ec) {
                    return Result::fail(
                        "Copied but failed to remove original: "
                        + ec.message());
                }
            }
            return Result::ok("Moved to: " + actualDest.string());
        });
}

// RECURSIVE LISTING 

Result FileManager::listTree(const fs::path& dirpath,
                             std::string& output,
                             int maxDepth) {
    if (!fs::exists(dirpath)) {
        return Result::fail("Does not exist: " + dirpath.string());
    }
    if (!fs::is_directory(dirpath)) {
        return Result::fail("Not a directory: " + dirpath.string());
    }

    // Start with the root directory name
    output += dirpath.filename().string() + "/\n";

    // Use a helper lambda for recursion so we can track depth
    // and build the tree-drawing characters.
    std::function<void(const fs::path&, const std::string&, int)>
        walk = [&](const fs::path& dir,
                   const std::string& prefix,
                   int currentDepth) {

        if (maxDepth >= 0 && currentDepth >= maxDepth) return;

        // Collect and sort entries so output is deterministic.
        // directory_iterator order is OS-dependent — on Linux it's
        // essentially random (inode order), so sorting alphabetically
        // makes the tree readable and reproducible.
        std::vector<fs::directory_entry> entries;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry& a,
                     const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        });

        for (std::size_t i = 0; i < entries.size(); ++i) {
            bool isLast = (i == entries.size() - 1);
            auto& entry = entries[i];

            // Tree-drawing characters:
            //   ├── for items with siblings below them
            //   └── for the last item at this level
            //   │   continues a vertical line from a parent
            //       (spaces) when no vertical line is needed
            std::string connector = isLast ? "└── " : "├── ";
            std::string extension = isLast ? "    " : "│   ";

            std::string name = entry.path().filename().string();
            if (entry.is_directory(ec)) {
                name += "/";
            }

            output += prefix + connector + name + "\n";

            if (entry.is_directory(ec)) {
                walk(entry.path(), prefix + extension, currentDepth + 1);
            }
        }
    };

    walk(dirpath, "", 0);
    return Result::ok();
}

//  PRIVATE 

Result FileManager::validateParentExists(const fs::path& p) {
    auto parent = p.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        return Result::fail("Parent directory does not exist: "
                            + parent.string());
    }
    return Result::ok();

}

/*  Helper function which checks the dry-run flag, logs the operation, 
        and returns a Result. Every public method calls this at the point 
        where it would perform its side effect. */
//  return Result "execute or simulate"
Result FileManager::execOrSim(const std::string& operation,
                     const std::string& target,
                     const std::string& desc,
                     std::function<Result()> action) {
    
    if (m_dryRun) {
        std::string msg = "[DRY RUN] Would " + desc;
        m_logger.log(operation + " (dry-run)", target, msg);
        return Result::ok(msg);
    }

    Result res = action();
    m_logger.log(operation, target,
                 (res.success ? "OK: " : "FAIL: ") + res.message);
    return res;
}

