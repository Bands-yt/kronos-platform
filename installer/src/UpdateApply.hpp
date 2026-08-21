#pragma once

#include <cstdint>
#include <string>

namespace kronos_installer {

// Kronos ("In-App Auto-Updater" -- "Safe Swapping"): the real, separate-
// process half of the updater.
//
// Why a separate process at all: a running app cannot reliably replace
// its own executable. On Windows the loaded image is locked outright
// (an open file handle with no delete-share), and on Linux, while the
// inode trick makes overwriting *possible*, doing it underneath a live
// process still leaves that process running code from a half-swapped
// install. Handing the swap to a process that is NOT inside the
// directory being replaced removes the whole class of problem by
// construction -- the same design real updaters (Chrome, Sparkle) use.
//
// The launcher spawns this helper and then exits immediately;
// waitForProcessExit() below is what makes that safe.

// Blocks until the real process `pid` is genuinely gone, or until
// `timeoutSeconds` elapses. Returns true only if the process really
// exited. This is deliberately NOT waitpid()-based: the target is the
// helper's own *parent*, not its child, so the POSIX path polls with
// kill(pid, 0) and the Windows path opens a real handle and waits on it.
[[nodiscard]] bool waitForProcessExit(int64_t pid, double timeoutSeconds);

struct SwapResult {
    bool success = false;
    std::string error;
    // True when a real rollback actually ran because the swap failed
    // partway -- the caller should report the install as untouched
    // rather than broken.
    bool rolledBack = false;
};

// Replaces everything in `installDir` with the real, already-extracted,
// already-verified contents of `stagedDir`, with a real rollback path.
//
// The sequence is deliberately move-based rather than copy-based, so the
// window in which the install directory is incomplete is as short as a
// real directory rename:
//   1. move installDir aside to a real backup path
//   2. move stagedDir into installDir's place
//   3. on any real failure at step 2, move the backup back (rollback)
//   4. delete the backup only after a real success
//
// `backupDirOut` receives the real backup path when one is left behind
// (only on a real success, where deleting it is the caller's cleanup);
// it is empty after a rollback, since the backup is by then back in
// place as the real install.
[[nodiscard]] SwapResult swapInstallDirectory(const std::string& installDir, const std::string& stagedDir,
                                               std::string& backupDirOut);

// Starts `executablePath` as a real, detached process and returns
// immediately without waiting for it -- the final step of an update,
// where this helper relaunches the freshly-swapped app and then exits.
// Deliberately not system()/popen(): no shell is involved on either
// platform, so nothing in the path can be interpreted as a command.
[[nodiscard]] bool launchDetached(const std::string& executablePath, std::string& outError);

} // namespace kronos_installer
