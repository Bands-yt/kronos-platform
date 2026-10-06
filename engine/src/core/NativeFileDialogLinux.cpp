#include "core/NativeFileDialog.hpp"

#if defined(__linux__)

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <utility>

namespace engine::core {

namespace {

struct PendingDialog {
    pid_t pid = -1;
    int fd = -1;
    std::string output;
    std::function<void(const std::string&)> onPicked;
};

std::vector<PendingDialog>& pendingDialogs() {
    static std::vector<PendingDialog> dialogs;
    return dialogs;
}

std::string& lastError() {
    static std::string error;
    return error;
}

// A trailing space or empty pattern makes the GTK portal file chooser hang
// without ever mapping its window, so patterns are joined strictly.
std::string joinPatterns(const std::vector<std::string>& extensions) {
    std::string joined;
    for (const std::string& ext : extensions) {
        if (ext.empty()) continue;
        if (!joined.empty()) joined += ' ';
        joined += ext;
    }
    return joined;
}

void execArgs(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
}

// Runs in the forked child; execvp only returns when a tool is missing, so
// this falls through to the next one.
void execFirstAvailableDialog(const FileDialogOptions& options) {
    const std::string patterns = joinPatterns(options.extensions);
    const std::string name = options.filterName.empty() ? "Supported files" : options.filterName;
    const char* home = std::getenv("HOME");
    const std::string start = home != nullptr ? std::string(home) + "/" : std::string("./");

    std::vector<std::string> zenity = {"zenity", "--file-selection", "--title=" + options.title};
    if (options.save) zenity.insert(zenity.end(), {"--save", "--confirm-overwrite", "--filename=" + start + options.defaultName});
    if (options.directory) {
        zenity.push_back("--directory");
    } else if (!patterns.empty()) {
        zenity.push_back("--file-filter=" + name + " | " + patterns);
        zenity.push_back("--file-filter=All files | *");
    }
    execArgs(zenity);

    std::vector<std::string> kdialog = {"kdialog", "--title", options.title};
    if (options.directory) {
        kdialog.insert(kdialog.end(), {"--getexistingdirectory", start});
    } else {
        kdialog.insert(kdialog.end(), {options.save ? "--getsavefilename" : "--getopenfilename", start + options.defaultName});
        if (!patterns.empty()) kdialog.push_back(name + " (" + patterns + ")\nAll files (*)");
    }
    execArgs(kdialog);

    std::vector<std::string> yad = {"yad", "--file", "--title=" + options.title, "--width=900", "--height=600"};
    if (options.save) yad.insert(yad.end(), {"--save", "--confirm-overwrite", "--filename=" + start + options.defaultName});
    if (options.directory) {
        yad.push_back("--directory");
    } else if (!patterns.empty()) {
        yad.push_back("--file-filter=" + name + " | " + patterns);
        yad.push_back("--file-filter=All files | *");
    }
    execArgs(yad);

    zenity[0] = "qarma";
    execArgs(zenity);
}

bool spawnDialog(const FileDialogOptions& options, PendingDialog& out) {
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        lastError() = "Could not open a pipe for the file dialog.";
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        lastError() = "Could not start the file dialog process.";
        return false;
    }
    if (pid == 0) {
        close(pipeFds[0]);
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[1]);
        execFirstAvailableDialog(options);
        _exit(127);
    }
    close(pipeFds[1]);
    out.pid = pid;
    out.fd = pipeFds[0];
    lastError().clear();
    return true;
}

void readAvailable(PendingDialog& dialog) {
    char buffer[1024];
    while (true) {
        const ssize_t n = read(dialog.fd, buffer, sizeof(buffer));
        if (n > 0) {
            dialog.output.append(buffer, static_cast<size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

std::optional<std::string> finish(PendingDialog& dialog, int status) {
    if (dialog.fd >= 0) close(dialog.fd);
    dialog.fd = -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        lastError() = "No file dialog tool found. Install zenity, kdialog or yad.";
        return std::nullopt;
    }
    std::string result = std::move(dialog.output);
    const size_t newline = result.find('\n');
    if (newline != std::string::npos) result.resize(newline);
    while (!result.empty() && (result.back() == '\r' || result.back() == ' ')) result.pop_back();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || result.empty()) return std::nullopt;
    return result;
}

} // namespace

std::optional<std::string> openFileDialog(const FileDialogOptions& options) {
    PendingDialog dialog;
    if (!spawnDialog(options, dialog)) return std::nullopt;
    readAvailable(dialog);
    int status = 0;
    while (waitpid(dialog.pid, &status, 0) < 0 && errno == EINTR) {
    }
    return finish(dialog, status);
}

std::optional<std::string> openFileDialog(const std::string& title, const std::vector<std::string>& extensions) {
    FileDialogOptions options;
    options.title = title;
    options.extensions = extensions;
    return openFileDialog(options);
}

bool openFileDialogAsync(const FileDialogOptions& options, std::function<void(const std::string&)> onPicked) {
    PendingDialog dialog;
    if (!spawnDialog(options, dialog)) return false;
    fcntl(dialog.fd, F_SETFL, fcntl(dialog.fd, F_GETFL) | O_NONBLOCK);
    dialog.onPicked = std::move(onPicked);
    pendingDialogs().push_back(std::move(dialog));
    return true;
}

void pollFileDialogs() {
    std::vector<PendingDialog>& dialogs = pendingDialogs();
    std::vector<std::pair<std::function<void(const std::string&)>, std::string>> picked;
    for (size_t i = 0; i < dialogs.size();) {
        readAvailable(dialogs[i]);
        int status = 0;
        const pid_t done = waitpid(dialogs[i].pid, &status, WNOHANG);
        if (done == 0) {
            ++i;
            continue;
        }
        if (done == dialogs[i].pid) {
            readAvailable(dialogs[i]);
            if (auto path = finish(dialogs[i], status); path && dialogs[i].onPicked) {
                picked.emplace_back(std::move(dialogs[i].onPicked), std::move(*path));
            }
        } else if (dialogs[i].fd >= 0) {
            close(dialogs[i].fd);
        }
        dialogs.erase(dialogs.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (auto& [callback, path] : picked) callback(path);
}

bool fileDialogOpen() { return !pendingDialogs().empty(); }

const std::string& fileDialogError() { return lastError(); }

} // namespace engine::core

#endif
