/*
 * knobs.h — Custom rotary knob widget.
 *
 * Provides arc/radial knobs that respond to vertical mouse drag.
 * Drag up = increase, drag down = decrease.
 * Shift+drag = fine adjustment (1/10th rate).
 */

#ifndef SQ_KNOBS_H
#define SQ_KNOBS_H

/*
 * Draw a rotary knob with arc rendering.
 *
 * Returns 1 if the value changed, 0 otherwise.
 *
 * label:       text displayed below the knob
 * value:       pointer to the current value (modified by interaction)
 * min/max:     value range
 * default_val: value to reset to on double-click
 * step:        normal drag sensitivity
 */
int knob_float(const char *label,
               float *value, float min, float max,
               float default_val, float step);

/*
 * Mini knob for tight spaces (e.g., drum grid track controls).
 * Draws a small arc knob (~25px) with label above.
 *
 * Returns 1 if the value changed, 0 otherwise.
 *
 * total_height: total height available for knob + label
 */
int knob_mini(const char *label,
              float *value, float min, float max,
              float default_val, float step,
              float total_height);

/*
 * Inline knob: draws an arc knob in the next widget slot of the
 * current layout row.
 * The widget bounds are obtained from the current layout context.
 *
 * Returns 1 if the value changed, 0 otherwise.
 */
int knob_inline(float *value, float min, float max,
                float default_val, float step);

/*
 * Core knob: draw an arc knob at explicit screen position and size.
 * Used for toolbar knobs and other custom layouts.
 *
 * Returns 1 if the value changed, 0 otherwise.
 */
int knob_core_ext(const char *id_str,
                  float *value, float min, float max,
                  float default_val, float step,
                  /* ImVec2 */ float pos_x, float pos_y,
                  float size_x, float size_y);

#endif /* SQ_KNOBS_H */
