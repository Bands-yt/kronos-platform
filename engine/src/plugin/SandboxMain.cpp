// kronos_plugin_sandbox: runs one third-party plugin in its own process.
// Before the plugin is loaded the process gives up everything a plugin
// shouldn't need: Landlock limits the filesystem to reading the plugin's
// own folder and system libraries, seccomp blocks networking, starting
// programs, debugging or signalling other processes, and resource limits
// cap memory. The engine reaches the plugin only through the socket on fd 3.

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "plugin/PluginIpc.hpp"
#include "plugin/kronos_plugin.h"

namespace ipc = engine::plugin::ipc;

namespace {

constexpr int kSocket = 3;

// --- Landlock ---------------------------------------------------------

#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#define SYS_landlock_add_rule 445
#define SYS_landlock_restrict_self 446
#endif

constexpr uint64_t kFsExecute = 1ull << 0;
constexpr uint64_t kFsReadFile = 1ull << 2;
constexpr uint64_t kFsReadDir = 1ull << 3;
constexpr uint64_t kFsAbi1 = (1ull << 13) - 1;
constexpr uint64_t kFsRefer = 1ull << 13;
constexpr uint64_t kFsTruncate = 1ull << 14;
constexpr uint64_t kFsIoctlDev = 1ull << 15;
constexpr uint64_t kNetBindTcp = 1ull << 0;
constexpr uint64_t kNetConnectTcp = 1ull << 1;
constexpr uint64_t kScopeAbstractUnix = 1ull << 0;
constexpr uint64_t kScopeSignal = 1ull << 1;

struct RulesetAttr {
    uint64_t handledAccessFs;
    uint64_t handledAccessNet;
    uint64_t scoped;
};

struct PathBeneathAttr {
    uint64_t allowedAccess;
    int32_t parentFd;
} __attribute__((packed));

bool restrictFilesystem(const std::string& pluginDir, std::string& error) {
    const long abi = syscall(SYS_landlock_create_ruleset, nullptr, 0, 1u);
    if (abi < 1) {
        error = "this system doesn't support Landlock, so plugins can't be sandboxed";
        return false;
    }
    RulesetAttr attr{kFsAbi1, 0, 0};
    size_t attrSize = sizeof(uint64_t);
    if (abi >= 2) attr.handledAccessFs |= kFsRefer;
    if (abi >= 3) attr.handledAccessFs |= kFsTruncate;
    if (abi >= 4) {
        attr.handledAccessNet = kNetBindTcp | kNetConnectTcp;
        attrSize = 2 * sizeof(uint64_t);
    }
    if (abi >= 5) attr.handledAccessFs |= kFsIoctlDev;
    if (abi >= 6) {
        attr.scoped = kScopeAbstractUnix | kScopeSignal;
        attrSize = sizeof(RulesetAttr);
    }
    const int ruleset = static_cast<int>(syscall(SYS_landlock_create_ruleset, &attr, attrSize, 0u));
    if (ruleset < 0) {
        error = std::string("could not create the Landlock ruleset: ") + std::strerror(errno);
        return false;
    }
    auto allow = [&](const std::string& path, bool directory) {
        const int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
        if (fd < 0) return;
        PathBeneathAttr rule{directory ? (kFsExecute | kFsReadFile | kFsReadDir) : (kFsExecute | kFsReadFile), fd};
        syscall(SYS_landlock_add_rule, ruleset, 1, &rule, 0u);
        close(fd);
    };
    allow(pluginDir, true);
    for (const char* dir : {"/usr/lib", "/usr/lib64", "/usr/lib32", "/usr/local/lib", "/lib", "/lib64", "/lib32"}) {
        allow(dir, true);
    }
    for (const char* file : {"/etc/ld.so.cache", "/etc/ld.so.preload", "/etc/localtime", "/dev/null", "/dev/urandom"}) {
        allow(file, false);
    }
    const bool ok = syscall(SYS_landlock_restrict_self, ruleset, 0u) == 0;
    if (!ok) error = std::string("could not apply the Landlock ruleset: ") + std::strerror(errno);
    close(ruleset);
    return ok;
}

// --- seccomp ----------------------------------------------------------

#if defined(__x86_64__)
constexpr uint32_t kAuditArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr uint32_t kAuditArch = AUDIT_ARCH_AARCH64;
#else
#error "kronos_plugin_sandbox supports x86_64 and aarch64"
#endif

constexpr uint32_t kDeny = SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA);

const std::vector<long>& deniedSyscalls() {
    static const std::vector<long> list = {
        SYS_socket, SYS_socketpair, SYS_connect, SYS_bind, SYS_listen, SYS_accept, SYS_accept4,
        SYS_execve, SYS_execveat, SYS_ptrace, SYS_process_vm_readv, SYS_process_vm_writev,
        SYS_mount, SYS_umount2, SYS_pivot_root, SYS_chroot, SYS_setns, SYS_unshare,
        SYS_keyctl, SYS_add_key, SYS_request_key, SYS_bpf, SYS_perf_event_open, SYS_userfaultfd,
        SYS_init_module, SYS_finit_module, SYS_delete_module, SYS_reboot, SYS_swapon, SYS_swapoff,
        SYS_kexec_load, SYS_open_by_handle_at, SYS_name_to_handle_at,
        SYS_io_uring_setup, SYS_io_uring_enter, SYS_io_uring_register,
        SYS_pidfd_open, SYS_pidfd_getfd, SYS_pidfd_send_signal, SYS_process_madvise,
        SYS_tkill, SYS_rt_sigqueueinfo, SYS_rt_tgsigqueueinfo, SYS_kcmp, SYS_fanotify_init,
        SYS_move_pages, SYS_migrate_pages, SYS_syslog, SYS_acct, SYS_quotactl, SYS_vhangup,
        SYS_sethostname, SYS_setdomainname, SYS_settimeofday, SYS_clock_settime, SYS_personality,
#ifdef SYS_fork
        SYS_fork,
#endif
#ifdef SYS_vfork
        SYS_vfork,
#endif
#ifdef SYS_kexec_file_load
        SYS_kexec_file_load,
#endif
#ifdef SYS_iopl
        SYS_iopl, SYS_ioperm, SYS_modify_ldt,
#endif
    };
    return list;
}

bool applySeccomp(std::string& error) {
    const auto self = static_cast<uint32_t>(getpid());
    std::vector<sock_filter> f;
    auto stmt = [&](uint16_t code, uint32_t k) { f.push_back(BPF_STMT(code, k)); };
    auto jump = [&](uint16_t code, uint32_t k, uint8_t jt, uint8_t jf) { f.push_back(BPF_JUMP(code, k, jt, jf)); };
    constexpr uint32_t kArg0 = offsetof(seccomp_data, args[0]);
    constexpr uint32_t kArg1 = offsetof(seccomp_data, args[1]);

    stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
    jump(BPF_JMP | BPF_JEQ | BPF_K, kAuditArch, 1, 0);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));
#if defined(__x86_64__)
    jump(BPF_JMP | BPF_JGE | BPF_K, 0x40000000u, 0, 1); // x32 calls
    stmt(BPF_RET | BPF_K, kDeny);
#endif
    for (long nr : deniedSyscalls()) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(nr), 0, 1);
        stmt(BPF_RET | BPF_K, kDeny);
    }
    // clone3 can't be filtered by flags; glibc falls back to clone.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone3, 0, 1);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS);
    // Threads are fine, new processes aren't.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone, 0, 4);
    stmt(BPF_LD | BPF_W | BPF_ABS, kArg0);
    jump(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 0, 1);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    stmt(BPF_RET | BPF_K, kDeny);
    // Signals only to itself.
    for (long nr : {static_cast<long>(SYS_kill), static_cast<long>(SYS_tgkill)}) {
        jump(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(nr), 0, 4);
        stmt(BPF_LD | BPF_W | BPF_ABS, kArg0);
        jump(BPF_JMP | BPF_JEQ | BPF_K, self, 0, 1);
        stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
        stmt(BPF_RET | BPF_K, kDeny);
    }
    // No typing into the terminal the editor was started from.
    jump(BPF_JMP | BPF_JEQ | BPF_K, SYS_ioctl, 0, 4);
    stmt(BPF_LD | BPF_W | BPF_ABS, kArg1);
    jump(BPF_JMP | BPF_JEQ | BPF_K, TIOCSTI, 1, 0);
    jump(BPF_JMP | BPF_JEQ | BPF_K, TIOCLINUX, 0, 1);
    stmt(BPF_RET | BPF_K, kDeny);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

    sock_fprog program{static_cast<unsigned short>(f.size()), f.data()};
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
        error = std::string("could not apply the seccomp filter: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool lockDown(const std::string& pluginDir, std::string& error) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    prctl(PR_SET_DUMPABLE, 0);
    const int devNull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        close(devNull);
    }
    syscall(SYS_close_range, kSocket + 1, ~0u, 0u);
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
    const rlimit memory{4ull << 30, 4ull << 30};
    setrlimit(RLIMIT_AS, &memory);
    const rlimit files{64, 64};
    setrlimit(RLIMIT_NOFILE, &files);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        error = "could not set no_new_privs";
        return false;
    }
    return restrictFilesystem(pluginDir, error) && applySeccomp(error);
}

// --- Talking to the engine ----------------------------------------------

[[noreturn]] void lost() { _exit(3); }

void send(ipc::Msg type, const ipc::Writer& payload) {
    if (!ipc::sendFrame(kSocket, type, payload.bytes)) lost();
}

std::vector<uint8_t> hostCall(ipc::Msg type, const ipc::Writer& payload, int* fd = nullptr) {
    send(type, payload);
    ipc::Msg reply{};
    std::vector<uint8_t> bytes;
    if (ipc::recvFrame(kSocket, reply, bytes, -1, fd) != ipc::Recv::Ok || reply != ipc::Msg::HostReply) lost();
    return bytes;
}

int32_t resultOf(const std::vector<uint8_t>& bytes) {
    ipc::Reader reader(bytes);
    const auto result = reader.get<int32_t>();
    return reader.ok ? result : KRONOS_ERROR;
}

struct Importer {
    KronosAssetImporter desc{};
};
struct Panel {
    KronosPanel desc{};
};
struct MappedChannel {
    std::string name;
    KronosChannel channel{};
};

std::vector<Importer> importers;
std::vector<Panel> panels;
std::vector<MappedChannel> channels;

template <typename T>
bool copyStruct(const T* in, T& out, size_t minimum) {
    if (!in || in->struct_size < minimum) return false;
    out = T{};
    std::memcpy(&out, in, std::min<size_t>(in->struct_size, sizeof(T)));
    out.struct_size = sizeof(T);
    return true;
}

const char* orEmpty(const char* s) { return s ? s : ""; }

void hostLog(KronosHost*, int32_t level, const char* message) {
    ipc::Writer w;
    w.put(level).str(orEmpty(message));
    send(ipc::Msg::Log, w);
}

int32_t hostRegisterImporter(KronosHost*, const KronosAssetImporter* importer) {
    Importer entry;
    if (!copyStruct(importer, entry.desc, offsetof(KronosAssetImporter, import) + sizeof(void*)) || !entry.desc.import) {
        return KRONOS_INVALID;
    }
    ipc::Writer w;
    w.put(static_cast<uint32_t>(importers.size()))
        .str(orEmpty(entry.desc.type))
        .put(entry.desc.version)
        .str(orEmpty(entry.desc.extensions))
        .str(orEmpty(entry.desc.output_extension));
    importers.push_back(entry);
    return resultOf(hostCall(ipc::Msg::RegisterImporter, w));
}

int32_t hostRegisterPanel(KronosHost*, const KronosPanel* panel) {
    Panel entry;
    if (!copyStruct(panel, entry.desc, offsetof(KronosPanel, draw) + sizeof(void*)) || !entry.desc.draw) {
        return KRONOS_INVALID;
    }
    ipc::Writer w;
    w.put(static_cast<uint32_t>(panels.size()))
        .str(orEmpty(entry.desc.id))
        .str(orEmpty(entry.desc.title))
        .put(entry.desc.version);
    panels.push_back(entry);
    return resultOf(hostCall(ipc::Msg::RegisterPanel, w));
}

int32_t hostOpenChannel(KronosHost*, const char* name, uint32_t version, uint64_t size, KronosChannel* out) {
    if (!name || !out) return KRONOS_INVALID;
    for (const MappedChannel& mapped : channels) {
        if (mapped.name == name) {
            if (mapped.channel.version != version || mapped.channel.size != size) return KRONOS_CONFLICT;
            *out = mapped.channel;
            return KRONOS_OK;
        }
    }
    ipc::Writer w;
    w.str(name).put(version).put(size);
    int fd = -1;
    const int32_t result = resultOf(hostCall(ipc::Msg::OpenChannel, w, &fd));
    if (result != KRONOS_OK) {
        if (fd >= 0) close(fd);
        return result;
    }
    if (fd < 0) return KRONOS_ERROR;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (data == MAP_FAILED) return KRONOS_ERROR;
    channels.push_back({name, {data, size, version}});
    *out = channels.back().channel;
    return KRONOS_OK;
}

uint32_t hostEntityCount(KronosHost*) {
    const std::vector<uint8_t> bytes = hostCall(ipc::Msg::EntityCount, {});
    ipc::Reader reader(bytes);
    return reader.get<uint32_t>();
}

KronosEntity hostEntityAt(KronosHost*, uint32_t index) {
    ipc::Writer w;
    w.put(index);
    const std::vector<uint8_t> bytes = hostCall(ipc::Msg::EntityAt, w);
    ipc::Reader reader(bytes);
    return reader.get<uint64_t>();
}

KronosEntity hostFindEntity(KronosHost*, const char* name) {
    ipc::Writer w;
    w.str(orEmpty(name));
    const std::vector<uint8_t> bytes = hostCall(ipc::Msg::FindEntity, w);
    ipc::Reader reader(bytes);
    return reader.get<uint64_t>();
}

uint32_t hostEntityName(KronosHost*, KronosEntity entity, char* buffer, uint32_t capacity) {
    ipc::Writer w;
    w.put(entity);
    const std::vector<uint8_t> bytes = hostCall(ipc::Msg::EntityName, w);
    ipc::Reader reader(bytes);
    const std::string name = reader.str();
    if (buffer && capacity > 0) {
        const size_t n = std::min<size_t>(name.size(), capacity - 1);
        std::memcpy(buffer, name.data(), n);
        buffer[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}

int32_t hostGetPosition(KronosHost*, KronosEntity entity, float out[3]) {
    ipc::Writer w;
    w.put(entity);
    const std::vector<uint8_t> bytes = hostCall(ipc::Msg::GetPosition, w);
    ipc::Reader reader(bytes);
    const auto result = reader.get<int32_t>();
    float position[3] = {reader.get<float>(), reader.get<float>(), reader.get<float>()};
    if (!reader.ok) return KRONOS_ERROR;
    if (result == KRONOS_OK && out) std::memcpy(out, position, sizeof(position));
    return result;
}

int32_t hostSetPosition(KronosHost*, KronosEntity entity, const float position[3]) {
    if (!position) return KRONOS_INVALID;
    ipc::Writer w;
    w.put(entity).put(position[0]).put(position[1]).put(position[2]);
    return resultOf(hostCall(ipc::Msg::SetPosition, w));
}

// --- Panels -----------------------------------------------------------

struct UiEvent {
    uint32_t widget = 0;
    uint32_t hash = 0;
    ipc::UiKind kind{};
    int32_t intValue = 0;
    float floatValue = 0.0f;
    std::string text;
};

struct ChildUi {
    std::vector<UiEvent> events;
    uint32_t widget = 0;
    uint32_t count = 0;
    ipc::Writer commands;

    const UiEvent* take(ipc::UiKind kind, const char* label) {
        const uint32_t index = widget++;
        const uint32_t hash = ipc::labelHash(label);
        for (const UiEvent& event : events) {
            if (event.widget == index && event.hash == hash && event.kind == kind) return &event;
        }
        return nullptr;
    }
    bool room() { return count < ipc::kMaxUiCommands; }
};

ChildUi& ui(void* context) { return *static_cast<ChildUi*>(context); }

void uiText(void* c, const char* text) {
    if (!ui(c).room()) return;
    ui(c).commands.put(ipc::UiKind::Text).str(orEmpty(text));
    ++ui(c).count;
}

int32_t uiButton(void* c, const char* label) {
    const UiEvent* event = ui(c).take(ipc::UiKind::Button, label);
    if (ui(c).room()) {
        ui(c).commands.put(ipc::UiKind::Button).str(orEmpty(label));
        ++ui(c).count;
    }
    return event ? 1 : 0;
}

int32_t uiCheckbox(void* c, const char* label, int32_t* value) {
    const UiEvent* event = ui(c).take(ipc::UiKind::Checkbox, label);
    if (event && value) *value = event->intValue;
    if (ui(c).room()) {
        ui(c).commands.put(ipc::UiKind::Checkbox).str(orEmpty(label)).put(value ? *value : 0);
        ++ui(c).count;
    }
    return event ? 1 : 0;
}

int32_t uiSlider(void* c, const char* label, float* value, float min, float max) {
    const UiEvent* event = ui(c).take(ipc::UiKind::Slider, label);
    if (event && value) *value = event->floatValue;
    if (ui(c).room()) {
        ui(c).commands.put(ipc::UiKind::Slider).str(orEmpty(label)).put(value ? *value : 0.0f).put(min).put(max);
        ++ui(c).count;
    }
    return event ? 1 : 0;
}

int32_t uiInputText(void* c, const char* label, char* buffer, uint32_t capacity) {
    const UiEvent* event = ui(c).take(ipc::UiKind::InputText, label);
    if (event && buffer && capacity > 0) {
        const size_t n = std::min<size_t>(event->text.size(), capacity - 1);
        std::memcpy(buffer, event->text.data(), n);
        buffer[n] = '\0';
    }
    if (ui(c).room()) {
        ui(c).commands.put(ipc::UiKind::InputText).str(orEmpty(label)).put(capacity).str(buffer && capacity ? buffer : "");
        ++ui(c).count;
    }
    return event ? 1 : 0;
}

void uiSeparator(void* c) {
    if (!ui(c).room()) return;
    ui(c).commands.put(ipc::UiKind::Separator);
    ++ui(c).count;
}

void uiSameLine(void* c) {
    if (!ui(c).room()) return;
    ui(c).commands.put(ipc::UiKind::SameLine);
    ++ui(c).count;
}

std::vector<uint8_t> drawPanel(ipc::Reader& reader) {
    const auto index = reader.get<uint32_t>();
    ChildUi state;
    const auto eventCount = reader.get<uint32_t>();
    for (uint32_t i = 0; reader.ok && i < eventCount && i < ipc::kMaxUiCommands; ++i) {
        UiEvent event;
        event.widget = reader.get<uint32_t>();
        event.hash = reader.get<uint32_t>();
        event.kind = reader.get<ipc::UiKind>();
        if (event.kind == ipc::UiKind::Checkbox) event.intValue = reader.get<int32_t>();
        if (event.kind == ipc::UiKind::Slider) event.floatValue = reader.get<float>();
        if (event.kind == ipc::UiKind::InputText) event.text = reader.str();
        state.events.push_back(std::move(event));
    }
    if (reader.ok && index < panels.size()) {
        KronosUi table{};
        table.struct_size = sizeof(KronosUi);
        table.context = &state;
        table.text = uiText;
        table.button = uiButton;
        table.checkbox = uiCheckbox;
        table.slider_float = uiSlider;
        table.input_text = uiInputText;
        table.separator = uiSeparator;
        table.same_line = uiSameLine;
        panels[index].desc.draw(panels[index].desc.user, &table);
    }
    ipc::Writer reply;
    reply.put(state.count);
    reply.bytes.insert(reply.bytes.end(), state.commands.bytes.begin(), state.commands.bytes.end());
    return reply.bytes;
}

struct Output {
    std::vector<uint8_t> bytes;
};

int32_t outputWrite(void* context, const void* data, uint64_t size) {
    auto& out = *static_cast<Output*>(context);
    if (size > ipc::kMaxFrameBytes - 64 - out.bytes.size()) return KRONOS_ERROR;
    const auto* raw = static_cast<const uint8_t*>(data);
    out.bytes.insert(out.bytes.end(), raw, raw + size);
    return KRONOS_OK;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "kronos_plugin_sandbox is started by the editor to run one plugin.\n");
        return 2;
    }
    const int socketFd = std::atoi(argv[1]);
    if (socketFd != kSocket) {
        if (dup2(socketFd, kSocket) < 0) return 2;
        close(socketFd);
    }
    const std::string pluginPath = argv[2];
    std::error_code ec;
    const std::string pluginDir = std::filesystem::absolute(pluginPath, ec).parent_path().string();

    std::string error;
    const bool lockedDown = lockDown(pluginDir, error);

    void* library = nullptr;
    KronosPluginLoadFn loadFn = nullptr;
    KronosPluginTickFn tickFn = nullptr;
    KronosPluginUnloadFn unloadFn = nullptr;
    void* instance = nullptr;
    KronosHostApi api{};

    for (;;) {
        ipc::Msg type{};
        std::vector<uint8_t> payload;
        if (ipc::recvFrame(kSocket, type, payload, -1) != ipc::Recv::Ok) lost();
        ipc::Reader reader(payload);
        ipc::Writer reply;
        switch (type) {
        case ipc::Msg::Load: {
            KronosPluginInfo info{};
            info.struct_size = sizeof(KronosPluginInfo);
            int32_t result = KRONOS_ERROR;
            if (lockedDown) {
                library = dlopen(pluginPath.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!library) {
                    const char* message = dlerror();
                    error = message ? message : "could not load the library";
                }
            }
            if (library) {
                auto queryFn = reinterpret_cast<KronosPluginQueryFn>(dlsym(library, KRONOS_PLUGIN_QUERY_SYMBOL));
                loadFn = reinterpret_cast<KronosPluginLoadFn>(dlsym(library, KRONOS_PLUGIN_LOAD_SYMBOL));
                tickFn = reinterpret_cast<KronosPluginTickFn>(dlsym(library, KRONOS_PLUGIN_TICK_SYMBOL));
                unloadFn = reinterpret_cast<KronosPluginUnloadFn>(dlsym(library, KRONOS_PLUGIN_UNLOAD_SYMBOL));
                if (!queryFn || !loadFn || !unloadFn) {
                    error = "the library doesn't export the Kronos plugin functions";
                } else {
                    result = queryFn(&info);
                    if (result != KRONOS_OK) error = "the plugin's query function failed";
                }
            }
            reply.put(result).str(error).put(info.api_major).put(info.api_minor).str(orEmpty(info.id))
                .str(orEmpty(info.name)).str(orEmpty(info.version)).str(orEmpty(info.author)).put(info.capabilities);
            break;
        }
        case ipc::Msg::Start: {
            api.struct_size = sizeof(KronosHostApi);
            api.api_major = KRONOS_PLUGIN_API_MAJOR;
            api.api_minor = KRONOS_PLUGIN_API_MINOR;
            api.host = nullptr;
            api.granted_capabilities = reader.get<uint64_t>();
            api.sandboxed = 1;
            api.log = hostLog;
            api.register_asset_importer = hostRegisterImporter;
            api.register_panel = hostRegisterPanel;
            api.open_channel = hostOpenChannel;
            api.entity_count = hostEntityCount;
            api.entity_at = hostEntityAt;
            api.find_entity = hostFindEntity;
            api.entity_name = hostEntityName;
            api.get_position = hostGetPosition;
            api.set_position = hostSetPosition;
            reply.put(loadFn ? loadFn(&api, &instance) : KRONOS_ERROR);
            break;
        }
        case ipc::Msg::Tick: {
            const auto dt = reader.get<float>();
            if (tickFn && reader.ok) tickFn(instance, dt);
            break;
        }
        case ipc::Msg::Draw:
            reply.bytes = drawPanel(reader);
            break;
        case ipc::Msg::Import: {
            const auto index = reader.get<uint32_t>();
            const std::string name = reader.str();
            std::vector<uint8_t> data;
            reader.blob(data);
            Output output;
            int32_t result = KRONOS_INVALID;
            if (reader.ok && index < importers.size()) {
                KronosAssetOutput out{sizeof(KronosAssetOutput), &output, outputWrite};
                const KronosAssetImporter& importer = importers[index].desc;
                result = importer.import(importer.user, name.c_str(), data.data(), data.size(), &out);
            }
            reply.put(result).blob(output.bytes.data(), output.bytes.size());
            break;
        }
        case ipc::Msg::Unload:
            if (unloadFn) unloadFn(instance);
            send(ipc::Msg::Reply, reply);
            _exit(0);
        default:
            lost();
        }
        send(ipc::Msg::Reply, reply);
    }
}
