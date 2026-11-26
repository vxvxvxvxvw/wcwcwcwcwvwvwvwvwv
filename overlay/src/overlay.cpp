// overlay.cpp — W2S + draw ESP via ImGui DrawList
#include "overlay.hpp"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <cwchar>

// ─── W2S ─────────────────────────────────────────────────────────────────────

bool world_to_screen(const Camera& cam, Vec3 world,
                     float sw, float sh, Vec2& out) {
    using namespace DirectX;

    XMVECTOR wp = XMVectorSet(world.x, world.y, world.z, 1.0f);
    XMMATRIX vp = XMLoadFloat4x4(&cam.view_proj);
    XMVECTOR clip = XMVector4Transform(wp, vp);

    float w = XMVectorGetW(clip);
    if (w < 0.01f) return false;

    float ndcX = XMVectorGetX(clip) / w;
    float ndcY = XMVectorGetY(clip) / w;

    out.x = (ndcX + 1.0f) * 0.5f * sw;
    out.y = (1.0f - ndcY) * 0.5f * sh;
    return true;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static float dist_m(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    // Unreal units = cm → /100 → mètres
    return std::sqrt(dx*dx + dy*dy + dz*dz) * 0.01f;
}

static ImVec4 team_color(uint8_t team, bool is_local) {
    if (is_local) return {0.2f, 1.0f, 0.4f, 1.0f};   // vert
    if (team == 0)  return {1.0f, 0.3f, 0.3f, 1.0f}; // rouge (ennemi solo)
    // couleur déterministe par team index
    float h = (team * 47) % 360 / 360.0f;
    // HSV simple → RGB
    int   i = (int)(h * 6);
    float f = h * 6 - i;
    float q = 1.0f - f;
    switch (i % 6) {
        case 0: return {1,f,0,1};
        case 1: return {q,1,0,1};
        case 2: return {0,1,f,1};
        case 3: return {0,q,1,1};
        case 4: return {f,0,1,1};
        default:return {1,0,q,1};
    }
}

static ImU32 col4(ImVec4 c, float a=1.0f) {
    return IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(a*255));
}

// ─── draw_esp ────────────────────────────────────────────────────────────────

// Indices os snapshot (ordre défini dans entities.rs / Bone enum)
// 0=pelvis 1=spine1 2=spine2 3=spine3 4=neck 5=head
// 6=lshoulder 7=lelbow 8=lhand 9=rshoulder 10=relbow 11=rhand
// 12=lhip 13=lknee 14=lfoot 15=rhip 16=rknee 17=rfoot
static constexpr int SKEL[][2] = {
    {0,1},{1,2},{2,3},{3,4},{4,5},   // spine + tête
    {3,6},{6,7},{7,8},               // bras gauche
    {3,9},{9,10},{10,11},            // bras droit
    {0,12},{12,13},{13,14},          // jambe gauche
    {0,15},{15,16},{16,17},          // jambe droite
};

void draw_esp(const WorldSnapshot& snap, const Camera& cam,
              float sw, float sh, const EspCfg& cfg) {

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (uint32_t i = 0; i < snap.count && i < MAX_ENTITIES; ++i) {
        const EntityData& e = snap.entities[i];
        if (!e.valid)                           continue;
        if (e.is_local)                         continue;
        if (e.is_dying  && !cfg.show_dying)     continue;
        if (e.is_bot    && !cfg.show_bots)      continue;
        if (e.last_render_time == 0.0f)         continue;

        Vec3 origin { e.origin[0], e.origin[1], e.origin[2] };
        float dm = dist_m(cam.position, origin);
        if (dm > cfg.max_dist)                  continue;

        Vec3 head_v { e.head[0], e.head[1], e.head[2] };
        Vec2 s_head, s_origin;
        if (!world_to_screen(cam, head_v,   sw, sh, s_head))   continue;
        if (!world_to_screen(cam, origin,   sw, sh, s_origin)) continue;

        ImVec4 col4v = team_color(e.team_index, e.is_local);
        ImU32  col   = col4(col4v);
        ImU32  black = IM_COL32(0,0,0,180);

        // ── Box 2D ───────────────────────────────────────────────────────────
        if (cfg.boxes) {
            float h  = s_origin.y - s_head.y;
            float w  = h * 0.4f;
            float cx = (s_head.x + s_origin.x) * 0.5f;
            ImVec2 tl { cx - w, s_head.y };
            ImVec2 br { cx + w, s_origin.y };
            dl->AddRect({tl.x+1, tl.y+1}, {br.x+1, br.y+1}, black, 0, 0, 2.0f);
            dl->AddRect(tl, br, col, 0, 0, 1.5f);
        }

        // ── Skeleton ─────────────────────────────────────────────────────────
        if (cfg.skeleton) {
            Vec2 bscr[19];
            bool bvis[19];
            for (int b = 0; b < 19; ++b) {
                Vec3 bw { e.bones[b][0], e.bones[b][1], e.bones[b][2] };
                bvis[b] = world_to_screen(cam, bw, sw, sh, bscr[b]);
            }
            for (auto& [a, b] : SKEL) {
                if (!bvis[a] || !bvis[b]) continue;
                dl->AddLine({bscr[a].x+1, bscr[a].y+1},
                            {bscr[b].x+1, bscr[b].y+1}, black, 2.0f);
                dl->AddLine({bscr[a].x, bscr[a].y},
                            {bscr[b].x, bscr[b].y}, col, 1.5f);
            }
        }

        // ── Nom ───────────────────────────────────────────────────────────────
        if (cfg.names) {
            char buf[128];
            // UTF-16 → narrow (ASCII fallback)
            int n = 0;
            for (int c = 0; c < 64 && e.name[c] && n < 127; ++c)
                buf[n++] = (char)(e.name[c] < 128 ? e.name[c] : '?');
            buf[n] = '\0';
            if (n > 0) {
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float tx = s_head.x - ts.x * 0.5f;
                float ty = s_head.y - ts.y - 2.0f;
                dl->AddText({tx+1, ty+1}, black, buf);
                dl->AddText({tx,   ty},   col,   buf);
            }
        }

        // ── Distance ─────────────────────────────────────────────────────────
        if (cfg.distance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", dm);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            float tx = s_origin.x - ts.x * 0.5f;
            float ty = s_origin.y + 2.0f;
            dl->AddText({tx+1, ty+1}, black, buf);
            dl->AddText({tx,   ty},   IM_COL32(220,220,220,255), buf);
        }

        // ── Arme ─────────────────────────────────────────────────────────────
        {
            char buf[128]; int n = 0;
            for (int c = 0; c < 64 && e.weapon_name[c] && n < 127; ++c)
                buf[n++] = (char)(e.weapon_name[c] < 128 ? e.weapon_name[c] : '?');
            buf[n] = '\0';
            if (n > 0) {
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float h  = s_origin.y - s_head.y;
                float cx = (s_head.x + s_origin.x) * 0.5f;
                float bx = cx + h * 0.4f + 4.0f;
                float by = s_head.y;
                dl->AddText({bx+1, by+1}, black, buf);
                dl->AddText({bx,   by},   IM_COL32(255,220,80,255), buf);
                // ammo
                char ammo[16];
                snprintf(ammo, sizeof(ammo), " [%d]", e.ammo);
                dl->AddText({bx + ts.x + 1, by + 1}, black, ammo);
                dl->AddText({bx + ts.x,     by},     IM_COL32(180,180,180,255), ammo);
            }
        }
    }
}
