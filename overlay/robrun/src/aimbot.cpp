// aimbot.cpp — ROB RUN aimbot (writes look direction into game memory)
//
// ESP data comes from the Pulzar hypervisor (snapshot). Aimbot writes the
// computed yaw/pitch directly into BattleInputModel._lookDirection via
// WriteProcessMemory on ROB RUN.exe (EAC free tier: no kernel protection).
//
// Hotkeys / toggles live in main.cpp.

#include "aimbot.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ── Globals ──────────────────────────────────────────────────────────────────
bool  g_aim_on         = false;
bool  g_aim_bone_head  = true;
float g_aim_smooth     = 0.15f;
float g_aim_fov        = 15.f;
bool  g_aim_spread     = true;

// ── State ─────────────────────────────────────────────────────────────────────
static HANDLE  s_hProc      = nullptr;
static uint64_t s_gameBase  = 0;
static uint64_t s_inputModel = 0;   // BattleInputModel* in guest VA
static bool     s_spreadPatched = false;

static float s_curYaw   = 0.f;
static float s_curPitch = 0.f;
static bool  s_hasCur   = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void logf(const char* fmt, ...) {
    FILE* f = nullptr;
    fopen_s(&f, "C:\\Users\\PC\\Downloads\\rr_aim.txt", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

static bool readMem(uint64_t va, void* out, size_t n) {
    SIZE_T rd = 0;
    return s_hProc && ReadProcessMemory(s_hProc, (LPCVOID)va, out, n, &rd) && rd == n;
}
static bool writeMem(uint64_t va, const void* in, size_t n) {
    SIZE_T wr = 0;
    return s_hProc && WriteProcessMemory(s_hProc, (LPVOID)va, in, n, &wr) && wr == n;
}
static uint64_t read64(uint64_t va) {
    uint64_t v = 0;
    readMem(va, &v, 8);
    return v;
}

static uint64_t findGamePid() {
    static const wchar_t* CANDS[] = {
        L"RobRun.exe", L"ROB RUN.exe", L"Rob Run.exe", L"robrun.exe",
        L"Rob_Run.exe", L"ROBRUN.exe", nullptr
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    uint64_t pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pid == 0) {
                for (int i = 0; CANDS[i]; ++i) {
                    if (_wcsicmp(pe.szExeFile, CANDS[i]) == 0) { pid = pe.th32ProcessID; break; }
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// Resolve GameAssembly.dll base by walking the game process's PEB loader list
// via ReadProcessMemory. TH32CS_SNAPMODULE is blocked by EAC on protected
// processes, so we read the LDR inlist directly.
static uint64_t findModuleBase(uint64_t pid) {
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return 0;

    // NtQueryInformationProcess → PEB base address
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) { CloseHandle(h); return 0; }
    auto NtQIP = (LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG))
        GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!NtQIP) { CloseHandle(h); return 0; }

    struct { LONG ExitStatus; PVOID PebBaseAddress; ULONG_PTR AffinityMask;
             LONG BasePriority; ULONG_PTR UniqueProcessId; ULONG_PTR InheritedFromUniqueProcessId; } pbi{};
    ULONG ret = 0;
    if (NtQIP(h, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &ret) != 0 || !pbi.PebBaseAddress) {
        CloseHandle(h); return 0;
    }

    auto R64 = [&](uint64_t va, uint64_t& out) -> bool {
        SIZE_T rd = 0;
        return ReadProcessMemory(h, (LPCVOID)va, &out, 8, &rd) && rd == 8;
    };
    auto R16 = [&](uint64_t va, uint16_t& out) -> bool {
        SIZE_T rd = 0;
        return ReadProcessMemory(h, (LPCVOID)va, &out, 2, &rd) && rd == 2;
    };

    uint64_t peb = (uint64_t)pbi.PebBaseAddress;
    uint64_t ldr = 0;
    if (!R64(peb + 0x18, ldr) || !ldr) { CloseHandle(h); return 0; }

    uint64_t entry = 0;
    if (!R64(ldr + 0x10, entry) || !entry) { CloseHandle(h); return 0; }

    uint64_t head = ldr + 0x10;
    uint64_t base = 0;
    for (int i = 0; i < 64 && entry && entry != head; ++i) {
        uint64_t dllBase = 0, nameBuf = 0;
        uint16_t nameLen = 0;
        if (!R64(entry + 0x30, dllBase)) break;
        if (!R16(entry + 0x58, nameLen)) break;
        if (!R64(entry + 0x58 + 8, nameBuf)) break;

        if (dllBase && nameBuf && nameLen >= 14 * 2) {
            // read first 14 wide chars
            uint16_t chars[14]{};
            SIZE_T rd = 0;
            bool ok = ReadProcessMemory(h, (LPCVOID)nameBuf, chars, sizeof(chars), &rd) && rd == sizeof(chars);
            // needle "GameAssembly.d" (14 wide chars)
            static const uint16_t needle[14] = {
                0x47,0x61,0x6D,0x65, 0x41,0x73,0x73,0x65,
                0x6D,0x62,0x6C,0x79, 0x2E,0x64
            };
            if (ok) {
                bool match = true;
                for (int k = 0; k < 14; ++k) if (chars[k] != needle[k]) { match = false; break; }
                if (match) { base = dllBase; break; }
            }
        }
        // Flink
        uint64_t next = 0;
        if (!R64(entry, next)) break;
        entry = next;
    }

    CloseHandle(h);
    return base;
}

// ── Init ─────────────────────────────────────────────────────────────────────
bool aimbot_init() {
    // reset log
    DeleteFileW(L"C:\\Users\\PC\\Downloads\\rr_aim.txt");

    uint64_t pid = findGamePid();
    if (!pid) { logf("aimbot: ROB RUN pid not found"); return false; }
    logf("aimbot: pid=%llu", (unsigned long long)pid);

    s_hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                          PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (!s_hProc) { logf("aimbot: OpenProcess failed (%lu)", GetLastError()); return false; }

    s_gameBase = findModuleBase(pid);
    if (!s_gameBase) { logf("aimbot: GameAssembly.dll base not found"); return false; }
    logf("aimbot: GameAssembly.dll = 0x%llX", (unsigned long long)s_gameBase);

    // Resolve BattleModel singleton:
    //   TypeInfo at gameBase + 136615656 ; statics = *(TypeInfo + 0xB8) ; battle = *(statics + 0)
    uint64_t ti      = s_gameBase + 136615656ULL;
    uint64_t statics = read64(ti + 0xB8);
    if (!statics) { logf("aimbot: statics null (ti=0x%llX)", (unsigned long long)ti); return false; }
    uint64_t battle  = read64(statics + 0);
    if (!battle) { logf("aimbot: battle_model null"); return false; }
    logf("aimbot: battle_model = 0x%llX", (unsigned long long)battle);

    // BattleInputModel at battle + 0x50 (offset from offsets.json dump)
    uint64_t input = read64(battle + 0x50);
    if (!input) { logf("aimbot: input model null at battle+0x50"); return false; }
    s_inputModel = input;
    logf("aimbot: input_model = 0x%llX  look@+0x18  changed@+0x32", (unsigned long long)input);

    return true;
}

// ── Update ───────────────────────────────────────────────────────────────────
void aimbot_update(const RobRunSnapshot& snap) {
    if (!g_aim_on || !s_inputModel) return;

    // ── Patch spread once ────────────────────────────────────────────────────
    if (g_aim_spread && !s_spreadPatched) {
        uint8_t nop5[5] = { 0x90,0x90,0x90,0x90,0x90 };
        if (writeMem(s_gameBase + 0x4361194ULL, nop5, 5) &&
            writeMem(s_gameBase + 0x43611C2ULL, nop5, 5)) {
            s_spreadPatched = true;
            logf("aimbot: spread patched (NOP x2)");
        }
    }

    // ── Camera ───────────────────────────────────────────────────────────────
    Vec3 camPos = snap.camera.pos;
    Vec3 camFwd = snap.camera.fwd;
    float flen = std::sqrt(camFwd.x*camFwd.x + camFwd.y*camFwd.y + camFwd.z*camFwd.z);
    if (flen < 1e-6f) return;
    camFwd.x /= flen; camFwd.y /= flen; camFwd.z /= flen;

    float fovRad = g_aim_fov * 3.14159265f / 180.f;
    float cosLimit = std::cos(fovRad * 0.5f);

    // ── Pick best target (smallest angle to crosshair) ───────────────────────
    int best = -1;
    float bestCos = cosLimit;
    for (uint32_t i = 0; i < snap.count && i < MAX_RR_ENTITIES; ++i) {
        const RobRunEntity& e = snap.entities[i];
        if (!e.valid || e.is_local) continue;
        if (e.is_knocked) continue;

        Vec3 aimPt = g_aim_bone_head ? e.head : Vec3{
            (e.origin.x + e.head.x) * 0.5f,
            (e.origin.y + e.head.y) * 0.5f,
            (e.origin.z + e.head.z) * 0.5f
        };

        float dx = aimPt.x - camPos.x;
        float dy = aimPt.y - camPos.y;
        float dz = aimPt.z - camPos.z;
        float len = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (len < 0.1f) continue;
        float cosA = (camFwd.x*dx + camFwd.y*dy + camFwd.z*dz) / len;
        if (cosA > bestCos) { bestCos = cosA; best = (int)i; }
    }
    if (best < 0) return;

    const RobRunEntity& t = snap.entities[best];
    Vec3 aimPt = g_aim_bone_head ? t.head : Vec3{
        (t.origin.x + t.head.x) * 0.5f,
        (t.origin.y + t.head.y) * 0.5f,
        (t.origin.z + t.head.z) * 0.5f
    };

    float dx = aimPt.x - camPos.x;
    float dy = aimPt.y - camPos.y;
    float dz = aimPt.z - camPos.z;
    float horiz = std::sqrt(dx*dx + dz*dz);
    float yaw   = std::atan2(dx, dz);
    float pitch = std::atan2(dy, horiz);

    // ── Smoothing ────────────────────────────────────────────────────────────
    if (!s_hasCur) { s_curYaw = yaw; s_curPitch = pitch; s_hasCur = true; }
    else {
        float factor = 1.f - g_aim_smooth;
        if (factor < 0.01f) factor = 0.01f;
        if (factor > 1.f)    factor = 1.f;
        float dyaw = yaw - s_curYaw;
        if (dyaw >  3.14159265f) dyaw -= 6.2831853f;
        if (dyaw < -3.14159265f) dyaw += 6.2831853f;
        s_curYaw   += dyaw * factor;
        s_curPitch += (pitch - s_curPitch) * factor;
    }

    // ── Write look direction ─────────────────────────────────────────────────
    struct { float yaw, pitch; } look{ s_curYaw, s_curPitch };
    if (!writeMem(s_inputModel + 0x18, &look, 8)) {
        // log occasionally
        static int failc = 0;
        if (++failc % 60 == 1) logf("aimbot: write look failed");
        return;
    }
    uint8_t one = 1;
    writeMem(s_inputModel + 0x32, &one, 1);
}