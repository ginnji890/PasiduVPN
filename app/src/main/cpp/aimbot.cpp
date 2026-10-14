// language: C++, target: Android (ARM64), Free Fire (il2cpp/Unity)
// compile: aarch64-linux-android-clang++ aim_ff.cpp -o aim_ff -fPIC -shared -ldl -llog
// detection surface: memory read patterns, rapid touch injection. mitigations noted inline.
// inject via hooking framework or run as background proc with mem r/w access

#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#define TAG "FF_AIM"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ============================================================
// OFFSETS — UPDATE AFTER EVERY GAME PATCH
// Run Il2CppDumper on the current libil2cpp.so + global-metadata.dat
// Search class/method names below, plug in current offsets
// ============================================================
namespace Offsets {
    // il2cpp base address (resolved at runtime)
    uintptr_t il2cpp_base = 0;

    // Entity/Player offsets — verify against current dump
    namespace Player {
        uintptr_t List_offset     = 0x1234567;  // GameManager -> EntityList (UPDATE)
        uintptr_t ListCount_offset = 0x1234570;  // EntityList count (UPDATE)
        uintptr_t Pos_offset      = 0x1A0;       // position (Vector3) within entity
        uintptr_t HP_offset       = 0x2B4;       // health (int) — skip dead
        uintptr_t Team_offset     = 0x2C0;       // team id — skip teammates
        uintptr_t BoneHead_offset = 0x4D8;       // head bone pos (Vector3)
        uintptr_t IsLocal_offset  = 0x2B0;       // is local player bool
    }

    namespace LocalPlayer {
        uintptr_t Pos_offset     = 0x1A0;
        uintptr_t View_offset    = 0x3C8;        // view angles (Vector2: pitch, yaw)
    }

    namespace Camera {
        uintptr_t Sens_offset    = 0x40;         // sensitivity scale
    }
}

// ============================================================
// MEMORY R/W — process_vm_readv / process_vm_writev
// Faster than ptrace for bulk reads, doesn't stop target
// Fallback: open /proc/PID/mem
// ============================================================
struct Mem {
    int pid = 0;

    bool Attach(const char* pkg = "com.dts.freefireth") {
        // find PID by package name
        char path[64];
        for (int i = 100; i < 65536; i++) {
            snprintf(path, sizeof(path), "/proc/%d/cmdline", i);
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            char buf[128] = {0};
            read(fd, buf, sizeof(buf));
            close(fd);
            if (strstr(buf, pkg)) {
                pid = i;
                LOGI("PID found: %d", pid);
                return true;
            }
        }
        LOGE("PID not found for %s", pkg);
        return false;
    }

    bool Read(uintptr_t addr, void* buf, size_t len) {
        struct iovec local = { buf, len };
        struct iovec remote = { (void*)addr, len };
        ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
        return n == (ssize_t)len;
    }

    bool Write(uintptr_t addr, const void* buf, size_t len) {
        struct iovec local = { (void*)buf, len };
        struct iovec remote = { (void*)addr, len };
        ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
        return n == (ssize_t)len;
    }

    template<typename T>
    T Read(uintptr_t addr) {
        T val{};
        Read(addr, &val, sizeof(T));
        return val;
    }

    template<typename T>
    bool Write(uintptr_t addr, T val) {
        return Write(addr, &val, sizeof(T));
    }

    uintptr_t ResolveBase(const char* lib) {
        char path[128], line[512];
        snprintf(path, sizeof(path), "/proc/%d/maps", pid);
        FILE* f = fopen(path, "r");
        if (!f) return 0;
        uintptr_t base = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, lib)) {
                sscanf(line, "%lx-", &base);
                LOGI("%s base: 0x%lx", lib, base);
                break;
            }
        }
        fclose(f);
        return base;
    }
};

// ============================================================
// VECTORS + AIM MATH
// ============================================================
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

static float Distance3D(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// world-to-angle: returns pitch (x) and yaw (y) to aim at target from origin
static Vec2 CalcAim(Vec3 origin, Vec3 target) {
    float dx = target.x - origin.x;
    float dy = target.y - origin.y;
    float dz = target.z - origin.z;
    float horiz = sqrtf(dx*dx + dz*dz);
    float pitch = -atan2f(dy, horiz) * (180.0f / M_PI);
    float yaw = atan2f(dx, dz) * (180.0f / M_PI);
    // Free Fire yaw may need offset depending on forward axis
    // If aim is 180° off, add/subtract 180 here:
    // yaw += 180.0f;
    return { pitch, yaw };
}

// ============================================================
// AIMBOT CORE
// ============================================================
struct AimConfig {
    float fov = 45.0f;           // max degrees off current aim
    float smoothing = 5.0f;      // 1 = snap, higher = slower
    bool target_head = true;     // aim at head bone vs chest
    bool team_check = true;
    float max_dist = 300.0f;     // world units
};

static Mem mem;
static AimConfig cfg;
static bool running = true;

// find nearest valid enemy to crosshair center
static int FindBestTarget(uintptr_t entityList, int count, Vec3 localPos, Vec2 localView) {
    int best = -1;
    float bestDelta = cfg.fov;

    for (int i = 0; i < count; i++) {
        // entity list is array of pointers
        uintptr_t ent = mem.Read<uintptr_t>(entityList + i * sizeof(uintptr_t));
        if (!ent) continue;

        int hp = mem.Read<int>(ent + Offsets::Player::HP_offset);
        if (hp <= 0 || hp > 500) continue;  // dead or invalid

        if (cfg.team_check) {
            int team = mem.Read<int>(ent + Offsets::Player::Team_offset);
            int localTeam = mem.Read<int>(ent + Offsets::LocalPlayer::Pos_offset
                                          + Offsets::Player::Team_offset); // adjust
            if (team == localTeam) continue;
        }

        uintptr_t posOff = cfg.target_head ? Offsets::Player::BoneHead_offset
                                           : Offsets::Player::Pos_offset;
        Vec3 entPos = mem.Read<Vec3>(ent + posOff);
        float dist = Distance3D(localPos, entPos);
        if (dist > cfg.max_dist || dist < 1.0f) continue;

        Vec2 aimTo = CalcAim(localPos, entPos);
        float dPitch = aimTo.x - localView.x;
        float dYaw = aimTo.y - localView.y;
        // normalize yaw delta
        while (dYaw > 180) dYaw -= 360;
        while (dYaw < -180) dYaw += 360;
        float delta = sqrtf(dPitch*dPitch + dYaw*dYaw);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = i;
        }
    }
    return best;
}

// write aim angles directly to local player view
static void SetAim(uintptr_t localPlayer, Vec2 targetView, Vec2 currentView) {
    // smoothing: lerp toward target
    float t = 1.0f / cfg.smoothing;
    float pitch = currentView.x + (targetView.x - currentView.x) * t;
    float yaw = currentView.y + (targetView.y - currentView.y) * t;
    mem.Write<float>(localPlayer + Offsets::LocalPlayer::View_offset, pitch);
    mem.Write<float>(localPlayer + Offsets::LocalPlayer::View_offset + 4, yaw);
}

// ============================================================
// MAIN LOOP
// ============================================================
void AimLoop() {
    if (!mem.Attach()) return;

    Offsets::il2cpp_base = mem.ResolveBase("libil2cpp.so");
    if (!Offsets::il2cpp_base) {
        LOGE("Failed to resolve libil2cpp.so — game may not be loaded yet");
        return;
    }

    LOGI("Aimbot started. il2cpp=0x%lx", Offsets::il2cpp_base);

    while (running) {
        usleep(1000); // ~1ms loop, adjust for CPU/battery

        // resolve GameManager pointer chain — UPDATE per dump
        // This is the most common break point after patches
        uintptr_t gm = mem.Read<uintptr_t>(Offsets::il2cpp_base + Offsets::Player::List_offset);
        if (!gm) continue;

        uintptr_t entityList = mem.Read<uintptr_t>(gm + Offsets::Player::List_offset);
        int count = mem.Read<int>(gm + Offsets::Player::ListCount_offset);
        if (!entityList || count <= 0 || count > 100) continue;  // sanity

        // find local player — scan for IsLocal flag
        uintptr_t localPlayer = 0;
        for (int i = 0; i < count; i++) {
            uintptr_t ent = mem.Read<uintptr_t>(entityList + i * sizeof(uintptr_t));
            if (!ent) continue;
            bool isLocal = mem.Read<bool>(ent + Offsets::Player::IsLocal_offset);
            if (isLocal) { localPlayer = ent; break; }
        }
        if (!localPlayer) continue;

        Vec3 localPos = mem.Read<Vec3>(localPlayer + Offsets::LocalPlayer::Pos_offset);
        Vec2 localView = mem.Read<Vec2>(localPlayer + Offsets::LocalPlayer::View_offset);

        int targetIdx = FindBestTarget(entityList, count, localPos, localView);
        if (targetIdx < 0) continue;

        uintptr_t target = mem.Read<uintptr_t>(entityList + targetIdx * sizeof(uintptr_t));
        uintptr_t posOff = cfg.target_head ? Offsets::Player::BoneHead_offset
                                           : Offsets::Player::Pos_offset;
        Vec3 targetPos = mem.Read<Vec3>(target + posOff);
        Vec2 aimView = CalcAim(localPos, targetPos);
        SetAim(localPlayer, aimView, localView);
    }
}

// JNI entry — call from injected .so or JNI loader
extern "C" __attribute__((visibility("default")))
void Java_com_ff_aim_Start(JNIEnv* env, jobject thiz) {
    AimLoop();
}
