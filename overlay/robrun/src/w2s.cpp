#include "w2s.h"
#include <DirectXMath.h>
#include <cmath>
using namespace DirectX;

Screen w2s(const Vec3& world, const CamData& cam, float sw, float sh) {
    Screen out{0.f, 0.f, false};

    // Reconstruit la matrice view depuis fwd/up (cam.right sert de cross-check)
    XMVECTOR eye = XMVectorSet(cam.pos.x, cam.pos.y, cam.pos.z, 0.f);
    XMVECTOR fwd = XMVectorSet(cam.fwd.x, cam.fwd.y, cam.fwd.z, 0.f);
    XMVECTOR up  = XMVectorSet(cam.up.x,  cam.up.y,  cam.up.z,  0.f);
    XMMATRIX view = XMMatrixLookToLH(eye, fwd, up);

    // Projection perspective — fov est vertical en degrés (Unity m_VerticalFOV)
    float fov_rad = XMConvertToRadians(cam.fov > 0.f ? cam.fov : 60.f);
    float aspect  = sw / sh;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov_rad, aspect, 0.05f, 3000.f);

    XMMATRIX vp = XMMatrixMultiply(view, proj);

    XMVECTOR wp   = XMVectorSet(world.x, world.y, world.z, 1.f);
    XMVECTOR clip = XMVector4Transform(wp, vp);

    float w = XMVectorGetW(clip);
    if (w < 0.01f) return out;

    float ndcX =  XMVectorGetX(clip) / w;
    float ndcY = -XMVectorGetY(clip) / w; // Unity Y inversé vs DX clip space

    if (fabsf(ndcX) > 1.f || fabsf(ndcY) > 1.f) return out;

    out.x       = (ndcX + 1.f) * 0.5f * sw;
    out.y       = (ndcY + 1.f) * 0.5f * sh;
    out.visible = true;
    return out;
}
