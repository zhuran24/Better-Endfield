// Progress-locked accessory writes; unity_hierarchy.inl binds transforms and
// dash_pose_lease.inl supplies ownership.
std::shared_ptr<const AccessoryTable> g_accessory_table;
std::filesystem::file_time_type g_accessory_stamp{};
bool g_accessory_checked = false;
std::string g_accessory_table_status;
void AccessoryTableStatus(const char* filename, const std::string& reason, bool diagnostics) {
    const std::string status = std::string(filename) + ": " + reason;
    if (status == g_accessory_table_status) return;
    g_accessory_table_status = status;
    if (diagnostics) Log(("Sustained dash Jsspsi accessory: table " + status).c_str());
}

void LoadAccessoryTable(const char* filename, bool diagnostics) {
    const auto context = g_dash_pose;
    const auto handle = g_session.handle;
    try {
        const auto file = ActionsDataFile(filename);
        if (file.empty()) { g_accessory_table.reset(); g_accessory_checked=false;
            AccessoryTableStatus(filename, "rejected: actions data directory unavailable", diagnostics); return; }
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time(file, error);
        if (g_accessory_checked && !error && stamp == g_accessory_stamp) return;
        g_accessory_checked = false;
        // A bad replacement must not silently keep a previous valid table.
        g_accessory_table.reset();
        if (error) { AccessoryTableStatus(filename, "rejected: absent or unreadable", diagnostics); return; }
        std::ifstream input(file, std::ios::binary);
        auto next = std::make_shared<AccessoryTable>();
        std::string reason;
        if (!input || !next->Load(input, reason)) {
            AccessoryTableStatus(filename, "rejected: " + (reason.empty() ? std::string("open failed") : reason), diagnostics); return;
        }
        const auto after = std::filesystem::last_write_time(file, error);
        if (error || after != stamp) {
            g_accessory_checked = false;
            AccessoryTableStatus(filename, "rejected: changed during read; retry next dash", diagnostics); return;
        }
        AccessoryTableStatus(filename, "loaded", false);
        g_accessory_table = std::move(next);
        g_accessory_checked = true; g_accessory_stamp = stamp;
        if (diagnostics) {
            char text[192];
            std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: table loaded entries=%zu", g_accessory_table->entries.size()); Log(text);
            for (const auto& e : g_accessory_table->entries) {
                std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: entry state=%d begin=%.6f target=%.6f N=%u M=%zu",
                    e.state_hash, e.begin, e.target, e.sample_count, e.bones.size()); Log(text);
            }
        }
    } catch (...) {
        g_accessory_table.reset(); g_accessory_checked = false;
        if (context == g_dash_pose && handle == g_session.handle) FailDashFeatures();
    }
}
struct AccessoryBinding {
    void* transform = nullptr;
    size_t sample = 0;
    BonePose output{};
    BonePose held{};
    bool planned = false, failed = false, held_valid = false;
};
struct AccessoryOwner {
    void* component = nullptr;
    void* handle = nullptr;
    void* root = nullptr;
    void* entity = nullptr;
    uint32_t component_pin = 0, entity_pin = 0;
    std::shared_ptr<const Configuration> config;
    std::shared_ptr<const AccessoryTable> table;
    BoundHierarchy hierarchy;
    const CharacterProfile* profile = nullptr;
    std::shared_ptr<DashPoseContext> lease_context;
    std::shared_ptr<DashRootLease> root_lease;
    const AccessoryEntry* entry = nullptr;
    std::vector<AccessoryBinding> bindings;
    Frame snapshot{};
    int hash = 0, frame = -1, applied_frame = -1;
    uint64_t frames = 0;
    size_t skipped = 0;
    size_t bone_messages = 0, bone_failures = 0;
    double phase = 0;
    double fade_start = 0, fade_seconds = 0;
    bool frame_valid = false, matched_frame = false, bound = false, blocked = false, logged = false, fading = false;
    bool fade_in_progress = false, fade_logged = false;
    ~AccessoryOwner() {
        for (auto pin : {component_pin, entity_pin}) if (pin) g_host->gchandle_free(g_host->context, pin);
    }
};
// Explicit shutdown/cancel owns cleanup; process teardown must not call Host.
auto& g_accessory = *new std::shared_ptr<AccessoryOwner>;
std::shared_ptr<AccessoryOwner> AccessoryOwnerSnapshot() {
    return g_accessory;
}
std::shared_ptr<AccessoryOwner> AccessoryExchange(std::shared_ptr<AccessoryOwner> next) {
    auto old = std::exchange(g_accessory, std::move(next));
    g_accessory_live.store(g_accessory != nullptr, std::memory_order_relaxed);
    return old;
}
void EndAccessory(const char* reason = "released") {
    std::lock_guard<std::recursive_mutex> feature_lock(g_feature_mutex);
    auto old = AccessoryExchange({});
    if (old && old->fading && old->lease_context) old->lease_context->End();
    if (old && !g_stopping && GetCurrentThreadId() == g_game_thread.load(std::memory_order_relaxed) && old->config->diagnostics) {
        char text[256];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: ended reason=%s skipped=%zu failed=%zu", reason, old->skipped, old->bone_failures); Log(text);
    }
}
bool AccessoryNow(double& seconds) {
    LARGE_INTEGER counter{}, frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 || !QueryPerformanceCounter(&counter)) return false;
    seconds = static_cast<double>(counter.QuadPart) / frequency.QuadPart;
    return std::isfinite(seconds);
}
void StopAccessoryFade() {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    auto owner = AccessoryOwnerSnapshot();
    if (owner && owner->fading) EndAccessory("new dash");
}
thread_local unsigned g_accessory_start_depth = 0;
struct AccessoryStartScope {
    explicit AccessoryStartScope(bool main_character) : active(main_character) { if (active) { ++g_accessory_start_depth; StopAccessoryFade(); } }
    bool active;
    ~AccessoryStartScope() { if (active) --g_accessory_start_depth; }
    AccessoryStartScope(const AccessoryStartScope&) = delete;
    AccessoryStartScope& operator=(const AccessoryStartScope&) = delete;
};
void ReleaseAccessoryComponent(void* component) {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::recursive_mutex> feature_lock(g_feature_mutex);
    auto owner = AccessoryOwnerSnapshot();
    if (owner && owner->component == component) EndAccessory("component released");
}
void CancelAccessory(bool allow_fade) {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    auto owner = AccessoryOwnerSnapshot();
    if (!owner) return;
    const auto config = g_config.load();
    // Do not inspect mutable owner fields or invoke Unity on worker shutdown.
    if (!allow_fade || g_stopping || GetCurrentThreadId() != g_game_thread.load(std::memory_order_relaxed) ||
        g_accessory_start_depth || !config->enabled || !config->jsspsi_accessory_loop || config->jsspsi_capture || CaptureActive()) {
        EndAccessory("fade forbidden"); return;
    }
    if (owner->fading) return; // Reentrant cancel must not restart the real-time clock.
    if (!owner->frames || owner->blocked || !owner->bound || config->jsspsi_accessory_fade <= 0 ||
        !owner->root_lease || !owner->root_lease->Owns() ||
        std::none_of(owner->bindings.begin(), owner->bindings.end(), [](const auto& b) { return b.held_valid && !b.failed; })) {
        EndAccessory("no fade eligible pose"); return;
    }
    if (!AccessoryNow(owner->fade_start)) { EndAccessory("clock unavailable"); return; }
    owner->fade_seconds = config->jsspsi_accessory_fade;
    owner->fading = true; owner->matched_frame = false;
    for (auto& bone : owner->bindings) bone.planned = false;
    if (owner->config->diagnostics) {
        char text[192];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: fade started seconds=%.3f held=%zu frames=%llu",
            owner->fade_seconds, static_cast<size_t>(std::count_if(owner->bindings.begin(), owner->bindings.end(),
                [](const auto& b) { return b.held_valid && !b.failed; })), static_cast<unsigned long long>(owner->frames)); Log(text);
    }
}
void BeginAccessory(void* component) {
    const auto context = g_dash_pose;
    const auto handle = g_session.handle;
    EndAccessory();
    if (!DashFeaturesActive() || component != g_session.component || !g_hierarchy_contract ||
        !g_session.config || !g_session.config->jsspsi_accessory_loop || g_session.config->jsspsi_capture || CaptureActive()) return;
    if (!g_session.profile->accessory_file || !g_dash_pose) return;
    LoadAccessoryTable(g_session.profile->accessory_file, g_session.config->diagnostics);
    if (!DashFeaturesActive() || !g_dash_pose) return;
    try {
        auto owner = std::make_shared<AccessoryOwner>();
        owner->component = component; owner->handle = g_session.handle;
        owner->lease_context = g_dash_pose; owner->profile = g_session.profile;
        owner->config = g_session.config; owner->table = g_accessory_table;
        owner->component_pin = g_host->gchandle_new(g_host->context, component, 1);
        if (context != g_dash_pose || handle != g_session.handle || g_stopping) return;
        if (!owner->component_pin) { Log("Sustained dash Jsspsi accessory: session unavailable: component pin failed"); return; }
        AccessoryExchange(std::move(owner));
    } catch (...) {
        if (context == g_dash_pose && handle == g_session.handle) FailDashFeatures();
    }
}
bool AccessoryCurrent(const std::shared_ptr<AccessoryOwner>& owner) {
    return owner && !g_stopping && owner->lease_context && owner->lease_context->valid && owner->lease_context == g_dash_pose && DashFeaturesActive() && owner == AccessoryOwnerSnapshot() && !owner->fading &&
        owner->component == g_session.component && owner->handle == g_session.handle &&
        owner->config == g_session.config && owner->config == g_config.load() &&
        owner->config->enabled && owner->config->jsspsi_accessory_loop && !owner->config->jsspsi_capture && !CaptureActive() &&
        g_session.policy.active;
}
void AccessoryBlock(const std::shared_ptr<AccessoryOwner>& owner, const char* reason) {
    owner->blocked = true; owner->matched_frame = false;
    for (auto& bone : owner->bindings) bone.planned = false;
    // This path also handles bad_alloc: it must not allocate again.
    char text[384];
    std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: dash disabled: %s", reason); Log(text);
}
bool BindAccessory(const std::shared_ptr<AccessoryOwner>& owner) {
    bool ok = true;
    void* animator = DashObject(GetAnimator, owner->component, ok);
    if (!ok || !AccessoryCurrent(owner)) return false;
    owner->root = DashObject(PoseComponentTransform, animator, ok);
    if (!ok || !DashObjectAlive(owner->root) || !AccessoryCurrent(owner)) return false;
    owner->root_lease = AcquireDashRoot(owner->lease_context, 0, owner->root);
    if (!owner->root_lease) return false;
    PoseCallScope pose_scope(owner->lease_context, owner->root_lease);
    if (!BindAccessoryHierarchy(owner->hierarchy, animator, owner->root) || !AccessoryCurrent(owner)) return false;
    owner->entity = DashObject(GetEntity, owner->component, ok);
    if (!ok || !AccessoryCurrent(owner) || !owner->root_lease->pin || !owner->entity) return false;
    owner->entity_pin = g_host->gchandle_new(g_host->context, owner->entity, 1);
    if (!owner->entity_pin) return false;
    std::unordered_map<std::string, std::vector<void*>> names;
    std::unordered_map<std::string, size_t> occurrences;
    for (const auto& bone : owner->hierarchy.bones) names[bone.name].push_back(bone.transform);
    for (const auto& bone : owner->entry->bones) ++occurrences[bone.name];
    std::unordered_set<std::string> logged;
    for (size_t i = 0; i < owner->entry->bones.size(); ++i) {
        const auto& spec = owner->entry->bones[i];
        if (!(owner->config->jsspsi_accessory_mask & (uint32_t(1) << spec.group))) { ++owner->skipped; continue; }
        const auto found = names.find(spec.name);
        const bool duplicate = occurrences[spec.name] > 1 || (found != names.end() && found->second.size() != 1);
        if (duplicate || found == names.end()) {
            ++owner->skipped;
            if (owner->config->diagnostics && logged.insert(spec.name).second && owner->bone_messages++ < 4)
                Log((std::string("Sustained dash Jsspsi accessory: bone skipped (") + (duplicate ? "duplicate leaf): " : "missing leaf): ") + spec.name).c_str());
            continue;
        }
        void* transform = found->second.front();
        owner->bindings.push_back({transform, i});
    }
    owner->bound = true;
    if (owner->config->diagnostics) {
        char text[192];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: dash reserved state=%d mask=0x%08x bound=%zu skipped=%zu before_rewind=%d",
            owner->hash, owner->config->jsspsi_accessory_mask, owner->bindings.size(), owner->skipped, !g_session.rewound); Log(text);
    }
    return true;
}
void AccessoryPrepareFrame(void* component) {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    // Off means no new managed calls, file reads, binding or pose changes.
    auto owner = AccessoryOwnerSnapshot();
    if (!owner || component != owner->component || !AccessoryCurrent(owner) || owner->blocked || !owner->table) return;
    try {
        DashCallScope frame_calls([&] { return AccessoryCurrent(owner); });
        std::optional<PoseCallScope> pose_scope;
        if (owner->root_lease) pose_scope.emplace(owner->lease_context,owner->root_lease);
        bool ok = true;
        const int frame = DashUnbox<int>(DashInvoke(PoseFrameCount, nullptr, nullptr, ok), ok);
        if (!AccessoryCurrent(owner)) return;
        if (!ok) { AccessoryBlock(owner, "frameCount unavailable"); return; }
        if (frame == owner->frame) return;
        owner->frame = frame; owner->frame_valid = false; owner->matched_frame = false;
        for (auto& bone : owner->bindings) { bone.planned = false; }
        // ReadFrame only assigns next/outgoing fields during a transition.
        owner->snapshot = {};
        const bool read = ReadDashFrame(component, owner->snapshot, owner->hash);
        if (!AccessoryCurrent(owner)) return;
        owner->frame_valid = read;
        if (!read || !owner->snapshot.current_special || owner->snapshot.outgoing || !std::isfinite(owner->snapshot.time)) return;
        const bool same_handle = DashCurrentRequestHandle(component) == owner->handle;
        if (!AccessoryCurrent(owner) || !same_handle) return;
        const auto c = g_session.loop.c;
        if (!owner->entry) {
            size_t matches = 0; bool has_state = false;
            for (const auto& entry : owner->table->entries) {
                has_state |= entry.state_hash == owner->hash;
                if (entry.Matches(owner->hash, c.begin, c.target)) { owner->entry = &entry; ++matches; }
            }
            if (!has_state) { AccessoryBlock(owner, "no entry for current state"); return; }
            if (matches != 1) {
                char text[224];
                std::snprintf(text, sizeof(text), "window mismatch or ambiguous entries: state=%d begin=%.6f target=%.6f matches=%zu (tolerance <1e-4)",
                    owner->hash, c.begin, c.target, matches);
                AccessoryBlock(owner, text); return;
            }
        }
        if (!owner->entry->Matches(owner->hash, c.begin, c.target)) { AccessoryBlock(owner, "state/window changed"); return; }
        if (!owner->bound && !BindAccessory(owner)) { AccessoryBlock(owner, "incomplete Transform enumeration/binding"); return; }
        if (!AccessoryCurrent(owner)) return;
        if (!owner->root_lease || !owner->root_lease->Owns()) {
            AccessoryBlock(owner, "pose ownership lost"); return;
        }
        if (!pose_scope) pose_scope.emplace(owner->lease_context,owner->root_lease);
        if (!AccessoryPhase(owner->snapshot.time, owner->entry->begin, owner->entry->target, owner->phase)) return;
        bool animator_ok = true;
        const bool same_animator = DashObject(GetAnimator, component, animator_ok) == owner->hierarchy.animator;
        if (!AccessoryCurrent(owner)) return;
        if (!animator_ok || !same_animator || !DashObjectAlive(owner->hierarchy.animator) || !DashObjectAlive(owner->root)) {
            AccessoryBlock(owner, "Animator/root changed or destroyed"); return;
        }
        const bool same_root = DashObject(PoseComponentTransform, owner->hierarchy.animator, animator_ok) == owner->root;
        if (!AccessoryCurrent(owner)) return;
        if (!animator_ok || !same_root) { AccessoryBlock(owner, "root changed"); return; }
        owner->matched_frame = true;
        const bool cover = g_session.rewound && g_session.animation_replays > 0;
        for (auto& bone : owner->bindings) {
            if (bone.failed) continue;
            bool child_ok = true; void* args[]{owner->root};
            const bool alive = DashObjectAlive(bone.transform);
            if (!AccessoryCurrent(owner)) return;
            const bool child = alive && DashValue<bool>(PoseIsChild, bone.transform, child_ok, args);
            if (!AccessoryCurrent(owner)) return;
            if (!child_ok || !child || (cover && !owner->entry->Sample(bone.sample, owner->phase, bone.output))) {
                bone.failed = true;
                ++owner->bone_failures;
                if (owner->config->diagnostics && owner->bone_messages++ < 4)
                    Log(("Sustained dash Jsspsi accessory: bone disabled: " + owner->entry->bones[bone.sample].name).c_str());
                continue;
            }
            bone.planned = cover;
        }
    } catch (...) {
        if (AccessoryCurrent(owner)) FailDashFeatures();
    }
}
bool AccessoryFrameSnapshot(void* component, Frame& frame, int& hash) {
    auto owner = AccessoryOwnerSnapshot();
    if (!owner || owner->component != component || !AccessoryCurrent(owner) || owner->blocked || !owner->frame_valid) return false;
    frame = owner->snapshot; hash = owner->hash; return true;
}
bool AccessoryDefersSeam(void* component) {
    // Only a usable per-bone reservation may defer the seam's cloth-time pass to TailLate.
    // In particular, missing/ambiguous entries and mask=0 keep the seam's cloth-time pass.
    AccessoryPrepareFrame(component);
    auto owner = AccessoryOwnerSnapshot();
    if (!AccessoryCurrent(owner) || owner->component != component || owner->blocked ||
        !owner->matched_frame || !owner->bound || !owner->root_lease || !owner->root_lease->Owns()) return false;
    const bool reserved = std::any_of(owner->bindings.begin(), owner->bindings.end(),
        [](const auto& bone) { return !bone.failed; });
    if (reserved) {
        // The animation can advance between cloth and TailLate in the same frame.
        // TailLate must sample again rather than apply the early phase snapshot.
        owner->frame = -1; owner->frame_valid = owner->matched_frame = false;
        for (auto& bone : owner->bindings) bone.planned = false;
    }
    return reserved;
}
std::unordered_set<void*> AccessoryClaimsSnapshot(void* component) {
    std::unordered_set<void*> claims;
    if (!g_accessory_live.load(std::memory_order_relaxed)) return claims;
    auto owner = AccessoryOwnerSnapshot();
    if (!owner || component != owner->component || !AccessoryCurrent(owner) || owner->blocked ||
        !owner->matched_frame || !owner->bound || !owner->root_lease || !owner->root_lease->Owns()) return claims;
    claims.reserve(owner->bindings.size());
    for (const auto& bone : owner->bindings) if (!bone.failed) claims.insert(bone.transform);
    return claims;
}
void AccessoryApplyFrame(void* component) {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    auto owner = AccessoryOwnerSnapshot();
    if (!owner || component != owner->component || !AccessoryCurrent(owner) || !g_session.rewound || !g_session.animation_replays ||
        owner->blocked || !owner->frame_valid || owner->applied_frame == owner->frame) return;
    PoseCallScope pose_scope(owner->lease_context, owner->root_lease);
    DashCallScope frame_calls([&] { return AccessoryCurrent(owner); });
    owner->applied_frame = owner->frame;
    if (std::none_of(owner->bindings.begin(), owner->bindings.end(), [](const auto& bone) { return bone.planned; })) return;
    // Seam/native calls between preparation and application can replace or
    // destroy objects without changing this module's session fields immediately.
    bool ok = true;
    bool hierarchy_current = DashCurrentRequestHandle(component) == owner->handle;
    if (!AccessoryCurrent(owner)) return;
    if (hierarchy_current) hierarchy_current = DashObjectAlive(owner->hierarchy.animator);
    if (!AccessoryCurrent(owner)) return;
    if (hierarchy_current) hierarchy_current = DashObjectAlive(owner->root);
    if (!AccessoryCurrent(owner)) return;
    if (hierarchy_current) hierarchy_current = DashObject(GetAnimator, component, ok) == owner->hierarchy.animator;
    if (!AccessoryCurrent(owner)) return;
    if (hierarchy_current) hierarchy_current = DashObject(PoseComponentTransform, owner->hierarchy.animator, ok) == owner->root;
    if (!AccessoryCurrent(owner)) return;
    if (!ok || !hierarchy_current || !owner->root_lease || !owner->root_lease->Owns()) {
        AccessoryBlock(owner, "owner/hierarchy/pose lease changed before write"); return;
    }
    size_t applied = 0;
    for (auto& bone : owner->bindings) {
        if (!bone.planned) continue;
        if (!AccessoryCurrent(owner)) return;
        const bool alive = DashObjectAlive(bone.transform);
        if (!AccessoryCurrent(owner)) return;
        bool child_ok = true; void* args[]{owner->root};
        const bool child = alive && DashValue<bool>(PoseIsChild, bone.transform, child_ok, args);
        if (!AccessoryCurrent(owner)) return;
        if (!owner->root_lease || !owner->root_lease->Owns()) {
            AccessoryBlock(owner, "pose lease changed during bone validation"); return;
        }
        const bool wrote = child_ok && child && PoseFinite(bone.output) && pose_scope.Owns() && WriteDashPose(bone.transform, bone.output);
        if (!AccessoryCurrent(owner)) return;
        if (wrote) { ++applied; bone.held = bone.output; bone.held_valid = true; }
        else {
            bone.failed = true; bone.planned = false;
            ++owner->bone_failures;
            if (!owner->config->diagnostics || owner->bone_messages++ >= 4) continue;
            char text[320];
            std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: write failed: %s", owner->entry->bones[bone.sample].name.c_str()); Log(text);
        }
    }
    if (!applied) return;
    ++owner->frames;
    if (!owner->logged && owner->config->diagnostics) {
        owner->logged = true;
        char text[256];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: first apply state=%d u=%.6f phi=%.6f bound=%zu skipped=%zu written=%zu frame=%d",
            owner->hash, owner->snapshot.time, owner->phase, owner->bindings.size(), owner->skipped, applied, owner->frame); Log(text);
    }
}

bool AccessoryFadeCurrent(const std::shared_ptr<AccessoryOwner>& owner) {
    if (!owner || g_stopping || !owner->lease_context || !owner->lease_context->valid || GetCurrentThreadId() != g_game_thread.load(std::memory_order_relaxed) ||
        owner != AccessoryOwnerSnapshot() || !owner->fading || g_session.component) return false;
    const auto config = g_config.load();
    return config->enabled && config->jsspsi_accessory_loop && config->jsspsi_accessory_fade > 0 && !config->jsspsi_capture && !CaptureActive() &&
        (config->characters & (1u << (owner->profile - kCharacters))) && owner->root_lease && owner->root_lease->Owns();
}
bool AccessoryFadeHierarchy(const std::shared_ptr<AccessoryOwner>& owner) {
    // CharacterAnimationComponent is a managed game object, not a UnityEngine.Object.
    // Its pin + ReleaseDetour protect the wrapper; EntityValid/IsMain protect ownership.
    if (!AccessoryFadeCurrent(owner)) return false;
    bool ok = true;
    const bool animator_alive = DashObjectAlive(owner->hierarchy.animator);
    if (!AccessoryFadeCurrent(owner) || !animator_alive) return false;
    const bool root_alive = DashObjectAlive(owner->root);
    if (!AccessoryFadeCurrent(owner) || !root_alive) return false;
    const bool same_animator = DashObject(GetAnimator, owner->component, ok) == owner->hierarchy.animator;
    if (!AccessoryFadeCurrent(owner) || !ok || !same_animator) return false;
    const bool same_root = DashObject(PoseComponentTransform, owner->hierarchy.animator, ok) == owner->root;
    if (!AccessoryFadeCurrent(owner) || !ok || !same_root) return false;
    const bool same_entity = DashObject(GetEntity, owner->component, ok) == owner->entity;
    if (!AccessoryFadeCurrent(owner) || !ok || !same_entity) return false;
    const bool valid = DashValue<bool>(EntityValid, owner->entity, ok);
    if (!AccessoryFadeCurrent(owner) || !ok || !valid) return false;
    void* control = DashObject(GetCharCtrl, owner->entity, ok);
    if (!AccessoryFadeCurrent(owner) || !ok || !control) return false;
    const bool main = DashValue<bool>(IsMain, control, ok);
    return AccessoryFadeCurrent(owner) && ok && main;
}
void AccessoryFadeFrame(void* component) {
    if (!g_accessory_live.load(std::memory_order_relaxed)) return;
    auto owner = AccessoryOwnerSnapshot();
    if (!owner || GetCurrentThreadId() != g_game_thread.load(std::memory_order_relaxed)) return;
    if (!owner->fading || owner->fade_in_progress) return;
    PoseCallScope pose_scope(owner->lease_context, owner->root_lease, true);
    DashCallScope frame_calls([&] { return AccessoryFadeCurrent(owner); });
    // Guard before the first managed getter: frameCount deduplication happens
    // too late to prevent recursive hierarchy/frameCount callbacks.
    owner->fade_in_progress = true;
    struct FadeGuard { bool& active; ~FadeGuard() { active = false; } } guard{owner->fade_in_progress};
    auto stop = [&](const char* reason) {
        // A callback may already have started a different owner. Never cancel it.
        if (owner == AccessoryOwnerSnapshot()) EndAccessory(reason);
    };
    if (!AccessoryFadeCurrent(owner)) { stop("fade owner/config/lease changed"); return; }
    double now = 0;
    if (!AccessoryNow(now) || now < owner->fade_start) { stop("fade clock failed"); return; }
    const double elapsed = now - owner->fade_start;
    if (elapsed >= owner->fade_seconds) { stop("fade complete"); return; }
    if (component != owner->component) return;
    if (!AccessoryFadeHierarchy(owner)) { stop("fade hierarchy/character lost"); return; }
    bool ok = true;
    const int frame = DashUnbox<int>(DashInvoke(PoseFrameCount, nullptr, nullptr, ok), ok);
    if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
    if (!ok) { stop("fade frameCount unavailable"); return; }
    if (frame == owner->applied_frame) return;
    owner->applied_frame = frame;
    const float x = static_cast<float>(std::clamp(1.0 - elapsed / owner->fade_seconds, 0.0, 1.0));
    const float weight = x * x * (3 - 2 * x);
    size_t written = 0;
    for (const auto& bone : owner->bindings) {
        if (!bone.held_valid || bone.failed) continue;
        const bool alive = DashObjectAlive(bone.transform);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        if (!alive) { stop("fade bone destroyed"); return; }
        void* args[]{owner->root};
        const bool child = DashValue<bool>(PoseIsChild, bone.transform, ok, args);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        if (!ok || !child) { stop("fade bone left root"); return; }
        BonePose native;
        const bool read = ReadDashPose(bone.transform, native);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        if (!read) { stop("fade native read failed"); return; }
        // Native getter callbacks can replace the Animator/root without changing
        // this module's owner. Recheck before the setter, including this bone.
        if (!AccessoryFadeHierarchy(owner)) { stop("fade hierarchy changed before write"); return; }
        const bool still_alive = DashObjectAlive(bone.transform);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        const bool still_child = still_alive && DashValue<bool>(PoseIsChild, bone.transform, ok, args);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        if (!ok || !still_child) { stop("fade bone invalid before write"); return; }
        const BonePose output{PoseSlerp(native.rotation, bone.held.rotation, weight),
            {std::lerp(native.position.x, bone.held.position.x, weight),
             std::lerp(native.position.y, bone.held.position.y, weight),
             std::lerp(native.position.z, bone.held.position.z, weight)}};
        const bool wrote = PoseFinite(output) && pose_scope.Owns() && WriteDashPose(bone.transform, output);
        if (!AccessoryFadeCurrent(owner)) { stop("fade owner changed"); return; }
        if (!wrote) { stop("fade write failed"); return; }
        ++written;
    }
    if (!written) { stop("fade has no held bones"); return; }
    if (!owner->fade_logged && owner->config->diagnostics) {
        owner->fade_logged = true;
        char text[192];
        std::snprintf(text, sizeof(text), "Sustained dash Jsspsi accessory: fade first apply elapsed=%.6f weight=%.6f written=%zu frame=%d",
            elapsed, weight, written, frame); Log(text);
    }
}
