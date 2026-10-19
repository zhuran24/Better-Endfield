// Pose helpers for optional writers. v12 uses the unmodified upstream helpers.
bool ReadDashPose(void* transform, BonePose& value) {
    bool ok = true;
    void* args[]{&value.position, &value.rotation};
    DashInvoke(PoseGetLocal, transform, args, ok);
    if (!ok || !PoseFinite(value)) return false;
    value.rotation = PoseNormalize(value.rotation);
    return true;
}
bool WriteDashPose(void* transform, const BonePose& value) {
    if (!DashCallCurrent() || !PoseFinite(value)) return false;
    bool ok = true;
    BonePose copy = value;
    void* args[]{&copy.position, &copy.rotation};
    DashInvoke(PoseSetLocal, transform, args, ok);
    return ok;
}
// Pinned Transform enumeration shared by the two optional visual writers.
struct ManagedPin {
    uint32_t value = 0;
    explicit ManagedPin(void* object) : value(object ? g_host->gchandle_new(g_host->context, object, 1) : 0) {}
    ~ManagedPin() { if (value) g_host->gchandle_free(g_host->context, value); }
    uint32_t Take() { return std::exchange(value, 0); }
    ManagedPin(const ManagedPin&) = delete;
};
struct BoundBone {
    void* transform = nullptr;
    uint32_t pin = 0;
    std::string name;
    BonePose first{}, baseline{}, last{};
    bool wrote = false;
};
struct BoundHierarchy {
    void* animator = nullptr;
    void* root = nullptr;
    uint32_t animator_pin = 0;
    bool bound = false;
    std::vector<BoundBone> bones;
    BoundHierarchy() = default;
    BoundHierarchy(const BoundHierarchy&) = delete;
    BoundHierarchy& operator=(const BoundHierarchy&) = delete;
    ~BoundHierarchy() { Clear(); }
    void Clear() noexcept {
        for (auto& bone : bones) if (bone.pin) g_host->gchandle_free(g_host->context, std::exchange(bone.pin, 0));
        if (animator_pin) g_host->gchandle_free(g_host->context, std::exchange(animator_pin, 0));
        bones.clear(); animator = root = nullptr; bound = false;
    }
    void Swap(BoundHierarchy& other) noexcept {
        std::swap(animator, other.animator); std::swap(root, other.root);
        std::swap(animator_pin, other.animator_pin); std::swap(bound, other.bound); bones.swap(other.bones);
    }
};
template<class Accept> bool EnumerateHierarchy(BoundHierarchy& output, void* animator, void* expected_root, Accept accept) {
    if (!animator || !g_methods[ComponentGameObject].resolved.method_info || !g_transform_type || !DashObjectAlive(animator)) return false;
    bool ok = DashCallCurrent();
    void* root = DashObject(PoseComponentTransform, animator, ok);
    if (!ok || root != expected_root) return false;
    void* go = DashInvoke(ComponentGameObject, root, nullptr, ok);
    bool inactive = true; void* args[]{g_transform_type, &inactive};
    void* array = ok && go ? DashInvoke(ComponentsInChildren, go, args, ok) : nullptr;
    const int count = DashValue<int>(ArrayLength, array, ok);
    if (!ok || count < 1 || count > 4096) return false;
    BoundHierarchy next;
    for (int i = 0; i < count; ++i) {
        void* item_args[]{&i};
        void* transform = DashInvoke(ArrayItem, array, item_args, ok);
        char name[1024]{};
        void* managed = transform ? DashInvoke(ClipName, transform, nullptr, ok) : nullptr;
        if (!ok || !managed || g_host->copy_managed_string(g_host->context, managed, name, sizeof(name)) <= 0 ||
            !DashCallCurrent()) return false;
        name[sizeof(name)-1] = '\0';
        if (!accept(name)) continue;
        ManagedPin pin(transform);
        if (!pin.value || !DashCallCurrent()) return false;
        BoundBone bone; bone.transform = transform; bone.name = name;
        next.bones.push_back(std::move(bone));
        next.bones.back().pin = pin.Take();
    }
    if (next.bones.empty()) return false;
    ManagedPin pin(animator);
    if (!pin.value || !DashCallCurrent()) return false;
    next.animator = animator; next.root = root; next.animator_pin = pin.Take(); next.bound = true;
    output.Swap(next);
    return true;
}
bool BindAccessoryHierarchy(BoundHierarchy& output, void* animator, void* root) {
    return EnumerateHierarchy(output, animator, root, [](const char*) { return true; });
}
