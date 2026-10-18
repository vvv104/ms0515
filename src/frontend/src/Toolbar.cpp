/*
 * Toolbar.cpp — buttons with drawn icons.
 */
#include "Toolbar.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ms0515_frontend {

namespace {

constexpr float kPi = 3.14159265f;

/* The icon's square: side s, top-left p; strokes thick enough to read at
 * the size of the font. */
struct Box {
    ImVec2 p;
    float  s;
    ImVec2 at(float x, float y) const { return ImVec2(p.x + x * s, p.y + y * s); }
};

void drawFloppy(ImDrawList *dl, const Box &b, ImU32 c, float t, const char *label)
{
    /* The jacket with its cut corner, the shutter at the top, the label. */
    const ImVec2 pts[] = {b.at(0.12f, 0.08f), b.at(0.78f, 0.08f), b.at(0.90f, 0.20f),
                          b.at(0.90f, 0.92f), b.at(0.12f, 0.92f)};
    dl->AddPolyline(pts, 5, c, ImDrawFlags_Closed, t);
    dl->AddRect(b.at(0.32f, 0.08f), b.at(0.64f, 0.26f), c, 0.0f, 0, t);
    if (label) {
        const float fs = b.s * 0.62f;
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.0f, label);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(b.p.x + (b.s - sz.x) * 0.51f, b.p.y + b.s * 0.36f), c, label);
    }
}

void drawArrow(ImDrawList *dl, const Box &b, ImU32 c, float t, bool down)
{
    /* A tray, and an arrow into it (save) or out of it (load). */
    dl->AddLine(b.at(0.12f, 0.70f), b.at(0.12f, 0.90f), c, t);
    dl->AddLine(b.at(0.12f, 0.90f), b.at(0.88f, 0.90f), c, t);
    dl->AddLine(b.at(0.88f, 0.90f), b.at(0.88f, 0.70f), c, t);
    dl->AddLine(b.at(0.50f, 0.08f), b.at(0.50f, 0.66f), c, t);
    if (down)
        dl->AddTriangleFilled(b.at(0.28f, 0.48f), b.at(0.72f, 0.48f), b.at(0.50f, 0.74f), c);
    else
        dl->AddTriangleFilled(b.at(0.28f, 0.30f), b.at(0.72f, 0.30f), b.at(0.50f, 0.04f), c);
}

void drawIcon(ImDrawList *dl, ToolIcon icon, const Box &b, ImU32 c)
{
    const float t = std::max(1.5f, b.s * 0.09f);
    switch (icon) {
    case ToolIcon::Reset: {
        /* A circle three-quarters round, with the arrow at its end. */
        const ImVec2 o = b.at(0.5f, 0.54f);
        const float  r = b.s * 0.32f;
        dl->PathArcTo(o, r, -kPi * 0.35f, kPi * 1.25f, 20);
        dl->PathStroke(c, 0, t);
        const ImVec2 e(o.x + r * std::cos(-kPi * 0.35f), o.y + r * std::sin(-kPi * 0.35f));
        dl->AddTriangleFilled(ImVec2(e.x - b.s * 0.20f, e.y - b.s * 0.08f),
                              ImVec2(e.x + b.s * 0.10f, e.y - b.s * 0.20f),
                              ImVec2(e.x + b.s * 0.06f, e.y + b.s * 0.14f), c);
        break;
    }
    case ToolIcon::Pause:
        dl->AddRectFilled(b.at(0.24f, 0.16f), b.at(0.42f, 0.84f), c);
        dl->AddRectFilled(b.at(0.58f, 0.16f), b.at(0.76f, 0.84f), c);
        break;
    case ToolIcon::Run:
        dl->AddTriangleFilled(b.at(0.26f, 0.14f), b.at(0.26f, 0.86f), b.at(0.84f, 0.50f), c);
        break;
    case ToolIcon::Disk0:    drawFloppy(dl, b, c, t, "0"); break;
    case ToolIcon::Disk1:    drawFloppy(dl, b, c, t, "1"); break;
    case ToolIcon::HardDisk: {
        /* A drive's box, its platter seen from the top, its lamp. */
        dl->AddRect(b.at(0.08f, 0.22f), b.at(0.92f, 0.82f), c, b.s * 0.08f, 0, t);
        dl->AddCircle(b.at(0.40f, 0.52f), b.s * 0.18f, c, 16, t);
        dl->AddCircleFilled(b.at(0.40f, 0.52f), b.s * 0.04f, c);
        dl->AddCircleFilled(b.at(0.78f, 0.70f), b.s * 0.05f, c);
        break;
    }
    case ToolIcon::SaveState: drawArrow(dl, b, c, t, true);  break;
    case ToolIcon::LoadState: drawArrow(dl, b, c, t, false); break;
    case ToolIcon::Screenshot:
        /* A camera: the body, the viewfinder's bump, the lens. */
        dl->AddRect(b.at(0.08f, 0.28f), b.at(0.92f, 0.84f), c, b.s * 0.08f, 0, t);
        dl->AddRectFilled(b.at(0.34f, 0.16f), b.at(0.62f, 0.30f), c);
        dl->AddCircle(b.at(0.50f, 0.56f), b.s * 0.17f, c, 16, t);
        break;
    case ToolIcon::SoundOn:
    case ToolIcon::SoundOff: {
        const ImVec2 sp[] = {b.at(0.08f, 0.38f), b.at(0.26f, 0.38f), b.at(0.48f, 0.16f),
                             b.at(0.48f, 0.84f), b.at(0.26f, 0.62f), b.at(0.08f, 0.62f)};
        dl->AddConvexPolyFilled(sp, 6, c);
        if (icon == ToolIcon::SoundOn) {
            for (float r : {0.18f, 0.32f}) {
                dl->PathArcTo(b.at(0.50f, 0.50f), b.s * r, -kPi * 0.28f, kPi * 0.28f, 10);
                dl->PathStroke(c, 0, t);
            }
        } else {
            dl->AddLine(b.at(0.62f, 0.34f), b.at(0.90f, 0.66f), c, t);
            dl->AddLine(b.at(0.90f, 0.34f), b.at(0.62f, 0.66f), c, t);
        }
        break;
    }
    case ToolIcon::Keyboard: {
        dl->AddRect(b.at(0.04f, 0.24f), b.at(0.96f, 0.80f), c, b.s * 0.06f, 0, t);
        const float k = b.s * 0.05f;
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 5; ++col) {
                const ImVec2 q = b.at(0.18f + col * 0.16f, 0.38f + row * 0.15f);
                dl->AddRectFilled(ImVec2(q.x - k, q.y - k), ImVec2(q.x + k, q.y + k), c);
            }
        dl->AddLine(b.at(0.30f, 0.68f), b.at(0.70f, 0.68f), c, t);
        break;
    }
    case ToolIcon::Debugger: {
        /* A bug: the body, the head, three legs a side. */
        dl->AddEllipseFilled(b.at(0.50f, 0.58f), ImVec2(b.s * 0.20f, b.s * 0.27f), c);
        dl->AddCircleFilled(b.at(0.50f, 0.24f), b.s * 0.11f, c);
        /* Each leg goes up from the body to its knee, then down to the foot. */
        for (float y : {0.46f, 0.60f, 0.74f}) {
            const ImVec2 left[]  = {b.at(0.32f, y), b.at(0.18f, y - 0.12f), b.at(0.05f, y + 0.06f)};
            const ImVec2 right[] = {b.at(0.68f, y), b.at(0.82f, y - 0.12f), b.at(0.95f, y + 0.06f)};
            dl->AddPolyline(left,  3, c, ImDrawFlags_None, t * 0.7f);
            dl->AddPolyline(right, 3, c, ImDrawFlags_None, t * 0.7f);
        }
        break;
    }
    case ToolIcon::Fullscreen: {
        /* Four corners pointing out. */
        const float a = 0.10f, z = 0.90f, l = 0.30f;
        dl->AddLine(b.at(a, a), b.at(a + l, a), c, t); dl->AddLine(b.at(a, a), b.at(a, a + l), c, t);
        dl->AddLine(b.at(z, a), b.at(z - l, a), c, t); dl->AddLine(b.at(z, a), b.at(z, a + l), c, t);
        dl->AddLine(b.at(a, z), b.at(a + l, z), c, t); dl->AddLine(b.at(a, z), b.at(a, z - l), c, t);
        dl->AddLine(b.at(z, z), b.at(z - l, z), c, t); dl->AddLine(b.at(z, z), b.at(z, z - l), c, t);
        break;
    }
    }
}

float iconSide() { return ImGui::GetFontSize() * 1.5f; }
float buttonPad() { return ImGui::GetFontSize() * 0.30f; }

} // namespace

float toolbarHeight()
{
    return iconSide() + buttonPad() * 2.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
}

bool toolButton(const char *id, ToolIcon icon, const char *tooltip, bool on, bool enabled)
{
    const float s = iconSide(), pad = buttonPad();
    const ImVec2 size(s + pad * 2.0f, s + pad * 2.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##tool", size) && enabled;
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    const float  round = pad;
    if (on)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImGuiCol_ButtonActive), round);
    else if (enabled && (hovered || held))
        dl->AddRectFilled(p, q, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), round);
    const ImU32 c = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    drawIcon(dl, icon, Box{ImVec2(p.x + pad, p.y + pad), s}, c);

    if (hovered && tooltip) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.15f);
    return clicked;
}

void toolSeparator()
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  h = iconSide() + buttonPad() * 2.0f;
    const float  w = ImGui::GetFontSize() * 0.6f;
    ImGui::Dummy(ImVec2(w, h));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + w * 0.5f, p.y + h * 0.18f),
                                        ImVec2(p.x + w * 0.5f, p.y + h * 0.82f),
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 0.15f);
}

} // namespace ms0515_frontend
