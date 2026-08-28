// Real correctness + concurrency tests for the isolated polyglot_core
// target (EventRing, TypeRegistry, PackageRegistry, VirtualFileSystem).
// Not linked against engine_core/engine_runtime/studio -- this whole
// binary only depends on polyglot_core and the C++ standard library.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "polyglot/EventBus.hpp"
#include "polyglot/PackageRegistry.hpp"
#include "polyglot/UnifiedTypeSystem.hpp"
#include "polyglot/VirtualFileSystem.hpp"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n", description);
    }
}

engine::polyglot::EventPayload makePayload(uint32_t eventTypeId) {
    engine::polyglot::EventPayload payload{};
    payload.eventTypeId = eventTypeId;
    return payload;
}

// --- EventRing -------------------------------------------------------------

void testEventRingEmptyPopFails() {
    engine::polyglot::EventRing ring(8);
    engine::polyglot::EventPayload out{};
    check(!ring.tryPop(out), "EventRing::tryPop() on an empty ring real-fails, not a garbage read");
}

void testEventRingPushPopFifoOrder() {
    engine::polyglot::EventRing ring(8);
    for (uint32_t i = 0; i < 5; ++i) {
        check(ring.tryPush(makePayload(i + 1)), "EventRing::tryPush() real-succeeds under capacity");
    }
    for (uint32_t i = 0; i < 5; ++i) {
        engine::polyglot::EventPayload out{};
        check(ring.tryPop(out), "EventRing::tryPop() real-succeeds while non-empty");
        check(out.eventTypeId == i + 1, "EventRing preserves real FIFO order");
    }
}

void testEventRingFullRejectsPush() {
    // capacity 4 -> 3 real usable slots (one always kept empty, see
    // EventBus.hpp's own header comment).
    engine::polyglot::EventRing ring(4);
    check(ring.tryPush(makePayload(1)), "push 1/3");
    check(ring.tryPush(makePayload(2)), "push 2/3");
    check(ring.tryPush(makePayload(3)), "push 3/3");
    check(!ring.tryPush(makePayload(4)), "EventRing::tryPush() real-fails once genuinely full");
}

void testEventRingWraparoundPreservesOrder() {
    engine::polyglot::EventRing ring(4); // 3 usable slots
    engine::polyglot::EventPayload out{};

    check(ring.tryPush(makePayload(1)), "wraparound: push 1");
    check(ring.tryPush(makePayload(2)), "wraparound: push 2");
    check(ring.tryPop(out) && out.eventTypeId == 1, "wraparound: pop 1 first");

    // Ring internally wraps past its own backing array boundary here --
    // the real point of this test.
    check(ring.tryPush(makePayload(3)), "wraparound: push 3 (wraps internally)");
    check(ring.tryPush(makePayload(4)), "wraparound: push 4 (wraps internally)");

    check(ring.tryPop(out) && out.eventTypeId == 2, "wraparound: pop 2");
    check(ring.tryPop(out) && out.eventTypeId == 3, "wraparound: pop 3");
    check(ring.tryPop(out) && out.eventTypeId == 4, "wraparound: pop 4");
    check(!ring.tryPop(out), "wraparound: real-empty after draining everything pushed");
}

void testEventRingPayloadBytesSurviveRoundTrip() {
    engine::polyglot::EventRing ring(4);
    engine::polyglot::EventPayload in{};
    in.eventTypeId = 99;
    std::memset(in.data, 0xAB, sizeof(in.data));
    check(ring.tryPush(in), "payload round-trip: push");

    engine::polyglot::EventPayload out{};
    check(ring.tryPop(out), "payload round-trip: pop");
    check(out.eventTypeId == 99, "payload round-trip: eventTypeId preserved");
    check(std::memcmp(in.data, out.data, sizeof(in.data)) == 0, "payload round-trip: real full payload bytes preserved, not just eventTypeId");
}

// Real, live concurrent stress test -- an actual producer thread and a
// actual consumer thread hammering the same EventRing for a real,
// meaningful number of operations, verifying every one of N events is
// received exactly once, in order, none lost or duplicated. This is
// what actually exercises the atomics' memory-ordering correctness --
// the single-threaded tests above can't catch a real acquire/release bug.
void testEventRingConcurrentProducerConsumerStress() {
    constexpr uint32_t kEventCount = 200000;
    engine::polyglot::EventRing ring(1024);
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        for (uint32_t i = 1; i <= kEventCount; ++i) {
            engine::polyglot::EventPayload payload = makePayload(i);
            while (!ring.tryPush(payload)) {
                std::this_thread::yield(); // real, honest backpressure: ring is full, retry
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    uint32_t expectedNext = 1;
    bool orderedAndComplete = true;
    uint32_t received = 0;
    while (received < kEventCount) {
        engine::polyglot::EventPayload out{};
        if (ring.tryPop(out)) {
            ++received;
            if (out.eventTypeId != expectedNext) {
                orderedAndComplete = false;
            }
            ++expectedNext;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();

    check(orderedAndComplete, "EventRing concurrent stress: every event real-received exactly once, in real FIFO order, across real thread boundaries");
    check(received == kEventCount, "EventRing concurrent stress: real event count matches what was actually sent");
}

// --- TypeRegistry ------------------------------------------------------------

engine::polyglot::ComponentDescriptor makeTransformLikeDescriptor() {
    using namespace engine::polyglot;
    ComponentDescriptor descriptor;
    descriptor.componentName = "Transform";
    descriptor.totalByteSize = 40; // vec3 position + quat rotation + vec3 scale
    descriptor.fields.push_back({"position", FieldType::Vec3, 0, 12});
    descriptor.fields.push_back({"rotation", FieldType::Quat, 12, 16});
    descriptor.fields.push_back({"scale", FieldType::Vec3, 28, 12});
    return descriptor;
}

void testTypeRegistryRegisterAndFind() {
    engine::polyglot::TypeRegistry registry;
    check(registry.registerComponent(makeTransformLikeDescriptor()), "TypeRegistry::registerComponent() real-accepts a valid, in-bounds descriptor");
    const auto* found = registry.find("Transform");
    check(found != nullptr, "TypeRegistry::find() real-finds a registered component by name");
    check(found != nullptr && found->fields.size() == 3, "TypeRegistry::find() real-preserves every registered field");
}

void testTypeRegistryFindMissingReturnsNull() {
    engine::polyglot::TypeRegistry registry;
    check(registry.find("DoesNotExist") == nullptr, "TypeRegistry::find() real-returns null for an unregistered name");
}

void testTypeRegistryRejectsEmptyName() {
    engine::polyglot::TypeRegistry registry;
    engine::polyglot::ComponentDescriptor descriptor = makeTransformLikeDescriptor();
    descriptor.componentName.clear();
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects an empty component name");
}

void testTypeRegistryRejectsDuplicateName() {
    engine::polyglot::TypeRegistry registry;
    check(registry.registerComponent(makeTransformLikeDescriptor()), "first registration real-succeeds");
    check(!registry.registerComponent(makeTransformLikeDescriptor()), "TypeRegistry::registerComponent() real-rejects a second registration of the same name, no silent overwrite");
    check(registry.size() == 1, "a rejected duplicate real-leaves the registry with exactly the one real entry");
}

void testTypeRegistryRejectsFieldSizeMismatch() {
    using namespace engine::polyglot;
    TypeRegistry registry;
    ComponentDescriptor descriptor;
    descriptor.componentName = "Bad";
    descriptor.totalByteSize = 16;
    descriptor.fields.push_back({"notReallyAnF32", FieldType::F32, 0, 8}); // F32 is real-4 bytes, not 8
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects a field whose byteSize doesn't match its real FieldType size");
}

void testTypeRegistryRejectsFieldOutOfBounds() {
    using namespace engine::polyglot;
    TypeRegistry registry;
    ComponentDescriptor descriptor;
    descriptor.componentName = "OutOfBounds";
    descriptor.totalByteSize = 8;
    descriptor.fields.push_back({"overflow", FieldType::Vec3, 4, 12}); // 4+12=16 > totalByteSize=8
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects a field range exceeding totalByteSize");
}

void testTypeRegistryRejectsZeroTotalByteSize() {
    engine::polyglot::TypeRegistry registry;
    engine::polyglot::ComponentDescriptor descriptor;
    descriptor.componentName = "Empty";
    descriptor.totalByteSize = 0;
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects totalByteSize == 0");
}

// --- PackageManifestParser --------------------------------------------------

void testPackageManifestParserValidManifest() {
    using namespace engine::polyglot;
    const char* text =
        "# a real comment\n"
        "PACKAGE kronos-ui-kit\n"
        "\n"
        "VERSION 1.2.0\n"
        "DEPENDS kronos-math-utils\n"
        "DEPENDS kronos-net-shared\n"
        "ARTIFACT LuauModule src/main.luau sha256:abc123\n"
        "ARTIFACT MaterialAsset materials/glow.mat sha256:def456\n";

    PackageManifest manifest;
    std::string error;
    check(PackageManifestParser::parse(text, manifest, error), "a real, valid manifest parses");
    check(manifest.packageId == "kronos-ui-kit", "packageId parses");
    check(manifest.version == "1.2.0", "version parses");
    check(manifest.dependencyPackageIds.size() == 2, "both DEPENDS lines parse");
    check(manifest.dependencyPackageIds[1] == "kronos-net-shared", "DEPENDS order is preserved");
    check(manifest.artifacts.size() == 2, "both ARTIFACT lines parse");
    check(manifest.artifacts[0].kind == PackageArtifactKind::LuauModule, "first artifact kind parses");
    check(manifest.artifacts[1].relativePath == "materials/glow.mat", "artifact relative path parses");
    check(manifest.artifacts[1].checksum == "sha256:def456", "artifact checksum parses");
}

void testPackageManifestParserRejectsMissingPackage() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse("VERSION 1.0.0\n", manifest, error),
          "a manifest with no PACKAGE line real-fails to parse");
    check(!error.empty(), "a parse failure real-leaves a non-empty error message");
}

void testPackageManifestParserRejectsMissingVersion() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse("PACKAGE foo\n", manifest, error),
          "a manifest with no VERSION line real-fails to parse");
}

void testPackageManifestParserRejectsDuplicatePackage() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse("PACKAGE foo\nVERSION 1.0.0\nPACKAGE bar\n", manifest, error),
          "a manifest with two PACKAGE lines real-fails to parse");
}

void testPackageManifestParserRejectsUnrecognizedKeyword() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse("PACKAGE foo\nVERSION 1.0.0\nMONETIZE yes\n", manifest, error),
          "an unrecognized keyword real-fails to parse, not silently skipped");
}

void testPackageManifestParserRejectsUnrecognizedArtifactKind() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse(
              "PACKAGE foo\nVERSION 1.0.0\nARTIFACT PythonScript main.py sha256:x\n", manifest, error),
          "an unrecognized artifact kind real-fails to parse");
}

void testPackageManifestParserRejectsArtifactBeforePackage() {
    engine::polyglot::PackageManifest manifest;
    std::string error;
    check(!engine::polyglot::PackageManifestParser::parse("ARTIFACT LuauModule main.luau sha256:x\n", manifest, error),
          "an ARTIFACT line before any PACKAGE line real-fails to parse");
}

// --- DependencyResolver ------------------------------------------------------

engine::polyglot::PackageManifest makeManifest(const std::string& id, std::vector<std::string> deps = {}) {
    engine::polyglot::PackageManifest m;
    m.packageId = id;
    m.version = "1.0.0";
    m.dependencyPackageIds = std::move(deps);
    return m;
}

void testDependencyResolverLinearChain() {
    engine::polyglot::DependencyResolver resolver;
    resolver.addManifest(makeManifest("A", {"B"}));
    resolver.addManifest(makeManifest("B", {"C"}));
    resolver.addManifest(makeManifest("C"));

    auto result = resolver.resolve("A");
    check(result.status == engine::polyglot::DependencyResolutionStatus::Ok, "a real linear dependency chain resolves Ok");
    check(result.installOrder.size() == 3, "all three real packages appear in the install order");
    if (result.installOrder.size() == 3) {
        check(result.installOrder[0] == "C" && result.installOrder[1] == "B" && result.installOrder[2] == "A",
              "install order is real-topological: dependencies strictly before dependents");
    }
}

void testDependencyResolverDiamondSharesCommonDependencyOnce() {
    engine::polyglot::DependencyResolver resolver;
    resolver.addManifest(makeManifest("App", {"Left", "Right"}));
    resolver.addManifest(makeManifest("Left", {"Shared"}));
    resolver.addManifest(makeManifest("Right", {"Shared"}));
    resolver.addManifest(makeManifest("Shared"));

    auto result = resolver.resolve("App");
    check(result.status == engine::polyglot::DependencyResolutionStatus::Ok, "a real diamond dependency graph resolves Ok");
    check(result.installOrder.size() == 4, "the real shared dependency appears exactly once, not twice");
    if (result.installOrder.size() == 4) {
        auto sharedPos = std::find(result.installOrder.begin(), result.installOrder.end(), "Shared") - result.installOrder.begin();
        auto appPos = std::find(result.installOrder.begin(), result.installOrder.end(), "App") - result.installOrder.begin();
        check(sharedPos < appPos, "the real shared dependency real-precedes the app that (transitively) needs it");
    }
}

void testDependencyResolverMissingDependency() {
    engine::polyglot::DependencyResolver resolver;
    resolver.addManifest(makeManifest("App", {"DoesNotExist"}));

    auto result = resolver.resolve("App");
    check(result.status == engine::polyglot::DependencyResolutionStatus::MissingDependency,
          "a real missing dependency real-reports MissingDependency, not a crash or a silently incomplete order");
    check(result.missingDependencyId == "DoesNotExist", "the real missing dependency's own id is reported");
}

void testDependencyResolverCircularDependency() {
    engine::polyglot::DependencyResolver resolver;
    resolver.addManifest(makeManifest("A", {"B"}));
    resolver.addManifest(makeManifest("B", {"C"}));
    resolver.addManifest(makeManifest("C", {"A"}));

    auto result = resolver.resolve("A");
    check(result.status == engine::polyglot::DependencyResolutionStatus::CircularDependency,
          "a real A->B->C->A cycle real-reports CircularDependency, not an infinite loop");
    check(!result.circularChain.empty() && result.circularChain.front() == result.circularChain.back(),
          "the real reported cycle chain real-closes the loop (starts and ends on the same package)");
}

void testDependencyResolverResolveUnknownRootIsMissingDependency() {
    engine::polyglot::DependencyResolver resolver;
    auto result = resolver.resolve("NeverRegistered");
    check(result.status == engine::polyglot::DependencyResolutionStatus::MissingDependency,
          "resolving a root package that was never registered real-reports MissingDependency, not a crash");
}

// --- PackageRegistry ---------------------------------------------------

void testPackageRegistryRegisterAndResolve() {
    engine::polyglot::PackageRegistry registry;
    check(registry.registerManifest(makeManifest("Base")), "a real, valid manifest real-registers");
    check(registry.registerManifest(makeManifest("App", {"Base"})), "a second real, valid, dependent manifest real-registers");
    check(registry.find("App") != nullptr, "PackageRegistry::find() real-finds a registered manifest");

    auto result = registry.resolveInstallOrder("App");
    check(result.status == engine::polyglot::DependencyResolutionStatus::Ok,
          "PackageRegistry::resolveInstallOrder() real-delegates to a real, working DependencyResolver");
    check(result.installOrder.size() == 2, "the real full install order round-trips through the registry");
}

void testPackageRegistryRejectsEmptyPackageId() {
    engine::polyglot::PackageRegistry registry;
    engine::polyglot::PackageManifest manifest = makeManifest("");
    check(!registry.registerManifest(manifest), "PackageRegistry::registerManifest() real-rejects an empty packageId");
}

void testPackageRegistryRejectsSelfDependency() {
    engine::polyglot::PackageRegistry registry;
    check(!registry.registerManifest(makeManifest("Self", {"Self"})),
          "PackageRegistry::registerManifest() real-rejects a package depending on itself");
}

void testPackageRegistryRejectsArtifactWithEmptyPath() {
    engine::polyglot::PackageRegistry registry;
    engine::polyglot::PackageManifest manifest = makeManifest("Foo");
    manifest.artifacts.push_back({engine::polyglot::PackageArtifactKind::LuauModule, "", "sha256:x"});
    check(!registry.registerManifest(manifest), "PackageRegistry::registerManifest() real-rejects an artifact with an empty relativePath");
}

// --- VirtualFileSystem -------------------------------------------------------

void testVfsMountAndResolve() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/real/game/assets"), "a real, valid mount real-succeeds");
    std::string realPath;
    check(vfs.resolve("assets/models/box.obj", realPath), "resolve() real-succeeds against a real mounted prefix");
    check(realPath == "/real/game/assets/models/box.obj", "resolve() real-joins the mount directory with the real remainder");
}

void testVfsResolveBarePrefixResolvesToMountRoot() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/real/game/assets"), "setup mount succeeds");
    std::string realPath;
    check(vfs.resolve("assets", realPath), "resolving the bare prefix itself real-succeeds");
    check(realPath == "/real/game/assets", "the bare prefix real-resolves to the mount root, no trailing remainder");
}

void testVfsResolveFailsForUnmountedPrefix() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/real/game/assets"), "setup mount succeeds");
    std::string realPath;
    check(!vfs.resolve("scripts/main.luau", realPath), "resolve() real-fails for a prefix nothing is mounted at");
    check(realPath.empty(), "a failed resolve() real-leaves outRealPath empty");
}

void testVfsMountRejectsInvalidPrefix() {
    engine::polyglot::VirtualFileSystem vfs;
    check(!vfs.mount("assets/models", "/real/dir"), "mount() real-rejects a prefix containing a slash");
    check(!vfs.mount("", "/real/dir"), "mount() real-rejects an empty prefix");
    check(!vfs.mount("assets", ""), "mount() real-rejects an empty real directory");
    check(vfs.mountCount() == 0, "none of the real-rejected mount() calls actually mounted anything");
}

void testVfsResolveRejectsPathTraversalEscape() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/real/game/assets"), "setup mount succeeds");
    std::string realPath;
    check(!vfs.resolve("assets/../../etc/passwd", realPath),
          "resolve() real-rejects a virtual path that would lexically escape the mounted directory");
    check(realPath.empty(), "a rejected traversal-escape real-leaves outRealPath empty");
}

void testVfsResolvePicksHighestPriorityMount() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/base/assets", 0), "setup mount succeeds");
    check(vfs.mount("assets", "/override-pack/assets", 10), "setup mount succeeds");
    std::string realPath;
    check(vfs.resolve("assets/glow.mat", realPath), "resolve() real-succeeds with two mounts sharing one prefix");
    check(realPath == "/override-pack/assets/glow.mat",
          "resolve() real-picks the higher-priority mount when two share the same prefix");
}

void testVfsResolveTieBreaksOnMostRecentlyMounted() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/first/assets", 5), "setup mount succeeds");
    check(vfs.mount("assets", "/second/assets", 5), "setup mount succeeds"); // same priority
    std::string realPath;
    check(vfs.resolve("assets/glow.mat", realPath), "resolve() real-succeeds with two same-priority mounts");
    check(realPath == "/second/assets/glow.mat",
          "resolve() real-breaks a priority tie in favor of the most-recently-mounted");
}

void testVfsUnmountRemovesOnlyMostRecentMount() {
    engine::polyglot::VirtualFileSystem vfs;
    check(vfs.mount("assets", "/first/assets", 0), "setup mount succeeds");
    check(vfs.mount("assets", "/second/assets", 0), "setup mount succeeds");
    check(vfs.unmount("assets"), "unmount() real-succeeds while a mount for this prefix still exists");

    std::string realPath;
    check(vfs.resolve("assets/glow.mat", realPath), "resolve() still real-succeeds -- the real earlier mount is still there");
    check(realPath == "/first/assets/glow.mat",
          "unmount() real-removed only the most-recently-added mount, real-uncovering the one underneath it");

    check(vfs.unmount("assets"), "unmount() real-succeeds a second time on the real, last-remaining mount");
    check(!vfs.unmount("assets"), "unmount() real-fails once nothing is mounted at this prefix anymore");
    check(!vfs.resolve("assets/glow.mat", realPath), "resolve() real-fails once every mount for this prefix is gone");
}

void testVfsIsMountedAndMountCount() {
    engine::polyglot::VirtualFileSystem vfs;
    check(!vfs.isMounted("assets"), "isMounted() real-reports false before any mount() call");
    check(vfs.mount("assets", "/real/assets"), "setup mount succeeds");
    check(vfs.isMounted("assets"), "isMounted() real-reports true after a real, successful mount()");
    check(vfs.mountCount() == 1, "mountCount() real-reflects the one real mount registered");
}

} // namespace

int main() {
    testEventRingEmptyPopFails();
    testEventRingPushPopFifoOrder();
    testEventRingFullRejectsPush();
    testEventRingWraparoundPreservesOrder();
    testEventRingPayloadBytesSurviveRoundTrip();
    testEventRingConcurrentProducerConsumerStress();

    testTypeRegistryRegisterAndFind();
    testTypeRegistryFindMissingReturnsNull();
    testTypeRegistryRejectsEmptyName();
    testTypeRegistryRejectsDuplicateName();
    testTypeRegistryRejectsFieldSizeMismatch();
    testTypeRegistryRejectsFieldOutOfBounds();
    testTypeRegistryRejectsZeroTotalByteSize();

    testPackageManifestParserValidManifest();
    testPackageManifestParserRejectsMissingPackage();
    testPackageManifestParserRejectsMissingVersion();
    testPackageManifestParserRejectsDuplicatePackage();
    testPackageManifestParserRejectsUnrecognizedKeyword();
    testPackageManifestParserRejectsUnrecognizedArtifactKind();
    testPackageManifestParserRejectsArtifactBeforePackage();

    testDependencyResolverLinearChain();
    testDependencyResolverDiamondSharesCommonDependencyOnce();
    testDependencyResolverMissingDependency();
    testDependencyResolverCircularDependency();
    testDependencyResolverResolveUnknownRootIsMissingDependency();

    testPackageRegistryRegisterAndResolve();
    testPackageRegistryRejectsEmptyPackageId();
    testPackageRegistryRejectsSelfDependency();
    testPackageRegistryRejectsArtifactWithEmptyPath();

    testVfsMountAndResolve();
    testVfsResolveBarePrefixResolvesToMountRoot();
    testVfsResolveFailsForUnmountedPrefix();
    testVfsMountRejectsInvalidPrefix();
    testVfsResolveRejectsPathTraversalEscape();
    testVfsResolvePicksHighestPriorityMount();
    testVfsResolveTieBreaksOnMostRecentlyMounted();
    testVfsUnmountRemovesOnlyMostRecentMount();
    testVfsIsMountedAndMountCount();

    std::fprintf(stdout, "%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
