// Game-thread hooks own these visual states. Worker Shutdown takes the same
// recursive feature lock; managed calls may reenter hooks on the game thread.
// g_feature_mutex protects g_dash_pose, g_seam, g_accessory and capture state.
// Local shared_ptr snapshots keep retired objects alive until the callback returns.
// Holding the context through accessory fade also holds the companion roots.
constexpr int kDashRootCount = 4; // 0 rider, 1 mount, 2-3 side companions.
const BE_PoseLeaseApiV1* g_pose_leases = nullptr;
std::atomic_bool g_pose_lease_ready{false};
std::recursive_mutex g_feature_mutex;
struct DashRootLease {
    void* root = nullptr;
    uint32_t pin = 0;
    uint64_t token = 0;
    const BE_PoseLeaseApiV1* api = nullptr;
    ~DashRootLease() {
        ReleaseToken();
        if (pin && g_host) g_host->gchandle_free(g_host->context, pin);
    }
    void ReleaseToken() noexcept { if (token && api) api->release(root, kId, std::exchange(token, 0)); }
    bool Owns() const { return token && api && api->owns(root, kId, token); }
};
struct DashPoseContext {
    void* component = nullptr;
    void* handle = nullptr;
    bool valid = true;
    bool diagnostics = false;
    std::shared_ptr<DashRootLease> roots[kDashRootCount];
    bool attempted[kDashRootCount]{};
    void End() noexcept {
        valid=false;
        for (const auto& root:roots) if (root) root->ReleaseToken();
    }
    ~DashPoseContext() { End(); }
};
// Explicit shutdown/cancel owns cleanup; process teardown must not call Host.
auto& g_dash_pose = *new std::shared_ptr<DashPoseContext>;
std::shared_ptr<DashRootLease> AcquireDashRoot(const std::shared_ptr<DashPoseContext>& context, int index, void* root) {
    if (!context || !context->valid || g_stopping || !root || index < 0 || index >= kDashRootCount) return {};
    if (context->attempted[index]) {
        auto lease = context->roots[index];
        return lease && lease->root == root && lease->Owns() ? lease : nullptr;
    }
    context->attempted[index] = true;
    for (const auto& lease : context->roots) {
        if (lease && lease->root == root) { context->roots[index] = lease; return lease->Owns() ? lease : nullptr; }
    }
    auto lease = std::make_shared<DashRootLease>();
    lease->root = root; lease->api = g_pose_leases;
    if (lease->api) {
        lease->pin = g_host->gchandle_new(g_host->context, root, 1);
        if (lease->pin && context->valid) lease->token = lease->api->acquire(root, kId);
    }
    if (!context->valid || !lease->Owns()) {
        if (context->diagnostics) {
            char text[160];
            std::snprintf(text, sizeof(text), "Sustained dash Jsspsi pose: root %d unavailable; no bone writes for this dash.", index);
            Log(text);
        }
        return {};
    }
    context->roots[index] = lease;
    return lease;
}
struct PoseCallScope;
thread_local const PoseCallScope* g_pose_call_scope = nullptr;
struct PoseCallScope {
    const PoseCallScope* previous = g_pose_call_scope;
    std::shared_ptr<DashPoseContext> context;
    std::shared_ptr<DashRootLease> root;
    bool fading;
    PoseCallScope(std::shared_ptr<DashPoseContext> c, std::shared_ptr<DashRootLease> r, bool fade = false)
        : context(std::move(c)), root(std::move(r)), fading(fade) { g_pose_call_scope = this; }
    ~PoseCallScope() { g_pose_call_scope = previous; }
    bool Owns() const {
        return !g_stopping && context && context->valid && root && root->Owns() &&
            (fading || context == g_dash_pose);
    }
};
bool CurrentPoseCallOwned() { return !g_pose_call_scope || g_pose_call_scope->Owns(); }
