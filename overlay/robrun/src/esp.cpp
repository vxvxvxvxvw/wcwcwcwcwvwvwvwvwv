// esp.cpp — robrun-overlay  |  dessin ESP complet
// Hotkeys gérés dans main.cpp (F1 toggle, F2 skeleton, F3 dist, END quit)

#include "esp.h"
#include "w2s.h"
#include <cstdio>
#include <cmath>
#include <string>

// ── Options ESP (toggles depuis main.cpp) ────────────────────────────────────
bool g_esp_boxes    = true;
bool g_esp_snapline = true;
bool g_esp_names    = true;
bool g_esp_dist     = true;
bool g_esp_role     = true;
bool g_esp_armor    = true;
bool g_esp_state    = true;   // KO, VEH, sprint, crouch

// ── Couleurs par rôle ────────────────────────────────────────────────────────
static ImU32 role_color(uint8_t role) {
    switch (role) {
        case 0:  return IM_COL32(255,  65,  65, 255); // bandit  — rouge
        case 1:  return IM_COL32( 65, 145, 255, 255); // police  — bleu
        case 2:  return IM_COL32( 50, 220,  80, 255); // medic   — vert
        case 3:  return IM_COL32(240, 220,  50, 255); // civ     — jaune
        default: return IM_COL32(210, 210, 210, 255); // inconnu — gris
    }
}

static const char* role_name(uint8_t role) {
    switch (role) {
        case 0: return "BANDIT";
        case 1: return "POLICE";
        case 2: return "MEDIC";
        case 3: return "CIV";
        default: return "?";
    }
}

// UTF-16LE → ASCII best-effort
static std::string nick_to_str(const uint16_t* nick, size_t max = 32) {
    std::string out;
    out.reserve(max);
    for (size_t i = 0; i < max && nick[i]; ++i) {
        uint16_t c = nick[i];
        out += (c < 0x80) ? static_cast<char>(c) : '?';
    }
    return out;
}

static float dist3(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Dessine un rectangle avec contour sombre pour lisibilité
static void draw_rect(ImDrawList* dl, float x0, float y0, float x1, float y1,
                      ImU32 col, float thickness = 1.3f) {
    dl->AddRect({x0, y0}, {x1, y1}, IM_COL32(0,0,0,180), 0.f, 0, thickness + 1.5f);
    dl->AddRect({x0, y0}, {x1, y1}, col,                 0.f, 0, thickness);
}

// Texte avec ombre portée 1px
static void draw_text_shadow(ImDrawList* dl, float x, float y, ImU32 col, const char* s) {
    dl->AddText({x+1.f, y+1.f}, IM_COL32(0,0,0,200), s);
    dl->AddText({x,     y    }, col,                  s);
}

// ── draw_esp ─────────────────────────────────────────────────────────────────
void draw_esp(ImDrawList* dl, const RobRunSnapshot& snap, float sw, float sh) {

    for (uint32_t i = 0; i < snap.count && i < MAX_RR_ENTITIES; ++i) {
        const RobRunEntity& e = snap.entities[i];
        if (!e.valid || e.is_local) continue;

        Screen head_s   = w2s(e.head,   snap.camera, sw, sh);
        Screen origin_s = w2s(e.origin, snap.camera, sw, sh);

        bool any_vis = head_s.visible || origin_s.visible;
        if (!any_vis) continue;

        ImU32 col = role_color(e.role);

        float metres = dist3(e.origin, snap.camera.pos);

        // ── Snapline ────────────────────────────────────────────────────────
        if (g_esp_snapline && origin_s.visible) {
            dl->AddLine({sw * 0.5f, sh}, {origin_s.x, origin_s.y},
                        IM_COL32(255, 255, 255, 50), 1.f);
        }

        // ── Boîte 2D ────────────────────────────────────────────────────────
        if (g_esp_boxes && head_s.visible && origin_s.visible) {
            float h  = fabsf(origin_s.y - head_s.y);
            if (h < 2.f) h = 2.f;
            float cx = (head_s.x + origin_s.x) * 0.5f;
            float w2 = h * 0.33f;
            float top    = head_s.y   - h * 0.06f;
            float bottom = origin_s.y + h * 0.04f;
            draw_rect(dl, cx - w2, top, cx + w2, bottom, col, 1.3f);

            // Barre armure sur le côté gauche (si armor > 0)
            if (g_esp_armor && e.armor > 0) {
                float bar_x  = cx - w2 - 4.f;
                float ratio  = (float)e.armor / 100.f;
                if (ratio > 1.f) ratio = 1.f;
                float bar_h  = bottom - top;
                // fond
                dl->AddRectFilled({bar_x - 2.f, top}, {bar_x, bottom},
                                  IM_COL32(0,0,0,160));
                // remplissage (cyan pour armure)
                dl->AddRectFilled({bar_x - 2.f, top + bar_h * (1.f - ratio)},
                                  {bar_x, bottom},
                                  IM_COL32(80, 220, 255, 220));
            }
        }

        // ── Dot tête ────────────────────────────────────────────────────────
        if (head_s.visible) {
            dl->AddCircleFilled({head_s.x, head_s.y}, 3.5f, col);
            dl->AddCircle      ({head_s.x, head_s.y}, 3.5f, IM_COL32(0,0,0,180), 12, 1.5f);
        }

        // ── Label ───────────────────────────────────────────────────────────
        if (head_s.visible) {
            char buf[192];
            std::string nick = nick_to_str(e.nick);

            // Ligne 1 : [ROLE] nom   (si activé)
            if (g_esp_names || g_esp_role) {
                char nbuf[128];
                if (!nick.empty() && g_esp_names)
                    snprintf(nbuf, sizeof(nbuf), "%s [%s]", nick.c_str(),
                             g_esp_role ? role_name(e.role) : "");
                else if (g_esp_role)
                    snprintf(nbuf, sizeof(nbuf), "[%s]", role_name(e.role));
                else
                    nbuf[0] = '\0';

                if (nbuf[0])
                    draw_text_shadow(dl, head_s.x + 5.f, head_s.y - 28.f, col, nbuf);
            }

            // Ligne 2 : distance   (si activé)
            if (g_esp_dist) {
                snprintf(buf, sizeof(buf), "%.0fm", metres);
                draw_text_shadow(dl, head_s.x + 5.f, head_s.y - 15.f,
                                 IM_COL32(220, 220, 220, 230), buf);
            }

            // Ligne 3 : états KO / VEH / sprint / crouch   (si activé)
            if (g_esp_state) {
                char sbuf[64] = {};
                int  sp       = 0;
                if (e.is_knocked)  sp += snprintf(sbuf + sp, sizeof(sbuf) - sp, " KO");
                if (e.in_vehicle)  sp += snprintf(sbuf + sp, sizeof(sbuf) - sp, " VEH");
                if (e.is_sprinting)sp += snprintf(sbuf + sp, sizeof(sbuf) - sp, " SPR");
                if (e.is_crouching)sp += snprintf(sbuf + sp, sizeof(sbuf) - sp, " CRO");
                if (sp > 0) {
                    ImU32 sc = e.is_knocked ? IM_COL32(255,80,80,230)
                             : e.in_vehicle  ? IM_COL32(255,200,50,230)
                             :                 IM_COL32(180,180,180,200);
                    draw_text_shadow(dl, head_s.x + 5.f, head_s.y - 2.f, sc, sbuf + 1); // skip leading space
                }
            }
        }
    }
}
