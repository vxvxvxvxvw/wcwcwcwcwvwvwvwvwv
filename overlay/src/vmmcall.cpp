// vmmcall.cpp — implémente _vmmcall_snapshot_asm en MASM x64
// MSVC x64 n'a pas d'inline asm, donc on utilise /FAcs + un .asm séparé.
// Ce .cpp appelle la fonction assembleur définie dans vmmcall_asm.asm.
//
// Pour compiler : cmake génère fn_overlay.vcxproj avec vmmcall_asm.asm en MASM.
// ml64.exe /c /Cx vmmcall_asm.asm  → vmmcall_asm.obj

#include "vmmcall.hpp"

// Déclaration externe — définie dans vmmcall_asm.asm
extern "C" uint64_t _vmmcall_snapshot_asm(uint64_t cmd, void* buf, uint64_t sz);
