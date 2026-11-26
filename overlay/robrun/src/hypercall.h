#pragma once
#include "shared.h"

// Émet le VMMCALL CMD_ROBRUN_SNAPSHOT vers Pulzar.
// Retourne true si le hyperviseur a rempli le buffer (rax == 0).
bool hypercall_robrun(RobRunSnapshot& snap);
