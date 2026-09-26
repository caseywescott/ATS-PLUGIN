#pragma once

#include "ThemeRecipes.h"

#include <visage/app.h>

namespace ats::ui
{

// Applies a recipe's draw-op list to a Visage canvas. Deliberately the only
// place that translates ThemeDrawOp into canvas calls, so the recipes stay
// headless and testable.
inline void paintThemeOps(visage::Canvas& canvas, const std::vector<ThemeDrawOp>& ops)
{
    for (const auto& op : ops)
    {
        if (op.clipped)
        {
            canvas.saveState();
            canvas.setClampBounds(op.clip.x, op.clip.y, op.clip.width, op.clip.height);
        }
        switch (op.shape)
        {
        case ThemeShape::roundedRect:
            canvas.setColor(op.color.argb);
            canvas.roundedRectangle(op.bounds.x, op.bounds.y, op.bounds.width, op.bounds.height,
                                    op.radius);
            break;
        case ThemeShape::roundedRectBorder:
            canvas.setColor(op.color.argb);
            canvas.roundedRectangleBorder(op.bounds.x, op.bounds.y, op.bounds.width,
                                          op.bounds.height, op.radius, op.thickness);
            break;
        case ThemeShape::roundedRectGradient:
            canvas.setBrush(visage::Brush::linear(
                visage::Color(op.colorFrom.argb), visage::Color(op.color.argb),
                { op.bounds.x, op.bounds.y },
                { op.bounds.x + op.bounds.width, op.bounds.y + op.bounds.height }));
            canvas.roundedRectangle(op.bounds.x, op.bounds.y, op.bounds.width, op.bounds.height,
                                    op.radius);
            break;
        case ThemeShape::ellipse:
            canvas.setColor(op.color.argb);
            canvas.circle(op.bounds.x, op.bounds.y, op.bounds.width);
            break;
        }
        if (op.clipped)
            canvas.restoreState();
    }
}

inline void paintRaisedPanel(visage::Canvas& canvas, const ThemeTokens& t, UiRect r, float radius)
{
    paintThemeOps(canvas, ThemeRecipes::raisedPanel(t, r, radius));
}

inline void paintWell(visage::Canvas& canvas, const ThemeTokens& t, UiRect r, float radius)
{
    paintThemeOps(canvas, ThemeRecipes::well(t, r, radius));
}

inline void paintButtonBackground(visage::Canvas& canvas, const ThemeTokens& t, UiRect r,
                                  bool down, bool hovered)
{
    paintThemeOps(canvas, ThemeRecipes::buttonBackground(t, r, down, hovered));
}

} // namespace ats::ui
