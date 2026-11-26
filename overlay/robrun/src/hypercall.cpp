#include "hypercall.h"
#include <intrin.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>

extern "C" uint64_t _rr_vmmcall(uint64_t cmd, void* buf, uint64_t sz, uint64_t module_base);

// Trouve GameAssembly.dll via TH32CS_SNAPMODULE sur le PID de ROB RUN.exe
static uint64_t find_gameassembly_base() {
    // 1. Trouve le PID — log tous les process pour diagnostiquer le vrai nom
    DWORD target_pid = 0;

    FILE* f = nullptr;
    fopen_s(&f, "C:\\Users\\PC\\Downloads\\rr_base.txt", "w");

    // Noms candidats à tester (ordre de préférence)
    static const wchar_t* CANDIDATES[] = {
        L"RobRun.exe",
        L"ROB RUN.exe",
        L"Rob Run.exe",
        L"robrun.exe",
        L"Rob_Run.exe",
        L"ROBRUN.exe",
        nullptr
    };

    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{ sizeof(pe) };
            if (Process32FirstW(snap, &pe)) {
                do {
                    // Log tout pour diagnostic
                    if (f) {
                        char namebuf[260]{};
                        WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, namebuf, sizeof(namebuf), nullptr, nullptr);
                        fprintf(f, "PROC: %s (pid=%lu)\n", namebuf, pe.th32ProcessID);
                    }
                    // Cherche le premier candidat qui match
                    if (target_pid == 0) {
                        for (int i = 0; CANDIDATES[i]; ++i) {
                            if (_wcsicmp(pe.szExeFile, CANDIDATES[i]) == 0) {
                                target_pid = pe.th32ProcessID;
                                if (f) {
                                    char nbuf[260]{};
                                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nbuf, sizeof(nbuf), nullptr, nullptr);
                                    fprintf(f, "MATCH: %s pid=%lu\n", nbuf, target_pid);
                                }
                                break;
                            }
                        }
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
    }

    if (target_pid == 0) {
        if (f) { fprintf(f, "ROB RUN: aucun candidat trouve dans la liste — voir PROC: ci-dessus\n"); fclose(f); }
        return 0;
    }

    // 2. Snapshot modules du process — TH32CS_SNAPMODULE marche sans OpenProcess
    uint64_t base = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, target_pid);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{ sizeof(me) };
        if (Module32FirstW(snap, &me)) {
            do {
                if (_wcsicmp(me.szModule, L"GameAssembly.dll") == 0) {
                    base = reinterpret_cast<uint64_t>(me.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
    }

    if (f) {
        if (base)
            fprintf(f, "GameAssembly.dll base = 0x%llX (pid=%lu)\n", base, target_pid);
        else
            fprintf(f, "GameAssembly.dll NOT FOUND via TH32CS_SNAPMODULE (pid=%lu)\n", target_pid);
        fclose(f);
    }

    return base;
}

static uint64_t s_module_base = 0;

// Retourne true si hypercall ok (rc != 0), false sinon
bool hypercall_robrun(RobRunSnapshot& snap) {
    if (s_module_base == 0)
        s_module_base = find_gameassembly_base();

    uint64_t rc = 0;
    __try {
        rc = _rr_vmmcall(
            CMD_ROBRUN_SNAPSHOT,
            &snap,
            static_cast<uint64_t>(sizeof(snap)),
            s_module_base
        );
    }
    __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
              ? EXCEPTION_EXECUTE_HANDLER
              : EXCEPTION_CONTINUE_SEARCH) {
        FILE* f = nullptr;
        fopen_s(&f, "C:\\Users\\PC\\Downloads\\rr_hv.txt", "w");
        if (f) { fprintf(f, "VMMCALL: #UD - Pulzar absent\n"); fclose(f); }
        return false;
    }

    if (rc == 0) s_module_base = 0; // invalider → retry au prochain frame
    return rc != 0;
}
