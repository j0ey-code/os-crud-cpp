#include "FileManager.h"
#include "PlatformOps.h"
#include <fstream>
#include <functional>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
/*  True if `dest` is the same location as `source`, or nested somewhere
    inside it. Copying a directory into itself or one of its own
    descendants otherwise recurses until the path length explodes
    ("File name too long"), leaving thousands of junk directories behind.

    Both paths are normalised to absolute form first (weakly_canonical
    resolves the existing prefix without requiring `dest` to exist yet;
    if that fails we fall back to a lexical normalisation). We then ask
    for `dest` relative to `source`: an empty result means they are
    unrelated, "." means they are equal, and any result NOT beginning
    with ".." means `dest` sits underneath `source`. */
bool isSelfOrDescendant(const fs::path& source, const fs::path& dest) {
    std::error_code ec;
    fs::path s = fs::weakly_canonical(source, ec);
    if (ec) { s = fs::absolute(source, ec).lexically_normal(); ec.clear(); }
    fs::path d = fs::weakly_canonical(dest, ec);
    if (ec) { d = fs::absolute(dest, ec).lexically_normal(); }

    fs::path rel = d.lexically_relative(s);
    if (rel.empty()) return false;          // unrelated / different roots
    if (rel == fs::path(".")) return true;  // same directory
    return *rel.begin() != "..";            // no leading ".." => nested
}

/*  Existence checks that can't crash. The throwing fs::exists() raises
    an exception when it can't TELL whether a path exists (e.g. permission
    denied on a parent directory), and nothing caught it, so the whole
    process aborted with a core dump. These report the real reason
    instead, and never mistake "can't look" for "not there". */
Result requireExists(const fs::path& p, const std::string& notFoundMsg) {
    std::error_code ec;
    if (fs::exists(p, ec)) return Result::ok();
    if (ec) return Result::fail("Cannot access " + p.string() + ": " + ec.message());
    return Result::fail(notFoundMsg + p.string());
}

Result requireAbsent(const fs::path& p, const std::string& existsMsg) {
    std::error_code ec;
    const bool there = fs::exists(p, ec);
    if (ec) return Result::fail("Cannot access " + p.string() + ": " + ec.message());
    if (there) return Result::fail(existsMsg + p.string());
    return Result::ok();
}

// Non-throwing is_directory(); "can't tell" counts as not a directory,
// and the operation that follows reports the underlying error itself.
bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

fs::path parentOrDot(const fs::path& p) {
    return p.has_parent_path() ? p.parent_path() : fs::path(".");
}

// True if p's directory holds an entry spelled EXACTLY like p's filename
// (a case-insensitive exists() can't tell Report.txt from report.txt).
bool hasExactEntry(const fs::path& p) {
    std::error_code ec;
    fs::directory_iterator it(parentOrDot(p), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it->path().filename().native() == p.filename().native()) {
            return true;
        }
    }
    return false;
}

/*  True if `to` is merely a different spelling of `from`'s own directory
    entry, e.g. Report.txt -> report.txt on a case-insensitive filesystem
    (the default on Windows and macOS), or a Unicode-normalisation change
    on macOS. There, exists(to) sees `from` itself, which must not count
    as a conflict.

    Every check below is needed to rule out a genuinely DIFFERENT file,
    which a rename would otherwise overwrite:
      - names must actually differ, in the same directory;
      - equivalent() must report the same file (so a.txt -> b.txt is
        refused when b.txt resolves to an unrelated B.txt);
      - neither side may be a symlink, since equivalent() follows links
        and would equate a link with its target;
      - no entry spelled exactly like `to` may exist (a hard link to the
        same file is still a separate entry). */
bool isRespellingOf(const fs::path& from, const fs::path& to) {
    if (from.filename().native() == to.filename().native()) return false;

    std::error_code ec;
    if (!fs::equivalent(parentOrDot(from), parentOrDot(to), ec) || ec) {
        return false;
    }

    if (fs::is_symlink(fs::symlink_status(from, ec)) || ec) return false;
    if (fs::is_symlink(fs::symlink_status(to, ec)) || ec) return false;
    if (!fs::equivalent(from, to, ec) || ec) return false;

    return !hasExactEntry(to);
}

/*  Carry out a respelling rename. NTFS and APFS do this directly, but
    some filesystems (FAT, certain network shares, Wine over a Linux
    filesystem) treat it as a no-op that still reports success. So
    confirm the new spelling actually took; if not, go through a
    temporary name, and if even that doesn't stick, report failure
    rather than claiming a rename that never happened. */
std::error_code renameRespelling(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec || hasExactEntry(to)) return ec;

    fs::path tmp = from;
    tmp += ".filemgr-tmp";
    for (int i = 1; fs::exists(tmp, ec) || ec; ++i) {
        if (ec) return ec;
        tmp = from;
        tmp += ".filemgr-tmp" + std::to_string(i);
    }
    fs::rename(from, tmp, ec);
    if (ec) return ec;
    fs::rename(tmp, to, ec);
    if (ec) {
        std::error_code undo;
        fs::rename(tmp, from, undo);   // put the original name back
        return ec;
    }
    if (!hasExactEntry(to)) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    return {};
}
}  // namespace

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
    auto absent = requireAbsent(filepath, "Already exists: ");
    if (!absent.success) return absent;

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
    auto absent = requireAbsent(dirpath, "Already exists: ");
    if (!absent.success) return absent;

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
    // Distinguish "not there" from "can't look" (permission denied),
    // which used to be reported misleadingly as "Does not exist".
    auto present = requireExists(target, "Does not exist: ");
    if (!present.success) return present;

    std::error_code ec;
    out.name         = target.filename().string();
    out.extension    = target.extension().string();
    out.absolutePath = fs::absolute(target, ec).string();
    out.isDirectory  = fs::is_directory(target, ec);
    out.type         = fs::status(target, ec).type();
    out.permissions  = fs::status(target, ec).permissions();
    out.lastModified = fs::last_write_time(target, ec);

    if (out.isDirectory) {
        out.sizeBytes = 0;
        out.childCount = 0;
        // Open explicitly so a permission-denied directory reports an
        // incomplete count rather than a misleading "0 children".
        // Step with increment(ec): the range-for form throws (and
        // aborts) if an error surfaces partway through the listing.
        std::error_code dirEc;
        fs::directory_iterator dirIt(target, dirEc), end;
        for (; !dirEc && dirIt != end; dirIt.increment(dirEc)) {
            ++out.childCount;
        }
        if (dirEc) {
            out.childCountComplete = false;
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
    auto present = requireExists(oldPath, "Source not found: ");
    if (!present.success) return present;

    const bool respelling = isRespellingOf(oldPath, newPath);
    if (!respelling) {
        auto absent = requireAbsent(newPath, "Destination already exists: ");
        if (!absent.success) return absent;
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
            if (respelling) {
                ec = renameRespelling(oldPath, newPath);
            } else {
                fs::rename(oldPath, newPath, ec);
            }
            if (ec) return Result::fail("Rename failed: " + ec.message());
            return Result::ok("Renamed to: " + newPath.string());
        });
}

// DELETE 

Result FileManager::remove(const fs::path& target, bool useTrash) {
    auto present = requireExists(target, "Does not exist: ");
    if (!present.success) return present;

    std::string verb = useTrash ? "move to trash" : "permanently delete";

    // build a descriptive warning for the confirmation prompt
    // for directories, count children so the user knows the scope
    std::string warning = verb + ": " + target.string();
    if (isDir(target)) {
        // Step with increment(ec): the range-for form throws on the first
        // unreadable subdirectory, which aborted the whole program. If the
        // walk stops early, say the count is a lower bound rather than
        // understate what the user is about to delete.
        std::size_t count = 0;
        std::error_code ec;
        fs::recursive_directory_iterator it(target, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            ++count;
        }
        warning += ec ? " (directory with at least " + std::to_string(count)
                            + " items; some could not be read)"
                      : " (directory with " + std::to_string(count) + " items)";
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
            if (isDir(target)) {
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
    auto present = requireExists(source, "Source not found: ");
    if (!present.success) return present;

    // If the destination is an existing directory, copy INTO it
    // rather than failing. This mirrors how `cp` behaves:
    //   cp notes.txt ~/backup/   →   ~/backup/notes.txt
    fs::path actualDest = destination;
    if (isDir(destination)) {
        actualDest = destination / source.filename();
    }

    // Guard against recursively copying a directory into itself or a
    // descendant, which would loop until the path length explodes.
    if (isDir(source) && isSelfOrDescendant(source, actualDest)) {
        return Result::fail("Refusing to copy a directory into itself or a "
                            "subdirectory of itself: " + source.string());
    }

    auto absent = requireAbsent(actualDest, "Destination already exists: ");
    if (!absent.success) return absent;

    return execOrSim("COPY", source.string(),
        "copy " + source.string() + " → " + actualDest.string(),
        [&]() -> Result {
            std::error_code ec;
            if (isDir(source)) {
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
    auto present = requireExists(source, "Source not found: ");
    if (!present.success) return present;

    // A case-only respelling (mov Data data) is a rename in place. It must
    // be caught first: `data` "is a directory" (it's Data itself), so the
    // copy-into rule below would try to move Data into Data/Data.
    const bool respelling = isRespellingOf(source, destination);

    fs::path actualDest = destination;
    if (!respelling && isDir(destination)) {
        actualDest = destination / source.filename();
    }

    if (!respelling) {
        auto absent = requireAbsent(actualDest, "Destination already exists: ");
        if (!absent.success) return absent;
    }

    // fs::rename works as a move when crossing directories, and on the
    // same filesystem it's nearly instant (just a metadata update).
    return execOrSim("MOVE", source.string(),
        "move " + source.string() + " → " + actualDest.string(),
        [&]() -> Result {
            std::error_code ec;
            if (respelling) {
                ec = renameRespelling(source, actualDest);
                if (ec) return Result::fail("Move failed: " + ec.message());
            } else {
                fs::rename(source, actualDest, ec);
            }
            if (!ec) {
                return Result::ok("Moved to: " + actualDest.string());
            }

            // Only a genuine cross-filesystem rename justifies the
            // copy-then-delete fallback. Every OTHER rename failure
            // (permission denied, invalid target such as moving a
            // directory into itself, etc.) is reported as-is rather
            // than attempting a copy that could do something surprising.
            if (ec != std::errc::cross_device_link) {
                return Result::fail("Move failed: " + ec.message());
            }

            // Cross-device move: copy across, then remove the original.
            auto copyRes = copy(source, actualDest);
            if (!copyRes.success) {
                // Clean up whatever the failed copy left behind so a
                // botched cross-device move doesn't litter the target.
                std::error_code cleanupEc;
                fs::remove_all(actualDest, cleanupEc);
                return Result::fail("Cross-device move failed during copy: "
                                    + copyRes.message);
            }

            std::error_code rmEc;
            if (isDir(source)) {
                fs::remove_all(source, rmEc);
            } else {
                fs::remove(source, rmEc);
            }
            if (rmEc) {
                return Result::fail(
                    "Copied to destination but could not remove original: "
                    + rmEc.message());
            }
            return Result::ok("Moved to: " + actualDest.string());
        });
}

// RECURSIVE LISTING 

Result FileManager::listTree(const fs::path& dirpath,
                             std::string& output,
                             int maxDepth) {
    auto present = requireExists(dirpath, "Does not exist: ");
    if (!present.success) return present;
    if (!isDir(dirpath)) {
        return Result::fail("Not a directory: " + dirpath.string());
    }

    // Start with the root directory name
    output += dirpath.filename().string() + "/\n";

    // Count directories we couldn't read so the tree can flag them
    // instead of silently rendering them as empty (which would make an
    // incomplete listing look complete).
    std::size_t unreadable = 0;

    // Use a helper lambda for recursion so we can track depth
    // and build the tree-drawing characters.
    std::function<void(const fs::path&, const std::string&, int)>
        walk = [&](const fs::path& dir,
                   const std::string& prefix,
                   int currentDepth) {

        if (maxDepth >= 0 && currentDepth >= maxDepth) return;

        // Open the directory explicitly so we can distinguish "empty"
        // from "couldn't read" (e.g. permission denied). A silent empty
        // listing here is exactly the trap we want to avoid.
        std::error_code ec;
        fs::directory_iterator dirIt(dir, ec);
        if (ec) {
            output += prefix + "└── [unreadable: " + ec.message() + "]\n";
            ++unreadable;
            return;
        }

        // Collect and sort entries so output is deterministic.
        // directory_iterator order is OS-dependent — on Linux it's
        // essentially random (inode order), so sorting alphabetically
        // makes the tree readable and reproducible.
        // Step with increment(ec) so an error partway through the listing
        // is reported with a marker instead of throwing.
        std::vector<fs::directory_entry> entries;
        std::error_code iterEc;
        for (fs::directory_iterator end; !iterEc && dirIt != end;
             dirIt.increment(iterEc)) {
            entries.push_back(*dirIt);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry& a,
                     const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        });

        for (std::size_t i = 0; i < entries.size(); ++i) {
            // If the listing broke off, the marker below is the last line.
            bool isLast = !iterEc && (i == entries.size() - 1);
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

        if (iterEc) {
            output += prefix + "└── [unreadable: " + iterEc.message() + "]\n";
            ++unreadable;
        }
    };

    walk(dirpath, "", 0);
    if (unreadable > 0) {
        return Result::ok("Listed with " + std::to_string(unreadable)
                          + " unreadable director"
                          + (unreadable == 1 ? "y" : "ies")
                          + " (see [unreadable] markers)");
    }
    return Result::ok();
}

//  PRIVATE METHODS 

Result FileManager::validateParentExists(const fs::path& p) {
    auto parent = p.parent_path();
    if (parent.empty()) return Result::ok();
    return requireExists(parent, "Parent directory does not exist: ");

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

    // Safety net: an exception escaping here would abort the program and
    // skip the audit-log entry, so turn it into an ordinary failure.
    Result res;
    try {
        res = action();
    } catch (const std::exception& e) {
        res = Result::fail(std::string("Unexpected error: ") + e.what());
    }
    m_logger.log(operation, target,
                 (res.success ? "OK: " : "FAIL: ") + res.message);
    return res;
}

