// Header-only helper for a VERY BASIC filename wildcard, used by the
// cpy / mov / del / trash commands so a single invocation can act on
// many files at once (e.g. `filemgr del 'build/*.txt'`).
//
// The only metacharacter is the asterisk (*). It is a catch-all for any
// run of characters or digits WITHIN the filename or WITHIN the
// extension. Crucially, the period ('.') that separates a name from its
// extension is a strict delimiter: '*' will never expand across a '.',
// mirroring how the two are treated as distinct components on GNU/Linux.
//
//   *.txt      matches  notes.txt, a.txt        (any stem, .txt ext)
//   file.*     matches  file.md, file.cpp        (stem "file", any ext)
//   *.*        matches  photo.jpg                (a stem AND an ext)
//   report*    matches  report, report2          (extension-less names)
//   *.txt      does NOT match  notes.bak.txt      ('*' can't cross the '.')
//
// Because '*' cannot cross a period, a bare `*` matches only names that
// have no extension at all; use `*.*` to catch names that do.

#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <system_error>

namespace glob {

// True if the pattern contains our one wildcard metacharacter.
inline bool hasWildcard(const std::string& s) {
    return s.find('*') != std::string::npos;
}

/*  Match a single filename against a wildcard pattern.

    This is the classic iterative glob matcher (linear time, with
    backtracking to the most recent '*'), with one added rule: a '*'
    is forbidden from consuming a '.'. When the star would have to
    absorb a period to keep going, we stop backtracking and report no
    match — that is what makes the period a hard component boundary.

    Matching is case-sensitive, consistent with GNU/Linux filenames. */
inline bool match(const std::string& pattern, const std::string& text) {
    const std::size_t n = text.size();
    const std::size_t m = pattern.size();

    std::size_t p = 0, t = 0;      // cursors into pattern and text
    std::size_t starP = std::string::npos;  // pattern index of last '*'
    std::size_t starT = 0;         // text index the last '*' started at

    while (t < n) {
        if (p < m && pattern[p] == '*') {
            // Record this star and let it match zero characters for now.
            starP = p;
            starT = t;
            ++p;
        } else if (p < m && pattern[p] == text[t]) {
            // Literal character match.
            ++p;
            ++t;
        } else if (starP != std::string::npos && text[starT] != '.') {
            // Mismatch, but a previous '*' can absorb one more character
            // — provided that character is not the period delimiter.
            ++starT;
            t = starT;
            p = starP + 1;
        } else {
            return false;
        }
    }

    // Any trailing pattern characters must all be stars to match "".
    while (p < m && pattern[p] == '*') {
        ++p;
    }
    return p == m;
}

/*  Expand a path whose final component contains a wildcard into the
    concrete paths that match, without recursing into subdirectories
    (the wildcard applies to one directory level, like a shell glob).

    The parent directory is read as-is and each entry's filename is
    tested with match(). Results keep the caller's original parent
    spelling, so a relative pattern yields relative matches. Matches are
    sorted by filename for deterministic, reproducible output.

    Wildcards are only supported in the final component; a '*' anywhere
    in the parent path is rejected via `ec`. On any filesystem error
    `ec` is set and an empty vector is returned. */
inline std::vector<std::filesystem::path>
expand(const std::filesystem::path& pattern, std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();

    const fs::path parent  = pattern.parent_path();
    const std::string name = pattern.filename().string();

    if (hasWildcard(parent.string())) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }

    const fs::path dir = parent.empty() ? fs::path(".") : parent;

    std::vector<fs::path> matches;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string fname = entry.path().filename().string();
        if (match(name, fname)) {
            matches.push_back(parent.empty() ? fs::path(fname)
                                             : parent / fname);
        }
    }
    if (ec) return {};

    std::sort(matches.begin(), matches.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename() < b.filename();
              });
    return matches;
}

}  // namespace glob
