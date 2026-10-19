// Native-dash recording keeps the whole clip, including its .85 exit.
// BECAP001: magic[8], u32 bones, u32 frames; each bone stores u8 group,
// u16 path bytes, UTF-8 relative path. Each frame stores f32 progress,
// f32 seconds since start, then seven f32 values per bone (quaternion, position).
struct CaptureBone {
    void* transform = nullptr;
    void* root = nullptr;
    void* animator = nullptr;
    uint32_t pin = 0;
    uint8_t group = 0;
    std::string path;
};
struct CaptureState {
    bool active = false, valid = true;
    void* component = nullptr;
    void* handle = nullptr;
    std::shared_ptr<const Configuration> config;
    std::vector<CaptureBone> bones;
    std::vector<float> samples;
    uint32_t frames = 0;
    uint64_t start = 0;
    uint64_t generation = 0;
    std::filesystem::path file;
    ~CaptureState() {
        for (const auto& bone : bones) if (bone.pin) g_host->gchandle_free(g_host->context, bone.pin);
    }
};
// Like the visual owners, only explicit lifecycle paths release managed pins.
auto& g_capture = *new std::shared_ptr<CaptureState>;
uint64_t g_capture_generation = 0;
bool CaptureActive() { return g_capture && g_capture->active; }
bool CaptureCurrent(const std::shared_ptr<CaptureState>& state) {
    return state && state->valid && !g_stopping && state->generation == g_capture_generation &&
        state->component == g_session.component && state->handle == g_session.handle &&
        state->config == g_session.config && state->config == g_config.load();
}
void DiscardCapture() noexcept {
    ++g_capture_generation;
    auto old = std::exchange(g_capture, {});
    if (old) old->valid = false;
}
// Walk only within the selected Animator root. A detached transform is not a path.
std::string CapturePath(void* transform, void* root) {
    std::string path;
    for (int depth = 0; transform && transform != root && depth < 64; ++depth) {
        bool ok = true;
        void* name = DashInvoke(ClipName, transform, nullptr, ok);
        char text[1024]{};
        if (!ok || !name || g_host->copy_managed_string(g_host->context, name, text, sizeof(text)) <= 0 || !DashCallCurrent()) return {};
        path = path.empty() ? text : std::string(text) + "/" + path;
        transform = DashObject(TransformParent, transform, ok);
        if (!ok) return {};
    }
    return transform == root ? path : std::string{};
}
bool CaptureBoneCurrent(const CaptureBone& bone) {
    if (!DashObjectAlive(bone.animator) || !DashObjectAlive(bone.root) || !DashObjectAlive(bone.transform)) return false;
    bool ok = true;
    if (DashObject(PoseComponentTransform, bone.animator, ok) != bone.root || !ok) return false;
    void* args[]{bone.root};
    return DashValue<bool>(PoseIsChild, bone.transform, ok, args) && ok;
}
void CaptureAddTree(const std::shared_ptr<CaptureState>& state, void* animator, void* root, uint8_t group) {
    if (!root || !g_transform_type) return;
    bool ok = true;
    void* go = DashObject(ComponentGameObject, root, ok);
    bool inactive = true;
    void* args[]{g_transform_type, &inactive};
    void* array = DashInvoke(ComponentsInChildren, go, args, ok);
    const int count = DashValue<int>(ArrayLength, array, ok);
    for (int i = 0; ok && i < count && i < 2048; ++i) {
        void* item_args[]{&i};
        void* transform = DashInvoke(ArrayItem, array, item_args, ok);
        if (!ok || !transform) return;
        CaptureBone bone;
        bone.transform = transform; bone.root = root; bone.animator = animator; bone.group = group;
        ManagedPin pin(transform);
        if (!CaptureCurrent(state)) return;
        if (!pin.value) continue;
        bone.path = transform == root ? "." : CapturePath(transform, root);
        if (!CaptureCurrent(state)) return;
        if (bone.path.empty() || !CaptureBoneCurrent(bone)) continue;
        state->bones.push_back(std::move(bone));
        state->bones.back().pin = pin.Take();
    }
}
void CaptureBegin(void* component, const CharacterProfile* profile) {
    DiscardCapture();
    if (!g_methods[ComponentGameObject].resolved.method_info || !g_methods[TransformParent].resolved.method_info || !g_transform_type) {
        Log("Sustained dash Jsspsi capture: Unity helpers unavailable; nothing recorded.");
        return;
    }
    auto state = std::make_shared<CaptureState>();
    state->component = component; state->handle = g_session.handle; state->config = g_session.config;
    state->generation = g_capture_generation;
    DashCallScope calls([&] { return CaptureCurrent(state); });
    state->file = ActionsDataFile(profile->capture_file);
    if (state->file.empty()) { Log("Sustained dash Jsspsi capture: data directory unavailable; nothing recorded."); return; }
    bool ok = true;
    void* animator = DashObject(GetAnimator, component, ok);
    void* root = DashObject(PoseComponentTransform, animator, ok);
    if (ok && root) CaptureAddTree(state, animator, root, 0);
    if (!CaptureCurrent(state)) return;
    // One dictionary lookup per recording; all four trees use the same binding path.
    void* dictionary = OptionalField(JsspsiTmpObjects, state->handle);
    for (int slot = 0; dictionary && slot < 3; ++slot) {
        bool slot_ok = true;
        void* beast = TmpObjectAnimator(slot, dictionary);
        void* beast_root = DashObject(PoseComponentTransform, beast, slot_ok);
        if (!CaptureCurrent(state)) return;
        if (slot_ok) CaptureAddTree(state, beast, beast_root, static_cast<uint8_t>(slot + 1));
        if (!CaptureCurrent(state)) return;
    }
    if (!CaptureCurrent(state)) return;
    state->start = GetTickCount64();
    state->active = !state->bones.empty();
    g_capture = state;
    char text[160];
    std::snprintf(text, sizeof(text), "Sustained dash Jsspsi capture: armed for %s: bones=%zu (loop disabled for this dash).", profile->codename, state->bones.size());
    Log(text);
}
void CaptureFrame(void* component) {
    const auto state = g_capture;
    if (!state || !state->active || component != state->component || state->frames >= 900) return;
    DashCallScope calls([&] { return CaptureCurrent(state) && state == g_capture; });
    Frame frame;
    int hash = 0;
    if (!ReadDashFrame(component, frame, hash) || !frame.current_special || !DashCallCurrent()) return;
    std::vector<float> sample{frame.time, (GetTickCount64() - state->start) / 1000.0f};
    for (const auto& bone : state->bones) {
        BonePose pose{};
        if (!CaptureBoneCurrent(bone) || !ReadDashPose(bone.transform, pose) || !CaptureBoneCurrent(bone)) pose = {};
        if (!DashCallCurrent()) return;
        const float values[]{pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w,
            pose.position.x, pose.position.y, pose.position.z};
        sample.insert(sample.end(), std::begin(values), std::end(values));
    }
    state->samples.insert(state->samples.end(), sample.begin(), sample.end());
    ++state->frames;
}
void CaptureEnd() {
    const auto state = g_capture;
    DiscardCapture();
    if (!state || !state->frames || state->file.empty()) return;
    std::ofstream out(state->file, std::ios::binary | std::ios::trunc);
    auto u32 = [&](uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
    out.write("BECAP001", 8);
    u32(static_cast<uint32_t>(state->bones.size())); u32(state->frames);
    for (const auto& bone : state->bones) {
        out.put(static_cast<char>(bone.group));
        uint16_t length = static_cast<uint16_t>(std::min<size_t>(bone.path.size(), 65535));
        out.write(reinterpret_cast<const char*>(&length), 2); out.write(bone.path.data(), length);
    }
    out.write(reinterpret_cast<const char*>(state->samples.data()), static_cast<std::streamsize>(state->samples.size() * sizeof(float)));
    out.flush(); const bool flushed = out.good(); out.close();
    char text[200];
    std::snprintf(text, sizeof(text), "Sustained dash Jsspsi capture: written: bones=%zu frames=%u ok=%d", state->bones.size(), state->frames, flushed && !out.fail() ? 1 : 0);
    Log(text);
}
