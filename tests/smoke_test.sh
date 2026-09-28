#!/usr/bin/env bash
# Cross-platform smoke tests for filemgr.
#
# Runs the real binary against a scratch directory and checks what
# actually happened on disk, so the same script can be pointed at a
# Linux, macOS, or Windows build (Git Bash on Windows, or Wine).
#
#   tests/smoke_test.sh ./build/filemgr
#   tests/smoke_test.sh wine ./filemgr.exe      (FILEMGR_TARGET=windows)
#
# FILEMGR_TARGET (linux|macos|windows) overrides OS detection, which is
# needed under Wine where `uname` still reports Linux.

set -u

if [ $# -eq 0 ]; then
    echo "usage: $0 <filemgr command...>" >&2
    exit 2
fi
FM=("$@")
# The tests cd into a scratch dir, so pin relative paths to the binary.
for i in "${!FM[@]}"; do
    if [ -f "${FM[$i]}" ] && [ "${FM[$i]#/}" = "${FM[$i]}" ]; then
        FM[$i]="$PWD/${FM[$i]}"
    fi
done

case "${FILEMGR_TARGET:-$(uname -s)}" in
    linux|Linux)                 TARGET=linux ;;
    macos|Darwin)                TARGET=macos ;;
    windows|MINGW*|MSYS*|CYGWIN*) TARGET=windows ;;
    *) echo "unknown target" >&2; exit 2 ;;
esac

ROOT="$(mktemp -d 2>/dev/null || mktemp -d -t filemgr)"
WORK="$ROOT/work"
mkdir -p "$WORK"
cd "$WORK" || exit 2

# Keep the audit log and (on Linux) the trash inside the scratch area so
# a local run never touches the real user's files.
if [ "$TARGET" = linux ]; then
    export HOME="$ROOT/home"
    export XDG_DATA_HOME="$HOME/.local/share"
    mkdir -p "$HOME"
fi

PASS=0
FAIL=0
FAILED=()

# Run filemgr with a hard timeout (perl is present on all three CI
# images), so a hung osascript/Finder call cannot stall the suite.
fm() {
    perl -e 'alarm shift @ARGV; exec @ARGV or die "exec: $!"' 60 "${FM[@]}" "$@"
}

check() {
    local name=$1
    shift
    if "$@"; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $name"
        # Surface failures as GitHub annotations (visible without logs).
        if [ -n "${GITHUB_ACTIONS:-}" ]; then
            echo "::error title=$TARGET::$name"
        fi
        FAIL=$((FAIL + 1))
        FAILED+=("$name")
    fi
}

# Each test runs in a fresh subdirectory of WORK.
fresh() {
    cd "$WORK" || exit 2
    rm -rf "$WORK/$1"
    mkdir -p "$WORK/$1"
    cd "$WORK/$1" || exit 2
}

echo "== filemgr smoke tests (target: $TARGET) in $WORK"

# --- basics -----------------------------------------------------------

t_version()   { fm --version | grep -q '^filemgr '; }
t_usage_err() { fm >/dev/null 2>&1; [ $? -eq 2 ]; }
t_unknown()   { fm frobnicate x >/dev/null 2>&1; [ $? -eq 2 ]; }
check "version flag"            t_version
check "no command exits 2"      t_usage_err
check "unknown command exits 2" t_unknown

# --- create / read ----------------------------------------------------

t_inst() {
    fresh inst
    fm inst hello.txt --content "hello" 2>/dev/null &&
        [ "$(cat hello.txt)" = "hello" ]
}
t_inst_exists() {
    fresh inst_exists
    : > a.txt
    ! fm inst a.txt 2>/dev/null
}
t_mkdir_nested() {
    fresh mkdir
    fm mkdir a/b/c 2>/dev/null && [ -d a/b/c ]
}
t_info() {
    fresh info
    printf '12345' > f.txt
    local out
    out=$(fm info f.txt 2>/dev/null) &&
        grep -q 'Size: *5 bytes' <<<"$out" &&
        grep -q 'Extension: *\.txt' <<<"$out"
}
t_info_dir_children() {
    fresh info_dir
    mkdir d && : > d/1 && : > d/2
    fm info d 2>/dev/null | grep -q 'Children: *2'
}
check "inst writes content"          t_inst
check "inst refuses existing file"   t_inst_exists
check "mkdir creates nested dirs"    t_mkdir_nested
check "info reports size/extension"  t_info
check "info counts dir children"     t_info_dir_children

# --- rename -----------------------------------------------------------

t_rename() {
    fresh rename
    : > a.txt
    fm rename a.txt b.md 2>/dev/null && [ -f b.md ] && [ ! -e a.txt ]
}
t_rename_case_only() {
    # Windows (NTFS) and macOS (APFS) are case-insensitive by default,
    # so a naive exists(newPath) check sees the *source* file here.
    fresh rename_case
    : > Report.txt
    fm rename Report.txt report.txt 2>/dev/null &&
        ls | grep -qx 'report.txt'
}
check "rename file/extension"        t_rename
check "rename case-only (Foo->foo)"  t_rename_case_only

# --- copy / move ------------------------------------------------------

t_cpy_into_dir() {
    fresh cpy
    mkdir dst && printf x > a.txt
    fm cpy a.txt dst 2>/dev/null && [ -f dst/a.txt ] && [ -f a.txt ]
}
t_cpy_recursive() {
    fresh cpy_r
    mkdir -p src/sub && printf x > src/sub/f
    fm cpy src out 2>/dev/null && [ -f out/sub/f ]
}
t_cpy_self() {
    fresh cpy_self
    mkdir -p src/sub
    ! fm cpy src src/sub 2>/dev/null && [ ! -e src/sub/src ]
}
t_mov() {
    fresh mov
    mkdir dst && printf x > a.txt
    fm mov a.txt dst 2>/dev/null && [ -f dst/a.txt ] && [ ! -e a.txt ]
}
t_mov_case_only() {
    fresh mov_case
    mkdir Data
    fm mov Data data 2>/dev/null && ls | grep -qx 'data'
}
check "cpy file into existing dir"   t_cpy_into_dir
check "cpy dir recursively"          t_cpy_recursive
check "cpy refuses dir into itself"  t_cpy_self
check "mov file into dir"            t_mov
check "mov case-only (Data->data)"   t_mov_case_only

# --- wildcards --------------------------------------------------------

t_glob_cpy() {
    fresh glob
    mkdir out && : > a.txt && : > b.txt && : > c.md
    fm cpy '*.txt' out 2>/dev/null &&
        [ -f out/a.txt ] && [ -f out/b.txt ] && [ ! -e out/c.md ]
}
t_glob_subdir() {
    # Forward-slash parent path in a pattern (common in scripts).
    fresh glob_sub
    mkdir -p sub out && : > sub/a.log && : > sub/b.log
    fm -y del 'sub/*.log' 2>/dev/null && [ -z "$(ls sub)" ]
}
check "glob cpy '*.txt'"             t_glob_cpy
check "glob del 'sub/*.log'"         t_glob_subdir

# --- delete & confirmation --------------------------------------------

t_del_yes() {
    fresh del_y
    mkdir -p d/e && : > d/e/f
    fm -y del d 2>/dev/null && [ ! -e d ]
}
t_del_no() {
    fresh del_n
    : > keep.txt
    printf 'n\n' | fm del keep.txt >/dev/null 2>&1
    [ -f keep.txt ]
}
t_del_eof() {
    fresh del_eof
    : > keep.txt
    fm del keep.txt </dev/null >/dev/null 2>&1
    [ -f keep.txt ]
}
t_dry_run() {
    fresh dry
    : > a.txt
    fm -n -y del a.txt 2>/dev/null && fm -n mkdir x 2>/dev/null &&
        [ -f a.txt ] && [ ! -e x ]
}
check "del -y removes dir tree"      t_del_yes
check "del answered 'n' keeps file"  t_del_no
check "del with closed stdin keeps"  t_del_eof
check "dry-run changes nothing"      t_dry_run

# --- trash ------------------------------------------------------------

in_trash() {   # in_trash <basename>  — best-effort, per platform
    case "$TARGET" in
        linux) [ -e "$HOME/.local/share/Trash/files/$1" ] ;;
        macos) ls "$HOME/.Trash" 2>/dev/null | grep -qxF "$1" ||
                   ! ls "$HOME/.Trash" >/dev/null 2>&1 ;;  # unreadable (TCC): skip
        windows)
            # Read the Recycle Bin's own $I records, which hold each item's
            # full original path. Shell display names are no good here:
            # Explorer hides known extensions ("notes", not "notes.txt").
            if command -v powershell.exe >/dev/null 2>&1; then
                powershell.exe -NoProfile -Command '
                    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
                    Get-PSDrive -PSProvider FileSystem | % {
                        Get-ChildItem -Force "$($_.Root)`$Recycle.Bin\$sid" -Filter "`$I*" -ErrorAction SilentlyContinue
                    } | % {
                        $b = [IO.File]::ReadAllBytes($_.FullName)
                        [Text.Encoding]::Unicode.GetString($b, 28, $b.Length - 28).TrimEnd([char]0)
                    }' 2>/dev/null | tr -d '\r' | grep -qF "\\$1"
            else
                true
            fi ;;
    esac
}

t_trash_relative() {
    fresh trash_rel
    local n="rel-$RANDOM.txt"
    printf x > "$n"
    fm -y trash "$n" 2>/dev/null && [ ! -e "$n" ] && in_trash "$n"
}
t_trash_subdir_slash() {
    fresh trash_sub
    local n="sub-$RANDOM.txt"
    mkdir sub && printf x > "sub/$n"
    fm -y trash "sub/$n" 2>/dev/null && [ ! -e "sub/$n" ] && in_trash "$n"
}
t_trash_apostrophe() {
    fresh trash_apos
    local n="Bob's notes $RANDOM.txt"
    printf x > "$n"
    fm -y trash "$n" 2>/dev/null && [ ! -e "$n" ]
}
t_trash_no_injection() {
    # A filename is attacker-controlled data (e.g. an extracted archive).
    # It must never be interpreted by a shell.
    fresh trash_inj
    local n="x'\$(touch INJECTED)'.txt"
    printf x > "$n"
    fm -y trash "$n" >/dev/null 2>&1
    [ ! -e INJECTED ]
}
check "trash relative path"          t_trash_relative
check "trash 'sub/file' (fwd slash)" t_trash_subdir_slash
check "trash name with apostrophe"   t_trash_apostrophe
check "trash name is not shell-run"  t_trash_no_injection

# --- unicode & spaces -------------------------------------------------

t_spaces() {
    fresh spaces
    fm inst "my file.txt" 2>/dev/null && [ -f "my file.txt" ]
}
t_unicode_inst() {
    fresh uni
    fm inst "café-日本.txt" 2>/dev/null && [ -f "café-日本.txt" ]
}
t_unicode_existing() {
    fresh uni2
    printf 'abc' > "naïve-données.txt"
    fm info "naïve-données.txt" 2>/dev/null | grep -q 'Size: *3 bytes'
}
t_unicode_glob() {
    fresh uni3
    mkdir out && : > "Ωmega.txt"
    fm cpy '*.txt' out 2>/dev/null && [ -f "out/Ωmega.txt" ]
}
t_unicode_lookalike() {
    # Regression: on Windows 'Ω' was best-fit mapped to 'O', so this
    # pattern permanently deleted Omega.txt and left Ωmega.txt alone.
    # Refusing to act is acceptable; touching Omega.txt never is.
    fresh uni_lookalike
    echo keep > "Omega.txt"
    echo del  > "Ωmega.txt"
    fm -y del 'Ω*.txt' >/dev/null 2>&1
    [ -f "Omega.txt" ] && [ "$(cat Omega.txt)" = keep ]
}
check "path with spaces"             t_spaces
check "glob never hits look-alike"   t_unicode_lookalike
check "unicode name via argv"        t_unicode_inst
check "unicode existing file info"   t_unicode_existing
check "unicode name via glob"        t_unicode_glob

# --- tree & log -------------------------------------------------------

t_tree() {
    fresh tree
    mkdir -p a/b && : > a/b/f.txt && : > a/z.txt
    local out
    out=$(fm tree a 2>/dev/null) &&
        grep -q 'b/' <<<"$out" && grep -q 'f.txt' <<<"$out" &&
        grep -q 'z.txt' <<<"$out"
}
t_tree_depth() {
    fresh tree_d
    mkdir -p a/b/c
    ! fm tree --depth 1 a 2>/dev/null | grep -q 'c/'
}
t_log() {
    fm log 2>/dev/null | grep -q 'CREATE'
}
t_log_location() {
    case "$TARGET" in
        linux) [ -f "$XDG_DATA_HOME/filemgr/history.log" ] ;;
        macos) [ -f "$HOME/Library/Application Support/filemgr/history.log" ] ;;
        windows)
            if [ -n "${LOCALAPPDATA:-}" ] && command -v cygpath >/dev/null; then
                [ -f "$(cygpath -u "$LOCALAPPDATA")/filemgr/history.log" ]
            else
                true   # under Wine the prefix is opaque; skip
            fi ;;
    esac
}
check "tree lists nested entries"    t_tree
check "tree --depth limits output"   t_tree_depth
check "log records operations"       t_log
check "log in platform data dir"     t_log_location

# --- POSIX permission handling ---------------------------------------

if [ "$TARGET" != windows ] && [ "$(id -u)" != 0 ]; then
    t_perm_no_crash() {
        # stat() on a child of an unreadable dir fails with EACCES; the
        # throwing overload of fs::exists would abort the process.
        fresh perm
        mkdir locked && : > locked/f && chmod 000 locked
        fm -y del locked/f >/dev/null 2>&1
        local rc=$?
        chmod 755 locked
        [ $rc -eq 1 ]
    }
    check "permission denied fails cleanly (exit 1, no abort)" t_perm_no_crash
fi

echo
echo "== $PASS passed, $FAIL failed (target: $TARGET)"
if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "::notice title=$TARGET::$PASS passed, $FAIL failed"
fi
for f in "${FAILED[@]+"${FAILED[@]}"}"; do echo "   - $f"; done

cd / && chmod -R u+rwx "$ROOT" 2>/dev/null; rm -rf "$ROOT"
[ "$FAIL" -eq 0 ]
