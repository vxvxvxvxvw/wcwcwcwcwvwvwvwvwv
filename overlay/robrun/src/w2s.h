#pragma once
#include "shared.h"

struct Screen { float x, y; bool visible; };

// World-to-screen Unity il2cpp.
// CamData contient pos/fwd/right/up + fov vertical (m_VerticalFOV en degrés).
// La matrice view est reconstruite depuis fwd/up — pas de matrice native passée
// car le hyperviseur ne lit pas le UnityEngine.Camera.projectionMatrix.
// sw, sh : résolution écran en pixels.
Screen w2s(const Vec3& world, const CamData& cam, float sw, float sh);
