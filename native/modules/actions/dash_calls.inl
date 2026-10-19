// Retain the original identity across managed playback/audio callbacks. A new
// dash can reuse component/handle pointers, so compare its lease context too.
struct DashSessionSnapshot {
    void* component = g_session.component;
    void* handle = g_session.handle;
    std::shared_ptr<const Configuration> config = g_session.config;
    std::shared_ptr<DashPoseContext> context = g_dash_pose;
    bool Current() const {
        return !g_stopping && !g_session.features_failed && component && config &&
            component == g_session.component && handle == g_session.handle &&
            config == g_session.config && config == g_config.load() && context == g_dash_pose &&
            (!context || context->valid);
    }
};
// Jsspsi-only managed boundaries. Shared upstream helpers deliberately know
// nothing about pose leases. A frame supplies its identity predicate here;
// every managed return is checked before its result can be used by the next call.
struct DashCallScope;
thread_local const DashCallScope* g_dash_call_scope = nullptr;
struct DashCallScope {
    const DashCallScope* previous = g_dash_call_scope;
    std::function<bool()> current;
    explicit DashCallScope(std::function<bool()> check) : current(std::move(check)) { g_dash_call_scope = this; }
    ~DashCallScope() { g_dash_call_scope = previous; }
};
bool DashCallCurrent() {
    return !g_stopping && CurrentPoseCallOwned() && (!g_dash_call_scope || g_dash_call_scope->current());
}
// A Start reentered from an old getter establishes a new session, not an old
// writer's continuation. Restoring the scope afterwards lets the old frame fail.
struct SuspendDashCalls {
    const PoseCallScope* pose = std::exchange(g_pose_call_scope, nullptr);
    const DashCallScope* call = std::exchange(g_dash_call_scope, nullptr);
    ~SuspendDashCalls() { g_pose_call_scope = pose; g_dash_call_scope = call; }
};
void* DashInvoke(MethodId id, void* instance, void** args, bool& ok) {
    if (!ok || !DashCallCurrent() || !g_methods[id].resolved.method_info) { ok = false; return nullptr; }
    void* result = Invoke(id, instance, args, ok);
    if (!DashCallCurrent()) ok = false;
    return ok ? result : nullptr;
}
void* DashObject(MethodId id, void* instance, bool& ok) {
    if (!instance) { ok = false; return nullptr; }
    void* result = DashInvoke(id, instance, nullptr, ok);
    if (!result) ok = false;
    return result;
}
void* DashUnboxObject(void* object, bool& ok) {
    if (!ok || !object || !DashCallCurrent()) { ok = false; return nullptr; }
    void* result = g_host->object_unbox(g_host->context, object);
    if (!result || !DashCallCurrent()) ok = false;
    return ok ? result : nullptr;
}
template<class T> T DashUnbox(void* object, bool& ok) {
    T result{};
    void* value = DashUnboxObject(object, ok);
    if (ok) std::memcpy(&result, value, sizeof(T));
    return result;
}
template<class T> T DashValue(MethodId id, void* instance, bool& ok, void** args = nullptr) {
    if (!instance) { ok = false; return {}; }
    return DashUnbox<T>(DashInvoke(id, instance, args, ok), ok);
}
void* OptionalField(FieldId id, void* instance) {
    if (!instance || !g_fields[id].resolved.field_info || !DashCallCurrent()) return nullptr;
    void* result = FieldObject(id, instance);
    return DashCallCurrent() ? result : nullptr;
}
void* DashCurrentRequestHandle(void* component) {
    void* request = OptionalField(Request, component);
    return request ? OptionalField(RequestHandle, request) : nullptr;
}
void* DashInvokeNamed(void* object, const char* name, int count, void** args, bool& ok) {
    if (!DashCallCurrent()) ok = false;
    void* result = InvokeNamed(object, name, count, args, ok);
    if (!DashCallCurrent()) ok = false;
    return ok ? result : nullptr;
}
bool DashObjectAlive(void* object) {
    if (!object) return false;
    bool ok = true;
    void* args[]{object};
    return DashUnbox<bool>(DashInvoke(UnityAlive, nullptr, args, ok), ok) && ok;
}
