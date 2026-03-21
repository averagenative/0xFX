/*
 * knobs.cpp -- Custom rotary knob widget using Dear ImGui.
 *
 * Draws circular arcs using ImGui DrawList. The arc sweeps 270 degrees
 * from ~135 degrees (bottom-left) to ~405 degrees (bottom-right).
 *
 * The knob supports:
 * - Vertical drag to change value
 * - Shift+drag for fine adjustment (1/10th rate)
 * - Double-click to reset to default
 * - Value text display
 */

#include "imgui.h"

extern "C" {
#include "texture.h"
}

#include <math.h>
#include <stdio.h>

/* Arc angles in radians.
 * Start at bottom-left (135 deg = 2.356 rad), sweep clockwise 270 degrees
 * to bottom-right (405 deg = -0.785 rad in our coordinate system).
 * We use math convention: angles measured counter-clockwise from +X axis,
 * but screen Y is flipped so we negate sin for Y coords. */
#define ARC_START_ANGLE  2.356194f   /* 135 degrees */
#define ARC_END_ANGLE   -0.785398f   /* -45 degrees (= 315 degrees going CW) */
#define ARC_RANGE        3.141593f   /* 270 degrees in radians */
#define ARC_SEGMENTS     32

/* Colors — "worn grime" amber theme */
static const ImU32 col_knob_bg    = IM_COL32(40, 36, 30, 255);
static const ImU32 col_knob_track = IM_COL32(30, 27, 22, 255);
static const ImU32 col_knob_accent= IM_COL32(210, 150, 40, 255);  /* amber */
static const ImU32 col_knob_dot   = IM_COL32(230, 210, 180, 255); /* warm white */

/* --- Internal: draw an arc on the draw list -------------------------------- */

static void draw_arc(ImDrawList *dl,
                     float cx, float cy, float radius,
                     float from_angle, float to_angle,
                     int segments, float thickness,
                     ImU32 color)
{
    if (segments < 1) return;
    float range = from_angle - to_angle;
    for (int i = 0; i < segments; i++) {
        float a1 = from_angle - (float)i / (float)segments * range;
        float a2 = from_angle - (float)(i + 1) / (float)segments * range;
        float x1 = cx + radius * cosf(a1);
        float y1 = cy - radius * sinf(a1);
        float x2 = cx + radius * cosf(a2);
        float y2 = cy - radius * sinf(a2);
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, thickness);
    }
}

/* --- Internal: shared knob core logic -------------------------------------- */

/*
 * Draw and interact with an arc knob at the given position and size.
 * Returns 1 if value changed.
 */
static int knob_core(const char *id_str,
                     float *value, float min, float max,
                     float default_val, float step,
                     ImVec2 pos, ImVec2 size)
{
    float old_val = *value;
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Knob geometry */
    float knob_size = size.x < size.y ? size.x : size.y;
    float radius = knob_size * 0.4f;
    float cx = pos.x + size.x * 0.5f;
    float cy = pos.y + size.y * 0.5f;

    /* Normalized value 0..1 */
    float norm = 0.0f;
    if (max > min)
        norm = (*value - min) / (max - min);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    /* Draw filled circle background */
    dl->AddCircleFilled(ImVec2(cx, cy), radius + 2, col_knob_bg, 32);

    /* Draw background track arc (full sweep, dark) */
    draw_arc(dl, cx, cy, radius,
             ARC_START_ANGLE, ARC_END_ANGLE,
             ARC_SEGMENTS, 3.0f, col_knob_track);

    /* Draw value arc (partial sweep, colored) */
    if (norm > 0.001f) {
        float value_angle = ARC_START_ANGLE - (norm * ARC_RANGE);
        int val_segs = (int)(norm * ARC_SEGMENTS);
        if (val_segs < 1) val_segs = 1;
        draw_arc(dl, cx, cy, radius,
                 ARC_START_ANGLE, value_angle,
                 val_segs, 3.0f, col_knob_accent);
    }

    /* Draw indicator line from center toward current value position */
    {
        float dot_angle = ARC_START_ANGLE - (norm * ARC_RANGE);
        float inner_r = radius * 0.3f;
        float outer_r = radius * 0.7f;
        float ix = cx + inner_r * cosf(dot_angle);
        float iy = cy - inner_r * sinf(dot_angle);
        float ox = cx + outer_r * cosf(dot_angle);
        float oy = cy - outer_r * sinf(dot_angle);
        dl->AddLine(ImVec2(ix, iy), ImVec2(ox, oy), col_knob_dot, 2.0f);
    }

    /* Draw indicator dot at current value position */
    {
        float dot_angle = ARC_START_ANGLE - (norm * ARC_RANGE);
        float dot_r = radius * 0.7f;
        float dx = cx + dot_r * cosf(dot_angle);
        float dy = cy - dot_r * sinf(dot_angle);
        dl->AddCircleFilled(ImVec2(dx, dy), 2.5f, col_knob_dot, 8);
    }

    /* --- Mouse interaction ------------------------------------------------- */

    /* Use InvisibleButton for hit detection */
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id_str, size);
    bool is_active = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();

    /* Check shift for fine adjustment */
    float actual_step = step;
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyShift) {
        actual_step = step * 0.1f;
    }

    /* Vertical drag: mouse delta Y changes value */
    if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dy = io.MouseDelta.y;
        if (dy != 0.0f) {
            /* Drag up (negative dy) = increase value
             * Scale by range so full knob sweep ≈ 200 pixels regardless of step size */
            float range = max - min;
            float speed = (range > 0.0f) ? range / 200.0f : actual_step;
            if (io.KeyShift) speed *= 0.1f;
            *value -= dy * speed;
            if (*value < min) *value = min;
            if (*value > max) *value = max;
        }
    }

    /* Double-click to reset to default */
    if (is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *value = default_val;
    }

    return (*value != old_val) ? 1 : 0;
}

/* --- Public API ------------------------------------------------------------ */

extern "C" {

int knob_float(const char *label, float *value, float min, float max,
               float default_val, float step)
{
    ImGui::PushID(label);

    float knob_sz = 28.0f;
    float group_w = knob_sz + 8.0f;

    /* Render as a compact group so SameLine() works */
    ImGui::BeginGroup();

    /* Label (centered above knob) */
    float label_w = ImGui::CalcTextSize(label).x;
    float label_indent = (group_w - label_w) * 0.5f;
    if (label_indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_indent);
    ImGui::TextUnformatted(label);

    /* Knob */
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(knob_sz, knob_sz);
    int changed = knob_core(label, value, min, max, default_val, step, pos, size);

    /* Value (centered below knob) */
    char val_text[16];
    if (max - min > 100.0f)
        snprintf(val_text, sizeof(val_text), "%.0f", *value);
    else
        snprintf(val_text, sizeof(val_text), "%.2f", *value);
    float text_w = ImGui::CalcTextSize(val_text).x;
    float val_indent = (group_w - text_w) * 0.5f;
    if (val_indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + val_indent);
    ImGui::TextUnformatted(val_text);

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

int knob_mini(const char *label, float *value, float min, float max,
              float default_val, float step, float total_height)
{
    ImGui::PushID(label);

    float knob_h = total_height - 14.0f; /* reserve space for label */
    if (knob_h < 16.0f) knob_h = 16.0f;

    float avail_w = ImGui::GetContentRegionAvail().x;
    float knob_w = avail_w;
    if (knob_w > knob_h) knob_w = knob_h; /* keep square */

    /* Label */
    float text_w = ImGui::CalcTextSize(label).x;
    float label_indent = (avail_w - text_w) * 0.5f;
    if (label_indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_indent);
    ImGui::TextUnformatted(label);

    /* Knob area */
    float indent = (avail_w - knob_w) * 0.5f;
    if (indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(knob_w, knob_h);

    int changed = knob_core(label, value, min, max, default_val, step, pos, size);

    ImGui::PopID();
    return changed;
}

int knob_inline(float *value, float min, float max,
                float default_val, float step)
{
    /* Generate a unique ID based on value pointer */
    ImGui::PushID(value);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    /* Use the available region for the inline knob */
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetFrameHeight();
    if (avail_w < 16.0f) avail_w = 16.0f;
    if (avail_h < 16.0f) avail_h = 16.0f;
    ImVec2 size(avail_w, avail_h);

    int changed = knob_core("##inline", value, min, max, default_val, step, pos, size);

    ImGui::PopID();
    return changed;
}

int knob_core_ext(const char *id_str,
                  float *value, float min, float max,
                  float default_val, float step,
                  float pos_x, float pos_y,
                  float size_x, float size_y)
{
    return knob_core(id_str, value, min, max, default_val, step,
                     ImVec2(pos_x, pos_y), ImVec2(size_x, size_y));
}

/* --- Texture-mapped knob --------------------------------------------------- */

/*
 * Rotate a 2-D point (px, py) around (cx, cy) by angle_rad.
 */
static void rotate_point(float px, float py,
                          float cx, float cy,
                          float angle_rad,
                          float *out_x, float *out_y)
{
    float s = sinf(angle_rad);
    float c = cosf(angle_rad);
    float dx = px - cx;
    float dy = py - cy;
    *out_x = cx + dx * c - dy * s;
    *out_y = cy + dx * s + dy * c;
}

int knob_textured(const char *label,
                  float *value, float min, float max,
                  float default_val, float step,
                  const char *knob_texture_path)
{
    /* Try to load the texture.  Fall back gracefully to arc knob. */
    uintptr_t tex = 0;
    if (knob_texture_path && knob_texture_path[0])
        tex = fx_texture_load(knob_texture_path);

    if (!tex)
        return knob_float(label, value, min, max, default_val, step);

    ImGui::PushID(label);

    const float knob_sz = 40.0f;   /* rendered size (square) */
    const float group_w = knob_sz + 8.0f;

    ImGui::BeginGroup();

    /* Label (centered above knob) */
    float label_w = ImGui::CalcTextSize(label).x;
    float label_indent = (group_w - label_w) * 0.5f;
    if (label_indent > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_indent);
    ImGui::TextUnformatted(label);

    /* Knob position */
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(knob_sz, knob_sz);

    /* Normalized value [0, 1] */
    float norm = 0.0f;
    if (max > min)
        norm = (*value - min) / (max - min);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    /* Rotation: 135° at min, -135° at max (ImGui Y-down = negate standard math) */
    const float DEG_TO_RAD = 3.14159265f / 180.0f;
    float angle_deg = -135.0f + norm * 270.0f;
    float angle_rad = angle_deg * DEG_TO_RAD;

    /* Centre of the knob quad */
    float cx = pos.x + size.x * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    float hw = size.x * 0.5f;
    float hh = size.y * 0.5f;

    /* Corners before rotation (screen space) */
    float corners[4][2] = {
        { pos.x,          pos.y          },   /* top-left  */
        { pos.x + size.x, pos.y          },   /* top-right */
        { pos.x + size.x, pos.y + size.y },   /* bot-right */
        { pos.x,          pos.y + size.y },   /* bot-left  */
    };
    (void)hw; (void)hh;

    /* Rotate corners around centre */
    ImVec2 rv[4];
    for (int i = 0; i < 4; i++) {
        float rx, ry;
        rotate_point(corners[i][0], corners[i][1], cx, cy, angle_rad, &rx, &ry);
        rv[i] = ImVec2(rx, ry);
    }

    /* UV corners (standard quad mapping) */
    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 0.0f);
    ImVec2 uv2(1.0f, 1.0f);
    ImVec2 uv3(0.0f, 1.0f);

    /* Draw rotated quad */
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddImageQuad((ImTextureID)(uintptr_t)tex,
                     rv[0], rv[1], rv[2], rv[3],
                     uv0, uv1, uv2, uv3,
                     IM_COL32_WHITE);

    /* Mouse interaction — identical to knob_core() */
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(label, size);
    bool is_active  = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();

    ImGuiIO &io = ImGui::GetIO();
    float old_val = *value;

    if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dy = io.MouseDelta.y;
        if (dy != 0.0f) {
            float range = max - min;
            float speed = (range > 0.0f) ? range / 200.0f : step;
            if (io.KeyShift) speed *= 0.1f;
            *value -= dy * speed;
            if (*value < min) *value = min;
            if (*value > max) *value = max;
        }
    }

    if (is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        *value = default_val;

    /* Value label (centered below knob) */
    char val_text[16];
    if (max - min > 100.0f)
        snprintf(val_text, sizeof(val_text), "%.0f", *value);
    else
        snprintf(val_text, sizeof(val_text), "%.2f", *value);

    float text_w    = ImGui::CalcTextSize(val_text).x;
    float val_indent = (group_w - text_w) * 0.5f;
    if (val_indent > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + val_indent);
    ImGui::TextUnformatted(val_text);

    ImGui::EndGroup();
    ImGui::PopID();
    return (*value != old_val) ? 1 : 0;
}

int knob_overlay(const char *id_str,
                 float *value, float min, float max,
                 float default_val, float step,
                 float scr_x, float scr_y, float knob_sz,
                 const char *knob_texture_path)
{
    uintptr_t tex = 0;
    if (knob_texture_path && knob_texture_path[0])
        tex = fx_texture_load(knob_texture_path);

    ImGui::PushID(id_str);

    /* Normalized value [0, 1] */
    float norm = 0.0f;
    if (max > min)
        norm = (*value - min) / (max - min);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    float old_val = *value;
    bool is_interactive = (step > 0.0f);

    float cx = scr_x + knob_sz * 0.5f;
    float cy = scr_y + knob_sz * 0.5f;
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Glow ring behind interactive knobs — white-amber blend visible on any faceplate */
    if (is_interactive) {
        float glow_r = knob_sz * 0.52f;
        dl->AddCircleFilled(ImVec2(cx, cy), glow_r + 3.0f,
            IM_COL32(255, 220, 140, 30), 20);
        dl->AddCircleFilled(ImVec2(cx, cy), glow_r + 1.0f,
            IM_COL32(255, 200, 100, 45), 20);
        dl->AddCircle(ImVec2(cx, cy), glow_r,
            IM_COL32(255, 210, 120, 70), 20, 1.0f);
    }

    if (tex) {
        /* Rotation: 135° at min, -135° at max */
        const float DEG_TO_RAD = 3.14159265f / 180.0f;
        float angle_deg = -135.0f + norm * 270.0f;
        float angle_rad = angle_deg * DEG_TO_RAD;

        float corners[4][2] = {
            { scr_x,            scr_y            },
            { scr_x + knob_sz,  scr_y            },
            { scr_x + knob_sz,  scr_y + knob_sz  },
            { scr_x,            scr_y + knob_sz  },
        };

        ImVec2 rv[4];
        for (int i = 0; i < 4; i++) {
            float rx, ry;
            rotate_point(corners[i][0], corners[i][1], cx, cy, angle_rad, &rx, &ry);
            rv[i] = ImVec2(rx, ry);
        }

        dl->AddImageQuad((ImTextureID)(uintptr_t)tex,
                         rv[0], rv[1], rv[2], rv[3],
                         ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
                         IM_COL32_WHITE);

        /* Position indicator dot at the edge of the knob.
         * Clock face: 7:30 (min=0) → clockwise → 4:30 (max=1)
         * Screen coords: 0°=right, Y-down means positive angle = clockwise.
         * 7:30 on clock = 225° from 12 = 225° CW from top.
         * In math coords (0°=right): 7:30 = 225°-90° = 135° but with Y-down.
         * Simplest: use screen angle where 0°=top, CW positive.
         *   7:30 = 225°, 4:30 = 315° (wrap) = -45° = 360-45=315
         *   Actually: 7:30→12→4:30 = 270° CW sweep
         *   start_screen = 225° * pi/180 = 3.927 rad from top
         * Convert screen-from-top to math: math_angle = screen_angle - pi/2
         * Easier: just compute x,y directly.
         *   screen_angle = (225 + norm * 270) degrees from 12-o-clock, CW
         *   x = sin(screen_angle)  (sin because 0°=top, right=90°)
         *   y = -cos(screen_angle) (negative cos because 0°=top=up=-Y) wait...
         * In screen Y-down, from 12-o-clock (top):
         *   x = sin(a), y = -cos(a) gives CW rotation.
         * No wait: at 0° (12 o'clock): sin(0)=0, -cos(0)=-1 → (0,-1) = UP in screen. Wrong.
         * Screen Y-down: UP = negative Y. So (0,-1) IS up. That's 12 o'clock. ✓
         * At 90° (3 o'clock): sin(90)=1, -cos(90)=0 → (1,0) = right. ✓
         * At 180° (6 o'clock): sin(180)=0, -cos(180)=1 → (0,1) = down. ✓
         * At 270° (9 o'clock): sin(270)=-1, -cos(270)=0 → (-1,0) = left. ✓
         * So: x=sin(a), y=-cos(a) with a in degrees-from-12-CW. Perfect.
         */
        if (is_interactive) {
            float deg = 225.0f + norm * 270.0f; /* 225°=7:30, 495°=4:30 */
            float rad = deg * 3.14159265f / 180.0f;
            float dot_r_offset = knob_sz * 0.38f;
            float dot_x = cx + dot_r_offset * sinf(rad);
            float dot_y = cy - dot_r_offset * cosf(rad);
            dl->AddCircleFilled(ImVec2(dot_x, dot_y), 3.5f, IM_COL32(255, 255, 255, 220), 8);
            dl->AddCircleFilled(ImVec2(dot_x, dot_y), 2.0f, IM_COL32(220, 50, 30, 255), 8);
        }
    } else {
        /* Fallback: small arc knob at position */
        float cx = scr_x + knob_sz * 0.5f;
        float cy = scr_y + knob_sz * 0.5f;
        float r  = knob_sz * 0.4f;

        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(cx, cy), r + 3.0f, col_knob_bg);
        draw_arc(dl, cx, cy, r, ARC_START_ANGLE, ARC_END_ANGLE, ARC_SEGMENTS, 3.0f, col_knob_track);
        float val_angle = ARC_START_ANGLE - norm * ARC_RANGE;
        draw_arc(dl, cx, cy, r, ARC_START_ANGLE, val_angle, (int)(norm * ARC_SEGMENTS), 3.0f, col_knob_accent);
        float dot_x = cx + (r - 4.0f) * cosf(val_angle);
        float dot_y = cy - (r - 4.0f) * sinf(val_angle);
        dl->AddCircleFilled(ImVec2(dot_x, dot_y), 3.0f, col_knob_dot);
    }

    /* Mouse interaction */
    ImGui::SetCursorScreenPos(ImVec2(scr_x, scr_y));
    ImGui::InvisibleButton(id_str, ImVec2(knob_sz, knob_sz));
    bool is_active  = ImGui::IsItemActive();

    ImGuiIO &io = ImGui::GetIO();

    if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dy = io.MouseDelta.y;
        if (dy != 0.0f) {
            float range = max - min;
            float speed = (range > 0.0f) ? range / 200.0f : step;
            if (io.KeyShift) speed *= 0.1f;
            *value -= dy * speed;
            if (*value < min) *value = min;
            if (*value > max) *value = max;
        }
    }

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            *value = default_val;
        /* Clean tooltip: extract param name, show as "Volume: 0.50" */
        char tip_name[32];
        int ti = 0;
        for (int ci = 0; id_str[ci] && id_str[ci] != '#' && ti < 30; ci++)
            tip_name[ti++] = id_str[ci];
        tip_name[ti] = '\0';
        /* Show value as percentage-like 0-10 scale */
        float display_val = (max > min) ? (*value - min) / (max - min) * 10.0f : *value;
        ImGui::SetTooltip("%s: %.1f", tip_name, display_val);
    }

    /* Abbreviated label below the knob for interactive knobs */
    if (is_interactive) {
        /* Extract param name from id_str (format: "ParamName##...") */
        char label_buf[32];
        int li = 0;
        for (int ci = 0; id_str[ci] && id_str[ci] != '#' && li < 30; ci++)
            label_buf[li++] = id_str[ci];
        label_buf[li] = '\0';

        /* Abbreviate long names to fit between knobs */
        static const struct { const char *full; const char *abbr; } abbrevs[] = {
            { "Presence", "Pres" }, { "Feedback", "Fdbk" },
            { "Treble", "Treb" }, { "Volume", "Vol" },
            { "Master", "Mstr" }, { "Distance", "Dist" },
        };
        for (int ai = 0; ai < 6; ai++) {
            if (strcmp(label_buf, abbrevs[ai].full) == 0) {
                strcpy(label_buf, abbrevs[ai].abbr);
                break;
            }
        }

        ImGui::SetWindowFontScale(0.85f);
        ImVec2 label_sz = ImGui::CalcTextSize(label_buf);
        ImGui::SetWindowFontScale(1.0f);
        float label_x = cx - label_sz.x * 0.5f;
        float label_y = scr_y + knob_sz + 1.0f;
        /* Draw with black outline for readability on any background */
        ImGui::SetWindowFontScale(0.85f);
        ImFont *font = ImGui::GetFont();
        float fsz = ImGui::GetFontSize();
        for (int ox = -1; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++)
                if (ox || oy)
                    dl->AddText(font, fsz,
                        ImVec2(label_x + ox, label_y + oy),
                        IM_COL32(0, 0, 0, 200), label_buf);
        dl->AddText(font, fsz,
            ImVec2(label_x, label_y),
            IM_COL32(220, 200, 170, 230), label_buf);
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::PopID();
    return (*value != old_val) ? 1 : 0;
}

} /* extern "C" */
