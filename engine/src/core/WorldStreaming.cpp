#include "core/WorldStreaming.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/Hierarchy.hpp"
#include "core/Physics.hpp"
#include "core/SceneSpatialIndex.hpp"

namespace engine::core {

namespace {

std::atomic<uint32_t> nextEpoch{0};

uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

std::vector<EntityId> collectSubtree(ECS& ecs, EntityId root) {
    std::vector<EntityId> subtree{root};
    for (size_t i = 0; i < subtree.size(); ++i) {
        if (const auto* links = ecs.tryGetComponent<Hierarchy>(subtree[i])) {
            for (EntityId child : links->children) subtree.push_back(child);
        }
    }
    return subtree;
}

float axisGap(float value, float min, float max) { return std::max({min - value, 0.0f, value - max}); }

} // namespace

std::string WorldManifest::directoryFor(const std::string& scenePath) { return scenePath + ".world"; }

std::string WorldManifest::pathFor(const std::string& scenePath) { return directoryFor(scenePath) + "/world.kworld"; }

WorldCellCoord WorldManifest::cellOf(const glm::vec3& position, float cellSize) {
    return {static_cast<int32_t>(std::floor(position.x / cellSize)), static_cast<int32_t>(std::floor(position.z / cellSize))};
}

std::string WorldManifest::cellFileName(WorldCellCoord coord, const std::string& extension) {
    return "cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + extension;
}

int WorldManifest::find(WorldCellCoord coord) const {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].coord == coord) return static_cast<int>(i);
    }
    return -1;
}

float WorldManifest::distanceTo(const Cell& cell, const glm::vec3& position) const {
    float minX = static_cast<float>(cell.coord.x) * cellSize;
    float minZ = static_cast<float>(cell.coord.z) * cellSize;
    float maxX = minX + cellSize;
    float maxZ = minZ + cellSize;
    if (cell.hasBounds) {
        minX = std::min(minX, cell.bounds.min.x);
        minZ = std::min(minZ, cell.bounds.min.z);
        maxX = std::max(maxX, cell.bounds.max.x);
        maxZ = std::max(maxZ, cell.bounds.max.z);
    }
    const float dx = axisGap(position.x, minX, maxX);
    const float dz = axisGap(position.z, minZ, maxZ);
    return std::sqrt(dx * dx + dz * dz);
}

bool WorldManifest::parse(const std::string& text, std::string& error) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("KWORLD", 0) != 0) {
        error = "not a world manifest";
        return false;
    }
    WorldManifest parsed;
    int lineNumber = 1;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key) || key[0] == '#') continue;
        bool ok = true;
        if (key == "cell_size") ok = static_cast<bool>(fields >> parsed.cellSize) && parsed.cellSize > 0.0f;
        else if (key == "load_radius") ok = static_cast<bool>(fields >> parsed.loadRadius);
        else if (key == "unload_radius") ok = static_cast<bool>(fields >> parsed.unloadRadius);
        else if (key == "extension") ok = static_cast<bool>(fields >> parsed.cellExtension);
        else if (key == "cell") {
            Cell cell;
            int hasBounds = 0;
            ok = static_cast<bool>(fields >> cell.coord.x >> cell.coord.z >> cell.entities >> hasBounds >>
                                   cell.bounds.min.x >> cell.bounds.min.y >> cell.bounds.min.z >> cell.bounds.max.x >>
                                   cell.bounds.max.y >> cell.bounds.max.z);
            fields >> std::ws;
            std::getline(fields, cell.file);
            cell.hasBounds = hasBounds != 0;
            ok = ok && !cell.file.empty() && cell.file.find("..") == std::string::npos && cell.file[0] != '/';
            if (ok && parsed.find(cell.coord) >= 0) {
                error = "line " + std::to_string(lineNumber) + ": cell listed twice";
                return false;
            }
            if (ok) parsed.cells.push_back(std::move(cell));
        }
        if (!ok) {
            error = "line " + std::to_string(lineNumber) + ": bad \"" + key + "\"";
            return false;
        }
    }
    parsed.unloadRadius = std::max(parsed.unloadRadius, parsed.loadRadius);
    *this = std::move(parsed);
    return true;
}

std::string WorldManifest::serialize() const {
    std::ostringstream out;
    out << "KWORLD 1\n";
    out << "cell_size " << cellSize << "\n";
    out << "load_radius " << loadRadius << "\n";
    out << "unload_radius " << unloadRadius << "\n";
    out << "extension " << cellExtension << "\n";
    for (const Cell& cell : cells) {
        out << "cell " << cell.coord.x << ' ' << cell.coord.z << ' ' << cell.entities << ' ' << (cell.hasBounds ? 1 : 0)
            << ' ' << cell.bounds.min.x << ' ' << cell.bounds.min.y << ' ' << cell.bounds.min.z << ' '
            << cell.bounds.max.x << ' ' << cell.bounds.max.y << ' ' << cell.bounds.max.z << ' ' << cell.file << "\n";
    }
    return out.str();
}

bool WorldManifest::load(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "no world manifest at " + path;
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str(), error);
}

bool WorldManifest::save(const std::string& path) const {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << serialize();
        if (!out.good()) return false;
    }
    std::filesystem::rename(temporary, path, ec);
    return !ec;
}

WorldStreamer::WorldStreamer() : edits_(std::make_shared<EditStore>()) {}

WorldStreamer::~WorldStreamer() { close(); }

uint64_t WorldStreamer::contentHash(const SceneFile& file) {
    // Order-independent: a cell captured from the ECS lists its entities in storage order.
    uint64_t hash = file.entities.size();
    SceneFile single;
    single.entities.resize(1);
    for (const SceneEntityRecord& record : file.entities) {
        single.entities[0] = record;
        std::ostringstream text;
        single.writeText(text);
        hash += fnv1a(text.str());
    }
    return hash;
}

bool WorldStreamer::open(const std::string& scenePath, ECS& ecs, ResourceManager& resources,
                         const SceneBuildContext& context, std::string* error) {
    close();
    WorldManifest manifest;
    std::string reason;
    if (!manifest.load(WorldManifest::pathFor(scenePath), reason)) {
        if (error != nullptr) *error = reason;
        return false;
    }
    return start(scenePath, manifest, ecs, resources, context);
}

bool WorldStreamer::create(const std::string& scenePath, const WorldManifest& settings, ECS& ecs,
                           ResourceManager& resources, const SceneBuildContext& context, std::string* error) {
    close();
    if (scenePath.empty()) {
        if (error != nullptr) *error = "Save the scene first (File > Save Scene As). Its cells are written next to it.";
        return false;
    }
    WorldManifest manifest = settings;
    manifest.cells.clear();
    manifest.unloadRadius = std::max(manifest.unloadRadius, manifest.loadRadius);
    if (!start(scenePath, manifest, ecs, resources, context)) return false;
    manifestDirty_ = true;
    return true;
}

bool WorldStreamer::start(const std::string& scenePath, const WorldManifest& manifest, ECS& ecs,
                          ResourceManager& resources, const SceneBuildContext& context) {
    ecs_ = &ecs;
    resources_ = &resources;
    context_ = context;
    scenePath_ = scenePath;
    directory_ = WorldManifest::directoryFor(scenePath);
    manifest_ = manifest;
    epoch_ = nextEpoch.fetch_add(1) % 0x7FFF + 1;
    handles_.assign(manifest_.cells.size(), {});
    cellSlot_.assign(manifest_.cells.size(), -1);
    cellByPath_.clear();
    for (size_t i = 0; i < manifest_.cells.size(); ++i) {
        cellByPath_[ResourceManager::normalizePath(cellPath(i))] = static_cast<int>(i);
    }
    counters_ = {};
    manifestDirty_ = false;
    {
        std::lock_guard lock(edits_->mutex);
        edits_->files.clear();
    }
    installLoader();
    return true;
}

void WorldStreamer::close() {
    if (!isOpen()) return;
    handles_.clear();
    resources_->unloadUnreferenced(ResourceKind::Scene);
    for (Slot& slot : slots_) {
        if (slot.live) destroySlotEntities(slot);
    }
    resources_->setLoader(ResourceKind::Scene, {});
    for (const auto& [key, shared] : sharedMeshes_) pendingDestroy_.push_back({shared.handle, 0});
    if (!pendingDestroy_.empty() && waitForGpu) waitForGpu();
    updates_ = ~0ull >> 1;
    collectGarbage();
    updates_ = 0;
    freeMeshHandles_.clear();
    slots_.clear();
    cellSlot_.clear();
    cellByPath_.clear();
    sharedHandles_.clear();
    sharedMeshes_.clear();
    ecs_ = nullptr;
    resources_ = nullptr;
    scenePath_.clear();
}

std::string WorldStreamer::cellPath(size_t index) const { return directory_ + "/" + manifest_.cells[index].file; }

int WorldStreamer::cellForPath(const std::string& normalizedPath) const {
    auto it = cellByPath_.find(normalizedPath);
    return it == cellByPath_.end() ? -1 : it->second;
}

int WorldStreamer::addCell(WorldCellCoord coord) {
    WorldManifest::Cell cell;
    cell.coord = coord;
    cell.file = WorldManifest::cellFileName(coord, manifest_.cellExtension);
    manifest_.cells.push_back(cell);
    handles_.emplace_back();
    cellSlot_.push_back(-1);
    const int index = static_cast<int>(manifest_.cells.size() - 1);
    cellByPath_[ResourceManager::normalizePath(cellPath(static_cast<size_t>(index)))] = index;
    manifestDirty_ = true;
    return index;
}

WorldStreamer::Slot* WorldStreamer::slotFor(uint32_t handle) {
    if (handle >> 16 != epoch_) return nullptr;
    const size_t index = handle & 0xFFFF;
    return index < slots_.size() && slots_[index].live ? &slots_[index] : nullptr;
}

void WorldStreamer::installLoader() {
    ResourceLoader loader;
    loader.reserve = [this] {
        size_t index = 0;
        while (index < slots_.size() && slots_[index].live) ++index;
        if (index == slots_.size()) slots_.emplace_back();
        slots_[index] = Slot{};
        slots_[index].live = true;
        return handleFor(index);
    };
    loader.decode = [edits = edits_](const std::string& path, std::string& error) -> std::unique_ptr<ResourcePayload> {
        auto decoded = std::make_unique<DecodedCell>();
        decoded->path = path;
        bool edited = false;
        {
            std::lock_guard lock(edits->mutex);
            if (auto it = edits->files.find(path); it != edits->files.end()) {
                decoded->file = it->second;
                decoded->edited = edited = true;
            }
        }
        if (!edited && !decoded->file.loadFromFile(path)) {
            if (std::filesystem::exists(path)) {
                error = "could not read cell " + path;
                return nullptr;
            }
            decoded->file = SceneFile{}; // a cell created this session and not saved yet
        }
        decoded->hash = contentHash(decoded->file);
        return decoded;
    };
    loader.commit = [this](ResourcePayload& payload, uint32_t handle, bool reloading, std::string& error) {
        return commit(static_cast<DecodedCell&>(payload), handle, reloading, error);
    };
    loader.release = [this](uint32_t handle) { release(handle); };
    resources_->setLoader(ResourceKind::Scene, std::move(loader));
}

bool WorldStreamer::commit(DecodedCell& decoded, uint32_t handle, bool reloading, std::string& error) {
    Slot* slot = slotFor(handle);
    if (slot == nullptr) {
        error = "cell slot is gone";
        return false;
    }
    const int cell = cellForPath(decoded.path);
    if (cell < 0) {
        error = "not a cell of this world";
        return false;
    }
    if (reloading && slot->cell == cell && slot->hash == decoded.hash) return true; // our own save coming back
    destroySlotEntities(*slot);
    slot->cell = cell;
    slot->hash = decoded.hash;
    slot->unsaved = decoded.edited;
    cellSlot_[static_cast<size_t>(cell)] = static_cast<int>(slot - slots_.data());
    instantiate(*slot, decoded.file);
    ++counters_.loads;
    return true;
}

void WorldStreamer::instantiate(Slot& slot, const SceneFile& file) {
    SceneBuildContext context = context_;
    context.sharedMeshes = &sharedHandles_;
    context.registerMesh = [this](Mesh&& mesh) { return registerMesh(std::move(mesh)); };
    std::vector<EntityId> created;
    instantiateSceneEntities(file.entities, *ecs_, context, &created);
    for (EntityId entity : created) {
        ecs_->addComponent<StreamedCell>(entity, StreamedCell{static_cast<uint32_t>(slot.cell)});
        const MeshSource* source = ecs_->tryGetComponent<MeshSource>(entity);
        if (source == nullptr || context.device == VK_NULL_HANDLE || meshSourceFileBacked(source->kind)) continue;
        std::string key = meshSourceKey(*source);
        auto handle = sharedHandles_.find(key);
        if (handle == sharedHandles_.end()) continue;
        SharedMesh& shared = sharedMeshes_[key];
        shared.handle = handle->second;
        ++shared.references;
        slot.meshKeys.push_back(std::move(key));
    }
}

uint32_t WorldStreamer::registerMesh(Mesh mesh) {
    if (!freeMeshHandles_.empty()) {
        const uint32_t handle = freeMeshHandles_.back();
        freeMeshHandles_.pop_back();
        context_.meshLibrary->replaceMesh(handle, std::move(mesh), context_.allocator);
        return handle;
    }
    return context_.meshLibrary->registerMesh(std::move(mesh));
}

void WorldStreamer::detach(EntityId entity, int cell) {
    if (!ecs_->raw().valid(entity)) return;
    if (const auto* links = ecs_->tryGetComponent<Hierarchy>(entity)) {
        const std::vector<EntityId> children = links->children;
        for (EntityId child : children) {
            const auto* tag = ecs_->tryGetComponent<StreamedCell>(child);
            if (tag == nullptr || static_cast<int>(tag->cell) != cell) hierarchy::unparent(*ecs_, child);
        }
        hierarchy::unparent(*ecs_, entity);
    }
}

void WorldStreamer::destroyEntity(EntityId entity) {
    if (!ecs_->raw().valid(entity)) return;
    if (beforeDestroy) beforeDestroy(*ecs_, entity);
    if (const auto* body = ecs_->tryGetComponent<RigidBody>(entity);
        body != nullptr && body->joltBodyId != RigidBody::kInvalidBodyId && context_.physics != nullptr) {
        context_.physics->detachBody(entity, *ecs_);
    }
    ecs_->destroyEntity(entity);
}

void WorldStreamer::destroySlotEntities(Slot& slot) {
    const std::vector<EntityId> entities = cellEntities(slot.cell);
    for (EntityId entity : entities) detach(entity, slot.cell);
    for (EntityId entity : entities) destroyEntity(entity);
    for (const std::string& key : slot.meshKeys) {
        auto it = sharedMeshes_.find(key);
        if (it == sharedMeshes_.end() || --it->second.references != 0) continue;
        if (deferDestroy) {
            deferDestroy([library = context_.meshLibrary, allocator = context_.allocator, handle = it->second.handle] {
                library->destroyMesh(handle, allocator);
            });
        } else {
            pendingDestroy_.push_back({it->second.handle, updates_ + 4});
        }
        sharedHandles_.erase(key);
        sharedMeshes_.erase(it);
    }
    slot.meshKeys.clear();
}

void WorldStreamer::release(uint32_t handle) {
    Slot* slot = slotFor(handle);
    if (slot == nullptr) return;
    if (editing_ && slot->cell >= 0) {
        SceneFile captured = captureSlot(*slot);
        if (slot->unsaved || contentHash(captured) != slot->hash) {
            std::lock_guard lock(edits_->mutex);
            edits_->files[ResourceManager::normalizePath(cellPath(static_cast<size_t>(slot->cell)))] = std::move(captured);
        }
    }
    destroySlotEntities(*slot);
    if (slot->cell >= 0) cellSlot_[static_cast<size_t>(slot->cell)] = -1;
    *slot = Slot{};
    ++counters_.unloads;
}

void WorldStreamer::collectGarbage() {
    auto due = std::partition(pendingDestroy_.begin(), pendingDestroy_.end(),
                              [this](const PendingDestroy& pending) { return pending.at > updates_; });
    for (auto it = due; it != pendingDestroy_.end(); ++it) {
        context_.meshLibrary->destroyMesh(it->handle, context_.allocator);
        freeMeshHandles_.push_back(it->handle);
    }
    pendingDestroy_.erase(due, pendingDestroy_.end());
}

void WorldStreamer::setRadii(float loadRadius, float unloadRadius) {
    manifest_.loadRadius = std::max(0.0f, loadRadius);
    manifest_.unloadRadius = std::max(manifest_.loadRadius, unloadRadius);
    manifestDirty_ = true;
}

void WorldStreamer::update(const std::vector<glm::vec3>& sources, const std::vector<float>& radiusScales) {
    if (!isOpen()) return;
    ++updates_;
    collectGarbage();
    for (size_t i = 0; i < manifest_.cells.size(); ++i) {
        bool want = loadAll_;
        bool keep = loadAll_;
        for (size_t s = 0; s < sources.size() && !want; ++s) {
            const float scale = s < radiusScales.size() && radiusScales[s] > 0.0f ? radiusScales[s] : 1.0f;
            const float distance = manifest_.distanceTo(manifest_.cells[i], sources[s]) / scale;
            want = distance <= manifest_.loadRadius;
            keep = keep || distance <= manifest_.unloadRadius;
        }
        ResourceHandle& handle = handles_[i];
        if (!handle.valid() && want) handle = resources_->acquire(ResourceKind::Scene, cellPath(i));
        else if (handle.valid() && !want && !keep) handle.reset();
    }
}

SceneFile WorldStreamer::captureSlot(const Slot& slot) const {
    SceneFile file;
    for (EntityId entity : cellEntities(slot.cell)) {
        SceneEntityRecord record;
        if (captureSceneEntity(*ecs_, entity, record)) file.entities.push_back(std::move(record));
    }
    return file;
}

Aabb WorldStreamer::entityBounds(EntityId entity) {
    if (const auto* index = ecs_->raw().ctx().find<SceneSpatialIndex>()) {
        if (const Aabb* bounds = index->worldBounds(entity)) return *bounds;
    }
    const glm::vec3 position(hierarchy::computeWorldMatrix(*ecs_, entity)[3]);
    return {position, position};
}

void WorldStreamer::refreshCellInfo(size_t cell, const SceneFile& file) {
    WorldManifest::Cell& info = manifest_.cells[cell];
    info.entities = static_cast<uint32_t>(file.entities.size());
    info.hasBounds = false;
    auto grow = [&info](const Aabb& box) {
        info.bounds = info.hasBounds ? Aabb::merged(info.bounds, box) : box;
        info.hasBounds = true;
    };
    const int slot = cellSlot_[cell];
    if (slot >= 0) {
        for (EntityId entity : cellEntities(static_cast<int>(cell))) grow(entityBounds(entity));
    } else {
        for (const SceneEntityRecord& record : file.entities) {
            if (record.parentName.empty()) grow({record.position, record.position});
        }
    }
    manifestDirty_ = true;
}

bool WorldStreamer::adoptable(ECS& ecs, EntityId entity, float cellSize) {
    const Name* name = ecs.tryGetComponent<Name>(entity);
    if (name == nullptr || name->value.empty()) return false;
    if (ecs.hasComponent<StreamedCell>(entity) || ecs.hasComponent<Script>(entity) ||
        ecs.hasComponent<VisualScript>(entity) || ecs.hasComponent<SkinnedRenderable>(entity) ||
        ecs.hasComponent<StreamingSource>(entity)) {
        return false;
    }
    if (const auto* links = ecs.tryGetComponent<Hierarchy>(entity); links != nullptr && links->parent != kNullEntity) {
        return false;
    }
    if (const auto* body = ecs.tryGetComponent<RigidBody>(entity);
        body != nullptr && body->motionType != RigidBodyMotionType::Static) {
        return false;
    }
    if (!ecs.hasComponent<Renderable>(entity) && !ecs.hasComponent<Light>(entity) &&
        !ecs.hasComponent<ParticleEmitter>(entity) && !ecs.hasComponent<AudioSource>(entity)) {
        return false;
    }
    if (const auto* index = ecs.raw().ctx().find<SceneSpatialIndex>()) {
        if (const Aabb* bounds = index->worldBounds(entity)) {
            const glm::vec3 size = bounds->max - bounds->min;
            if (std::max(size.x, size.z) > 2.0f * cellSize) return false;
        }
    }
    return true;
}

size_t WorldStreamer::adopt(ECS& ecs) {
    if (!isOpen() || &ecs != ecs_) return 0;
    std::vector<EntityId> roots;
    for (auto entity : ecs.view<Transform, Name>()) {
        if (adoptable(ecs, entity, manifest_.cellSize)) roots.push_back(entity);
    }

    size_t adopted = 0;
    for (EntityId root : roots) {
        const std::vector<EntityId> subtree = collectSubtree(ecs, root);
        const glm::vec3 position(hierarchy::computeWorldMatrix(ecs, root)[3]);
        const WorldCellCoord coord = WorldManifest::cellOf(position, manifest_.cellSize);
        int cell = manifest_.find(coord);
        if (cell < 0) cell = addCell(coord);

        if (cellSlot_[static_cast<size_t>(cell)] >= 0) {
            for (EntityId entity : subtree) ecs.addComponent<StreamedCell>(entity, StreamedCell{static_cast<uint32_t>(cell)});
        } else {
            const std::string path = ResourceManager::normalizePath(cellPath(static_cast<size_t>(cell)));
            std::lock_guard lock(edits_->mutex);
            auto edit = edits_->files.find(path);
            if (edit == edits_->files.end()) {
                SceneFile existing;
                if (!existing.loadFromFile(path)) existing = SceneFile{};
                edit = edits_->files.emplace(path, std::move(existing)).first;
            }
            for (EntityId entity : subtree) {
                SceneEntityRecord record;
                if (captureSceneEntity(ecs, entity, record)) edit->second.entities.push_back(std::move(record));
            }
        }
        adopted += subtree.size();
    }

    // Entities headed for unloaded cells now live in edits; they come back when their cell loads.
    for (EntityId root : roots) {
        if (!ecs.raw().valid(root) || ecs.hasComponent<StreamedCell>(root)) continue;
        const std::vector<EntityId> subtree = collectSubtree(ecs, root);
        for (EntityId entity : subtree) detach(entity, -1);
        for (EntityId entity : subtree) destroyEntity(entity);
    }
    return adopted;
}

bool WorldStreamer::moveTo(const std::string& newScenePath, std::string* error) {
    if (!isOpen()) return false;
    const std::string newDirectory = WorldManifest::directoryFor(newScenePath);
    std::error_code ec;
    std::filesystem::create_directories(newDirectory, ec);
    if (std::filesystem::exists(directory_)) {
        std::filesystem::copy(directory_, newDirectory,
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        if (error != nullptr) *error = "could not copy the world to " + newDirectory + ": " + ec.message();
        return false;
    }
    std::vector<std::string> oldPaths;
    for (size_t i = 0; i < manifest_.cells.size(); ++i) oldPaths.push_back(ResourceManager::normalizePath(cellPath(i)));
    scenePath_ = newScenePath;
    directory_ = newDirectory;
    // Old paths stay mapped: cells still loaded from there can finish and unload.
    std::lock_guard lock(edits_->mutex);
    for (size_t i = 0; i < manifest_.cells.size(); ++i) {
        const std::string path = ResourceManager::normalizePath(cellPath(i));
        cellByPath_[path] = static_cast<int>(i);
        if (auto edit = edits_->files.find(oldPaths[i]); edit != edits_->files.end()) {
            SceneFile file = std::move(edit->second);
            edits_->files.erase(edit);
            edits_->files[path] = std::move(file);
        }
    }
    manifestDirty_ = true;
    return true;
}

void WorldStreamer::appendAllCells(SceneFile& file) const {
    if (!isOpen()) return;
    for (size_t i = 0; i < manifest_.cells.size(); ++i) {
        SceneFile cell;
        if (cellSlot_[i] >= 0) {
            cell = captureSlot(slots_[static_cast<size_t>(cellSlot_[i])]);
        } else {
            const std::string path = ResourceManager::normalizePath(cellPath(i));
            std::lock_guard lock(edits_->mutex);
            if (auto edit = edits_->files.find(path); edit != edits_->files.end()) cell = edit->second;
            else if (!cell.loadFromFile(path)) continue;
        }
        for (SceneEntityRecord& record : cell.entities) file.entities.push_back(std::move(record));
    }
}

bool WorldStreamer::hasUnsavedCells() const {
    if (!isOpen()) return false;
    if (manifestDirty_) return true;
    {
        std::lock_guard lock(edits_->mutex);
        if (!edits_->files.empty()) return true;
    }
    for (const Slot& slot : slots_) {
        if (slot.live && slot.cell >= 0 && (slot.unsaved || contentHash(captureSlot(slot)) != slot.hash)) return true;
    }
    return false;
}

bool WorldStreamer::saveCells(std::string* error) {
    if (!isOpen()) {
        if (error != nullptr) *error = "no world is open";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);

    // Entities move to the cell under them when that cell is loaded too;
    // otherwise they stay where they are so nothing loads twice.
    std::unordered_map<int, SceneFile> outputs;
    std::vector<std::pair<EntityId, int>> moves;
    for (Slot& slot : slots_) {
        if (!slot.live || slot.cell < 0) continue;
        outputs[slot.cell];
        for (EntityId entity : cellEntities(slot.cell)) {
            EntityId root = entity;
            for (int depth = 0; depth < 64; ++depth) {
                const auto* links = ecs_->tryGetComponent<Hierarchy>(root);
                if (links == nullptr || links->parent == kNullEntity || !ecs_->hasComponent<StreamedCell>(links->parent)) break;
                root = links->parent;
            }
            const glm::vec3 position(hierarchy::computeWorldMatrix(*ecs_, root)[3]);
            int target = manifest_.find(WorldManifest::cellOf(position, manifest_.cellSize));
            if (target < 0 || cellSlot_[static_cast<size_t>(target)] < 0) target = slot.cell;
            SceneEntityRecord record;
            if (captureSceneEntity(*ecs_, entity, record)) outputs[target].entities.push_back(std::move(record));
            if (target != slot.cell) moves.emplace_back(entity, target);
        }
    }
    for (const auto& [entity, target] : moves) ecs_->addComponent<StreamedCell>(entity, StreamedCell{static_cast<uint32_t>(target)});

    std::unordered_map<std::string, SceneFile> edits;
    {
        std::lock_guard lock(edits_->mutex);
        edits = edits_->files;
    }
    for (auto& [path, file] : edits) {
        const int cell = cellForPath(path);
        if (cell >= 0 && cellSlot_[static_cast<size_t>(cell)] < 0) outputs[cell] = std::move(file);
    }

    bool ok = true;
    for (auto& [cell, file] : outputs) {
        const uint64_t hash = contentHash(file);
        const int slot = cellSlot_[static_cast<size_t>(cell)];
        const std::string path = cellPath(static_cast<size_t>(cell));
        const bool unchanged = slot >= 0 && !slots_[static_cast<size_t>(slot)].unsaved &&
                               slots_[static_cast<size_t>(slot)].hash == hash && std::filesystem::exists(path);
        if (!unchanged) {
            if (!file.saveToFile(path)) {
                if (error != nullptr) *error = "could not write " + path;
                ok = false;
                continue;
            }
            if (slot >= 0) {
                slots_[static_cast<size_t>(slot)].hash = hash;
                slots_[static_cast<size_t>(slot)].unsaved = false;
            }
        }
        refreshCellInfo(static_cast<size_t>(cell), file);
    }
    if (ok) {
        std::lock_guard lock(edits_->mutex);
        edits_->files.clear();
    }
    if (!manifest_.save(WorldManifest::pathFor(scenePath_))) {
        if (error != nullptr) *error = "could not write the world manifest";
        return false;
    }
    manifestDirty_ = false;
    return ok;
}

WorldCellState WorldStreamer::cellState(size_t index) const {
    if (index >= handles_.size()) return WorldCellState::Unloaded;
    if (cellSlot_[index] >= 0) return WorldCellState::Loaded;
    const ResourceHandle& handle = handles_[index];
    if (!handle.valid()) return WorldCellState::Unloaded;
    return handle.state() == ResourceState::Failed ? WorldCellState::Failed : WorldCellState::Loading;
}

std::vector<EntityId> WorldStreamer::cellEntities(int cell) const {
    std::vector<EntityId> entities;
    if (cell < 0) return entities;
    for (auto [entity, tag] : ecs_->raw().view<StreamedCell>().each()) {
        if (static_cast<int>(tag.cell) == cell) entities.push_back(entity);
    }
    return entities;
}

std::vector<uint32_t> WorldStreamer::entityCounts() const {
    std::vector<uint32_t> counts(manifest_.cells.size(), 0);
    if (!isOpen()) return counts;
    for (auto [entity, tag] : ecs_->raw().view<StreamedCell>().each()) {
        if (tag.cell < counts.size()) ++counts[tag.cell];
    }
    return counts;
}

WorldStreamingStats WorldStreamer::stats() const {
    WorldStreamingStats stats = counters_;
    stats.cells = manifest_.cells.size();
    for (size_t i = 0; i < stats.cells; ++i) {
        switch (cellState(i)) {
            case WorldCellState::Loaded: ++stats.loaded; break;
            case WorldCellState::Loading: ++stats.loading; break;
            case WorldCellState::Failed: ++stats.failed; break;
            case WorldCellState::Unloaded: break;
        }
    }
    for (uint32_t count : entityCounts()) stats.entities += count;
    std::lock_guard lock(edits_->mutex);
    stats.editedCells = edits_->files.size();
    return stats;
}

} // namespace engine::core
