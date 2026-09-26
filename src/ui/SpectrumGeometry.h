#pragma once

#include "SpectralTraceBuilder.h"
#include "UiBackend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ats::ui
{

struct SpectrumRange
{
    float start = 0.0f;
    float end = 1.0f;
    [[nodiscard]] float length() const noexcept { return end - start; }
};

struct SpectrumViewport
{
    float timeCenter = 0.5f;
    float timeZoom = 1.0f;
    float frequencyCenter = 0.5f;
    float frequencyZoom = 1.0f;
};

class SpectrumGeometry
{
public:
    static constexpr float kMinZoom = 1.0f;
    static constexpr float kMaxZoom = 32.0f;
    static constexpr float kZoomBarWidth = 204.0f;
    static constexpr float kWheelPanPixelsPerDelta = 14.0f * 16.0f;
    static constexpr float kTraceScrollBarThickness = 13.0f;
    static constexpr float kBandLabelGutterWidth = 34.0f;

    [[nodiscard]] static UiRect getScreenBounds(UiRect localBounds,
                                                 SpectralTraceMode mode) noexcept
    {
        auto r = reduced(localBounds, 5.0f, 5.0f);
        const auto residual = isResidualDisplayMode(mode);
        r.y += residual ? 50.0f : 30.0f;
        r.height = std::max(0.0f, r.height - (residual ? 50.0f : 30.0f));
        return r;
    }

    [[nodiscard]] static UiRect getTraceContentBounds(UiRect screen) noexcept
    {
        auto content = reduced(screen, 14.0f, 12.0f);
        content.y += 18.0f;
        content.height = std::max(0.0f, content.height - 18.0f - 18.0f - kTraceScrollBarThickness);
        content.width = std::max(0.0f, content.width - kTraceScrollBarThickness);
        return content;
    }

    [[nodiscard]] static bool showsBandLabelGutter(SpectralTraceMode mode,
                                                    std::size_t bandCount,
                                                    std::uint32_t frameCount) noexcept
    { return mode == SpectralTraceMode::residual && bandCount > 0 && frameCount > 0; }

    [[nodiscard]] static bool showsFreqLabelGutter(SpectralTraceMode mode,
                                                   std::uint32_t frameCount) noexcept
    {
        return !isIsometricDisplayMode(mode) && mode != SpectralTraceMode::residual
            && frameCount > 0;
    }

    [[nodiscard]] static UiRect getPlotBounds(UiRect localBounds,
                                               SpectralTraceMode mode,
                                               std::size_t bandCount,
                                               std::uint32_t frameCount) noexcept
    {
        auto plot = getTraceContentBounds(getScreenBounds(localBounds, mode));
        if (showsBandLabelGutter(mode, bandCount, frameCount)
            || showsFreqLabelGutter(mode, frameCount))
        {
            plot.x += kBandLabelGutterWidth;
            plot.width = std::max(0.0f, plot.width - kBandLabelGutterWidth);
        }
        plot.width = std::max(0.0f, plot.width - 10.0f);
        return plot;
    }

    [[nodiscard]] static SpectrumRange visibleTimeRange(const SpectrumViewport& viewport) noexcept
    { return visibleRange(viewport.timeCenter, viewport.timeZoom); }

    [[nodiscard]] static SpectrumRange visibleFrequencyRange(const SpectrumViewport& viewport) noexcept
    { return visibleRange(viewport.frequencyCenter, viewport.frequencyZoom); }

    [[nodiscard]] static UiPoint positionForPoint(const SpectralTracePoint& point,
                                                  UiRect plot,
                                                  const SpectrumViewport& viewport) noexcept
    {
        const auto time = visibleTimeRange(viewport);
        const auto freq = visibleFrequencyRange(viewport);
        const auto x = (point.timeNorm - time.start) / std::max(1.0e-6f, time.length());
        const auto y = (point.freqNorm - freq.start) / std::max(1.0e-6f, freq.length());
        return { plot.x + plot.width * x, plot.y + plot.height - plot.height * y };
    }

    [[nodiscard]] static UiPoint positionForIsoCoords(float timeNorm, float freqNorm,
                                                       float ampNorm, UiRect plot,
                                                       const SpectrumViewport& viewport) noexcept
    {
        const auto time = visibleTimeRange(viewport);
        const auto freq = visibleFrequencyRange(viewport);
        const auto t = (timeNorm - time.start) / std::max(1.0e-6f, time.length());
        const auto f = (freqNorm - freq.start) / std::max(1.0e-6f, freq.length());
        const auto a = std::clamp(ampNorm, 0.0f, 1.25f);
        const UiPoint origin { plot.x + plot.width * 0.075f,
                               plot.y + plot.height - plot.height * 0.085f };
        const UiPoint freqAxis { plot.width * 0.66f, -plot.height * 0.055f };
        const UiPoint timeAxis { plot.width * 0.255f, -plot.height * 0.44f };
        const UiPoint ampAxis { 0.0f, -plot.height * 0.30f };
        return { origin.x + freqAxis.x * f + timeAxis.x * t + ampAxis.x * a,
                 origin.y + freqAxis.y * f + timeAxis.y * t + ampAxis.y * a };
    }

    [[nodiscard]] static float timeNormFromIsoPoint(UiPoint point, UiRect plot,
                                                     const SpectrumViewport& viewport) noexcept
    {
        float time = 0.0f, frequency = 0.0f;
        isoTimeFreqFromPoint(point, plot, viewport, time, frequency);
        return time;
    }

    static void isoTimeFreqFromPoint(UiPoint point, UiRect plot,
                                     const SpectrumViewport& viewport,
                                     float& timeNorm, float& frequencyNorm) noexcept
    {
        const auto tr = visibleTimeRange(viewport);
        const auto fr = visibleFrequencyRange(viewport);
        const auto origin = positionForIsoCoords(tr.start, fr.start, 0.0f, plot, viewport);
        const auto ta = positionForIsoCoords(tr.end, fr.start, 0.0f, plot, viewport);
        const auto fa = positionForIsoCoords(tr.start, fr.end, 0.0f, plot, viewport);
        const auto tx = ta.x - origin.x, ty = ta.y - origin.y;
        const auto fx = fa.x - origin.x, fy = fa.y - origin.y;
        const auto rx = point.x - origin.x, ry = point.y - origin.y;
        const auto det = fx * ty - tx * fy;
        float t = 0.0f, f = 0.0f;
        if (std::abs(det) > 1.0e-6f)
        {
            f = (rx * ty - tx * ry) / det;
            t = (fx * ry - rx * fy) / det;
        }
        timeNorm = std::clamp(tr.start + std::clamp(t, 0.0f, 1.0f) * tr.length(), 0.0f, 1.0f);
        frequencyNorm = std::clamp(fr.start + std::clamp(f, 0.0f, 1.0f) * fr.length(), 0.0f, 1.0f);
    }

    [[nodiscard]] static bool isIsometricDisplayMode(SpectralTraceMode mode) noexcept
    {
        return mode == SpectralTraceMode::isometric || mode == SpectralTraceMode::isometricResidual
            || mode == SpectralTraceMode::isometricEnvelope
            || mode == SpectralTraceMode::isometricEnvelopeRdc;
    }

    [[nodiscard]] static bool isResidualDisplayMode(SpectralTraceMode mode) noexcept
    { return mode == SpectralTraceMode::residual || mode == SpectralTraceMode::isometricResidual; }

private:
    [[nodiscard]] static UiRect reduced(UiRect r, float x, float y) noexcept
    { r.x += x; r.y += y; r.width = std::max(0.0f, r.width - 2.0f * x); r.height = std::max(0.0f, r.height - 2.0f * y); return r; }

    [[nodiscard]] static SpectrumRange visibleRange(float center, float zoom) noexcept
    {
        const auto halfSpan = 0.5f / std::max(1.0f, zoom);
        return { center - halfSpan, center + halfSpan };
    }
};

} // namespace ats::ui
