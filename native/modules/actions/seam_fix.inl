// BESEAM03: magic[8], u32 entries; each entry contains hash/target/begin,
// u32 bone count, then u16 name length + UTF-8 name + quaternion dq + position dp.
// Record learns the native target-to-begin difference on the first traversal.
// Apply splits that difference across the loop endpoints; Idle leaves native pose.
// objects[0] is the rider, [1] the mount (rider window), [2..3] side companions.
// Immutable tables and local state snapshots survive managed callback reentry.
constexpr uint64_t kSeamBindMillis = 1500; // Allow delayed companion creation.
constexpr float kSeamRewindThreshold = .02f; // Ignore small progress jitter.
constexpr float kSeamRecordTolerance = .03f; // Late target samples cannot learn the seam.
using SeamTable = std::vector<SeamEntry>;
std::shared_ptr<const SeamTable> g_seam_table = std::make_shared<SeamTable>();
std::filesystem::file_time_type g_seam_stamp{};
bool g_seam_loaded = false;
PoseQuaternion SeamMul(PoseQuaternion a, PoseQuaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
PoseQuaternion SeamConj(PoseQuaternion q) { return {-q.x,-q.y,-q.z,q.w}; }
std::filesystem::path SeamFile() {
    return g_session.profile && g_session.profile->seam_file ? ActionsDataFile(g_session.profile->seam_file) : std::filesystem::path{};
}
void LoadSeamTable() {
    const auto file = SeamFile();
    if (file.empty()) return;
    std::error_code error; const auto stamp = std::filesystem::last_write_time(file,error);
    if (g_seam_loaded && !error && stamp == g_seam_stamp) return;
    g_seam_loaded = false;
    auto table = std::make_shared<SeamTable>();
    std::ifstream in(file,std::ios::binary);
    const bool loaded = in && LoadSeamEntries(in,*table);
    g_seam_table = table;
    if (loaded && !error) { g_seam_stamp = stamp; g_seam_loaded = true; }
    if (g_session.config->diagnostics) {
        char text[160]; std::snprintf(text,sizeof(text),"Sustained dash Jsspsi seam: table %s, entries=%zu",
            loaded ? "loaded" : "absent/rejected; relearn",table->size()); Log(text);
    }
}
// A failed flush/close never replaces the last valid cache.
bool SaveSeamTable() {
    const auto file = SeamFile(); if (file.empty()) return false;
    auto temp = file; temp += ".new";
    std::error_code error;
    try {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        auto write = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); };
        out.write("BESEAM03",8); write(static_cast<uint32_t>(g_seam_table->size()));
        for (const auto& entry : *g_seam_table) {
            write(entry.hash); write(entry.target); write(entry.begin); write(static_cast<uint32_t>(entry.bones.size()));
            for (const auto& [bone,delta] : entry.bones) {
                write(static_cast<uint16_t>(bone.size())); out.write(bone.data(),static_cast<std::streamsize>(bone.size()));
                write(delta.dq); write(delta.dp);
            }
        }
        out.flush(); const bool flushed = out.good(); out.close();
        if (!flushed || out.fail()) { std::filesystem::remove(temp,error); return false; }
        std::filesystem::rename(temp,file,error);
        if (error) { std::filesystem::remove(temp,error); return false; }
        g_seam_stamp = std::filesystem::last_write_time(file,error); g_seam_loaded = !error;
        return true;
    } catch (...) { std::filesystem::remove(temp,error); return false; }
}
int FindSeam(const SeamTable& table,int hash,float target,float begin) {
    for (size_t i=0;i<table.size();++i)
        if (table[i].hash==hash && std::fabs(table[i].target-target)<5e-4f && std::fabs(table[i].begin-begin)<5e-4f) return static_cast<int>(i);
    return -1;
}
// Generated helpers, funnels and the line control are outside the skeletal seam.
bool SeamBoneWanted(std::string_view name) {
    return !name.starts_with("GEN_") && !name.starts_with("Funnel") && name!="lineJoint";
}
enum class SeamMode { Idle, Record, Apply };
struct SeamObject : BoundHierarchy {
    std::shared_ptr<DashRootLease> lease;
    SeamMode mode = SeamMode::Idle;
    int entry=-1,hash=0,seen_hash=0;
    float target=0,begin=0,last_time=-1;
    bool recorded=false,logged=false,from_rewind=false;
};
struct SeamState {
    void* handle=nullptr;
    std::shared_ptr<DashPoseContext> lease_context;
    SeamObject objects[4];
    uint64_t bind_deadline=0;
    int done_frame=-1;
};
// Explicit shutdown/cancel owns cleanup; process teardown must not call Host.
auto& g_seam = *new std::shared_ptr<SeamState>;
void ReleaseSeam() { g_seam.reset(); }
bool BindSeamObject(SeamObject& object,void* animator,int index,const std::shared_ptr<DashPoseContext>& lease_context) {
    bool ok=true; void* root=DashObject(PoseComponentTransform,animator,ok);
    if (!ok || lease_context!=g_dash_pose) return false;
    object.lease=AcquireDashRoot(lease_context,index,root);
    if (!object.lease) return false;
    PoseCallScope scope(lease_context,object.lease);
    return EnumerateHierarchy(object,animator,root,[](const char* name) { return SeamBoneWanted(name); });
}
float SeamRewindBlend(int index,float blend) {
    std::lock_guard<std::recursive_mutex> lock(g_feature_mutex);
    if (!g_seam || !g_session.config || !g_session.config->jsspsi_seam_fix || g_session.handle!=g_seam->handle) return blend;
    const auto& object=g_seam->objects[index];
    return object.bound && object.lease && object.lease->Owns() && object.mode==SeamMode::Apply ? 0.0f : blend;
}
// These checks are deliberately repeated after a getter: the lease token alone
// does not prove that a reparented or destroyed bone still belongs to its root.
bool SeamHierarchyCurrent(const SeamObject& object, const BoundBone* bone = nullptr) {
    if (!DashCallCurrent() || !DashObjectAlive(object.animator) || !DashObjectAlive(object.root)) return false;
    bool ok = true;
    if (DashObject(PoseComponentTransform, object.animator, ok) != object.root || !ok) return false;
    if (!bone) return DashCallCurrent();
    if (!DashObjectAlive(bone->transform)) return false;
    void* args[]{object.root};
    return DashValue<bool>(PoseIsChild, bone->transform, ok, args) && ok && DashCallCurrent();
}
bool SeamRead(BoundBone& bone,BonePose& out) {
    BonePose current;
    if (!ReadDashPose(bone.transform,current)) return false;
    bone.baseline=PoseNativeBaseline(current,bone.last,bone.baseline,bone.wrote); out=bone.baseline;
    return CurrentPoseCallOwned();
}
void SeamFixFrame(void* component,bool early) {
    if (!g_hierarchy_contract || !DashFeaturesActive() || component!=g_session.component || !g_session.profile->seam_file ||
        !g_session.config->jsspsi_seam_fix || g_session.config->jsspsi_capture || !g_dash_pose) return;
    const auto context = g_dash_pose;
    const auto config = g_session.config;
    const auto handle = g_session.handle;
    const auto profile = g_session.profile;
    auto session_current = [&] {
        return !g_stopping && context && context->valid && context == g_dash_pose &&
            component == g_session.component && handle == g_session.handle &&
            config == g_session.config && config == g_config.load() && profile == g_session.profile;
    };
    DashCallScope session_calls(session_current);
    const bool deferred = early && config->jsspsi_accessory_loop && AccessoryDefersSeam(component);
    if (!session_current() || deferred) return;
    if (!g_seam || g_seam->handle!=g_session.handle) {
        auto state=std::make_shared<SeamState>(); state->handle=g_session.handle; state->lease_context=g_dash_pose;
        state->bind_deadline=GetTickCount64()+kSeamBindMillis; g_seam=std::move(state); LoadSeamTable();
    }
    const auto state=g_seam;
    auto current = [&] { return session_current() && state && state == g_seam && state->lease_context == context; };
    DashCallScope frame_calls(current);
    bool ok=true; const int frame=DashUnbox<int>(DashInvoke(PoseFrameCount,nullptr,nullptr,ok),ok);
    if (!current() || (ok && state->done_frame==frame)) return;
    if (ok) state->done_frame=frame;
    if (GetTickCount64()<state->bind_deadline) {
        if (!state->objects[0].bound) {
            void* animator=DashObject(GetAnimator,component,ok);
            if (!current()) return;
            if (ok) BindSeamObject(state->objects[0],animator,0,state->lease_context);
            if (!current()) return;
        }
        for (int slot=0;slot<3;++slot) {
            if (!current()) return;
            if (!state->objects[slot+1].bound) {
                void* animator=TmpObjectAnimator(slot);
                if (!current()) return;
                if (animator) BindSeamObject(state->objects[slot+1],animator,slot+1,state->lease_context);
                if (!current()) return;
            }
        }
    }
    if (!current()) return;
    // Snapshot accessory reservations once for this seam frame.
    const auto claims=AccessoryClaimsSnapshot(component);
    const LoopConstants rider=g_session.loop.c;
    const LoopConstants side=profile->side_loop ? *profile->side_loop : rider;
    for (int index=0;index<4;++index) {
        auto& object=state->objects[index];
        if (!current()) return;
        if (!object.bound || !object.lease) continue;
        PoseCallScope scope(state->lease_context,object.lease);
        if (!SeamHierarchyCurrent(object)) continue;
        float time=0; int hash=0; bool transitioning=false;
        if (index==0) {
            Frame f{};
            if (!AccessoryFrameSnapshot(component,f,hash) && !ReadDashFrame(component,f,hash)) continue;
            time=f.time; transitioning=f.transitioning;
        } else {
            bool read_ok=true; int layer=0; void* args[]{&layer};
            transitioning=DashValue<bool>(InTransition,object.animator,read_ok,args);
            void* boxed=DashInvoke(CurrentState,object.animator,args,read_ok);
            void* native=DashUnboxObject(boxed, read_ok);
            hash=DashValue<int>(StateHash,native,read_ok); time=DashValue<float>(StateTime,native,read_ok);
            if (!read_ok) continue;
        }
        if (!DashCallCurrent()) return;
        if (!std::isfinite(time)) continue;
        const auto& c=index<=1 ? rider : side;
        if (index==0 && hash!=g_hashes[0] && hash!=g_hashes[1]) transitioning=true;
        if (transitioning || hash!=object.seen_hash) {
            object.seen_hash=hash; object.last_time=-1;
            if (transitioning) continue;
        }
        const bool first=object.last_time<c.target && time>=c.target;
        const bool rewound=object.last_time>=0 && time<object.last_time-kSeamRewindThreshold;
        object.last_time=time;
        const auto table=g_seam_table;
        if (first || rewound) {
            object.hash=hash; object.target=c.target; object.begin=c.begin;
            object.recorded=false; object.from_rewind=rewound;
            object.entry=FindSeam(*table,hash,c.target,c.begin);
            object.mode=object.entry>=0 ? SeamMode::Apply : time<=c.target+kSeamRecordTolerance ? SeamMode::Record : SeamMode::Idle;
            if (object.mode==SeamMode::Record) for (auto& bone:object.bones)
                if (!SeamRead(bone,bone.first)) { object.mode=SeamMode::Idle; break; }
            if (!DashCallCurrent()) return;
            if (!object.logged && g_session.config->diagnostics) {
                object.logged=true; char text[180];
                std::snprintf(text,sizeof(text),"Sustained dash Jsspsi seam: object %d state=%d window %.3f-%.3f -> %s",index,
                    hash,c.target,c.begin,object.mode==SeamMode::Apply ? "apply" : object.mode==SeamMode::Record ? "record" : "idle"); Log(text);
            }
        }
        if (hash!=object.hash) { object.mode=SeamMode::Idle; continue; }
        if (object.mode==SeamMode::Record && !object.recorded && time>=object.begin) {
            object.recorded=true; SeamEntry entry; entry.hash=hash; entry.target=object.target; entry.begin=object.begin;
            bool valid=true;
            for (auto& bone:object.bones) {
                BonePose end;
                if (!SeamRead(bone,end)) { valid=false; break; }
                PoseQuaternion dq=PoseNormalize(SeamMul(bone.first.rotation,SeamConj(end.rotation)));
                if (dq.w<0) dq={-dq.x,-dq.y,-dq.z,-dq.w};
                const PoseVector dp{bone.first.position.x-end.position.x,bone.first.position.y-end.position.y,bone.first.position.z-end.position.z};
                if (!PoseFinite({dq,dp})) { valid=false; break; }
                entry.bones[bone.name]={dq,dp};
            }
            if (!DashCallCurrent()) return;
            if (valid) {
                auto next=std::make_shared<SeamTable>(*g_seam_table); next->push_back(std::move(entry)); g_seam_table=std::move(next);
                const bool saved=SaveSeamTable();
                if (g_session.config->diagnostics) Log(saved ? "Sustained dash Jsspsi seam: recorded and saved." : "Sustained dash Jsspsi seam: recorded; cache save failed, old file retained.");
            }
            continue;
        }
        if (object.mode!=SeamMode::Apply || object.entry<0 || size_t(object.entry)>=table->size()) continue;
        const auto& deltas=(*table)[object.entry].bones;
        const float phase=std::clamp((time-object.target)/std::max(object.begin-object.target,1e-3f),0.f,1.f);
        const float share=SeamShare(phase,object.from_rewind);
        for (auto& bone:object.bones) {
            if (share==0 && !bone.wrote) continue;
            if (index==0 && claims.contains(bone.transform)) { bone.wrote=false; continue; }
            const auto found=deltas.find(bone.name); if (found==deltas.end()) continue;
            const auto& delta=found->second;
            if (std::fabs(delta.dq.w)>.999999f && delta.dp.x*delta.dp.x+delta.dp.y*delta.dp.y+delta.dp.z*delta.dp.z<1e-8f) continue;
            if (!SeamHierarchyCurrent(object, &bone)) { bone.wrote=false; continue; }
            BonePose native;
            if (!SeamRead(bone,native) || !DashCallCurrent()) return;
            BonePose output=native;
            if (share!=0) {
                const auto turn=PoseSlerp(PoseQuaternion{},delta.dq,std::fabs(share));
                output.rotation=PoseNormalize(SeamMul(share>0 ? turn : SeamConj(turn),native.rotation));
                output.position={native.position.x+delta.dp.x*share,native.position.y+delta.dp.y*share,native.position.z+delta.dp.z*share};
            }
            if (!PoseFinite(output) || !SeamHierarchyCurrent(object, &bone)) continue;
            const bool wrote=WriteDashPose(bone.transform,output);
            if (!DashCallCurrent()) return;
            if (wrote) { bone.last=output; bone.wrote=share!=0; }
        }
    }
}
using SeamInstanceFn=void(__fastcall*)(void*,const void*);
SeamInstanceFn g_cloth_update=nullptr;
void __fastcall ClothUpdateDetour(void* self,const void* method) {
    if (DashFeaturesActive()) GuardDashFeatures([&] { SeamFixFrame(g_session.component,true); });
    g_cloth_update(self,method);
}
