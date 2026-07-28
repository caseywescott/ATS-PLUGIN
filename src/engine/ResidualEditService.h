#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ats::engine
{

struct ResidualBandData
{
    int bandIndex = -1;
    std::vector<double> energy;

    friend bool operator==(const ResidualBandData& a, const ResidualBandData& b) noexcept
    { return a.bandIndex == b.bandIndex && a.energy == b.energy; }
    friend bool operator!=(const ResidualBandData& a, const ResidualBandData& b) noexcept
    { return !(a == b); }
};

struct ResidualMatrix
{
    std::vector<ResidualBandData> bands;

    [[nodiscard]] std::size_t frameCount() const noexcept
    {
        std::size_t count = 0;
        for (const auto& band : bands) count = std::max(count, band.energy.size());
        return count;
    }

    [[nodiscard]] ResidualBandData* find(int band) noexcept
    {
        for (auto& value : bands) if (value.bandIndex == band) return &value;
        return nullptr;
    }
    [[nodiscard]] const ResidualBandData* find(int band) const noexcept
    {
        for (const auto& value : bands) if (value.bandIndex == band) return &value;
        return nullptr;
    }
};

struct ResidualSelection
{
    std::vector<int> bands;
    int firstFrame = 0;
    int lastFrame = 0;
};

struct ResidualClipboard
{
    int sourceBandIndex = -1;
    std::vector<double> energies;
    [[nodiscard]] bool valid() const noexcept { return !energies.empty(); }
};

enum class ResidualEditOperation : std::uint8_t
{
    mute,
    scale,
    drawDb,
    smooth,
    tilt,
    copy,
    paste,
    swap,
    redistribute
};

struct ResidualEditParameters
{
    double scale = 1.0;
    int smoothRadiusFrames = 1;
    double tiltDbPerBandStep = 0.0;
    std::vector<std::pair<int, double>> drawDb;
    int otherBand = -1;
};

struct ResidualEditResult
{
    bool success = false;
    bool changed = false;
    bool finite = true;
    double energyBefore = 0.0;
    double energyAfter = 0.0;
    std::string error;
};

class ResidualEditService
{
public:
    static constexpr double kDbFloor = -120.0;
    static constexpr double kDbCeiling = 48.0;

    [[nodiscard]] static ResidualEditResult apply(ResidualMatrix& matrix,
                                                  ResidualEditOperation operation,
                                                  const ResidualSelection& selection,
                                                  const ResidualEditParameters& parameters = {},
                                                  ResidualClipboard* clipboard = nullptr)
    {
        const auto bands = uniqueBands(selection.bands);
        if (bands.empty()) return failure("No residual bands selected.");
        if (matrix.frameCount() == 0) return failure("No residual-band data.");

        const auto first = clampFrame(std::min(selection.firstFrame, selection.lastFrame), matrix.frameCount());
        const auto last = clampFrame(std::max(selection.firstFrame, selection.lastFrame), matrix.frameCount());
        if (first > last) return failure("Invalid frame selection.");

        if (operation == ResidualEditOperation::copy)
            return copyBand(matrix, bands, first, last, clipboard);
        if (operation == ResidualEditOperation::paste)
            return pasteBand(matrix, bands, first, last, clipboard);

        ResidualMatrix working = matrix;
        const auto before = selectedEnergy(working, bands, first, last);
        ResidualEditResult result;
        result.success = true;

        switch (operation)
        {
        case ResidualEditOperation::mute:
            forEachSelected(working, bands, first, last, [](double& value) { value = 0.0; });
            break;
        case ResidualEditOperation::scale:
            if (!finiteNonNegative(parameters.scale)) return failure("Scale must be finite and non-negative.");
            forEachSelected(working, bands, first, last, [&parameters](double& value) {
                value = std::max(0.0, value * parameters.scale);
            });
            break;
        case ResidualEditOperation::drawDb:
            if (!applyDrawDb(working, bands, first, last, parameters.drawDb, result.error))
                return failure(result.error);
            break;
        case ResidualEditOperation::smooth:
            if (parameters.smoothRadiusFrames < 1)
                return failure("Smooth radius must be positive.");
            smooth(working, bands, first, last, parameters.smoothRadiusFrames);
            break;
        case ResidualEditOperation::tilt:
            if (!std::isfinite(parameters.tiltDbPerBandStep)
                || std::abs(parameters.tiltDbPerBandStep) < 1.0e-12)
                return failure("Tilt amount must be finite and non-zero.");
            tilt(working, bands, first, last, parameters.tiltDbPerBandStep);
            break;
        case ResidualEditOperation::swap:
            if (bands.size() != 2) return failure("Swap requires exactly two bands.");
            swap(working, bands[0], bands[1], first, last);
            break;
        case ResidualEditOperation::redistribute:
            if (bands.size() < 2) return failure("Redistribution requires at least two bands.");
            redistribute(working, bands, first, last);
            break;
        case ResidualEditOperation::copy:
        case ResidualEditOperation::paste:
            break;
        }

        if (!validate(working))
            return failure("Residual edit produced non-finite or negative energy.");
        result.energyBefore = before;
        result.energyAfter = selectedEnergy(working, bands, first, last);
        result.changed = working.bands != matrix.bands;
        matrix = std::move(working);
        return result;
    }

private:
    [[nodiscard]] static ResidualEditResult failure(std::string message)
    { return { false, false, true, 0.0, 0.0, std::move(message) }; }

    [[nodiscard]] static bool finiteNonNegative(double value) noexcept
    { return std::isfinite(value) && value >= 0.0; }

    [[nodiscard]] static std::vector<int> uniqueBands(const std::vector<int>& values)
    {
        std::vector<int> result;
        for (const auto value : values)
            if (value >= 0 && std::find(result.begin(), result.end(), value) == result.end())
                result.push_back(value);
        std::sort(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] static std::size_t clampFrame(int frame, std::size_t count) noexcept
    { return count == 0 ? 1 : std::min<std::size_t>(count - 1, std::max(0, frame)); }

    template <typename Function>
    static void forEachSelected(ResidualMatrix& matrix,
                                const std::vector<int>& bands,
                                std::size_t first,
                                std::size_t last,
                                Function function)
    {
        for (const auto bandIndex : bands)
        {
            auto* band = matrix.find(bandIndex);
            if (band == nullptr || band->energy.empty()) continue;
            const auto end = std::min(last, band->energy.size() - 1);
            for (auto frame = std::min(first, band->energy.size() - 1); frame <= end; ++frame)
                function(band->energy[frame]);
        }
    }

    [[nodiscard]] static double selectedEnergy(const ResidualMatrix& matrix,
                                                const std::vector<int>& bands,
                                                std::size_t first,
                                                std::size_t last) noexcept
    {
        double total = 0.0;
        for (const auto bandIndex : bands)
        {
            const auto* band = matrix.find(bandIndex);
            if (band == nullptr || band->energy.empty()) continue;
            const auto end = std::min(last, band->energy.size() - 1);
            for (auto frame = std::min(first, band->energy.size() - 1); frame <= end; ++frame)
                if (finiteNonNegative(band->energy[frame])) total += band->energy[frame];
        }
        return total;
    }

    static bool validate(const ResidualMatrix& matrix) noexcept
    {
        for (const auto& band : matrix.bands)
            for (const auto value : band.energy)
                if (!finiteNonNegative(value)) return false;
        return true;
    }

    static double dbToEnergy(double db) noexcept
    { return std::pow(10.0, std::clamp(db, kDbFloor, kDbCeiling) / 10.0); }

    static double energyToDb(double energy) noexcept
    { return 10.0 * std::log10(std::max(1.0e-20, energy)); }

    static bool applyDrawDb(ResidualMatrix& matrix,
                            const std::vector<int>& bands,
                            std::size_t first,
                            std::size_t last,
                            const std::vector<std::pair<int, double>>& points,
                            std::string& error)
    {
        if (bands.size() != 1 || points.empty()) { error = "Draw requires one band and points."; return false; }
        std::map<int, double> sorted;
        for (const auto& point : points)
            if (std::isfinite(point.second)) sorted[point.first] = point.second;
        if (sorted.empty()) { error = "Draw contains no finite points."; return false; }
        auto* band = matrix.find(bands.front());
        if (band == nullptr || band->energy.empty()) { error = "Selected band is unavailable."; return false; }
        const auto begin = sorted.begin();
        for (auto frame = first; frame <= last && frame < band->energy.size(); ++frame)
        {
            auto right = sorted.lower_bound(static_cast<int>(frame));
            if (right == sorted.begin()) band->energy[frame] = dbToEnergy(right->second);
            else if (right == sorted.end()) band->energy[frame] = dbToEnergy(std::prev(right)->second);
            else
            {
                const auto left = std::prev(right);
                const auto span = std::max(1, right->first - left->first);
                const auto t = static_cast<double>(static_cast<int>(frame) - left->first) / span;
                band->energy[frame] = dbToEnergy(left->second + t * (right->second - left->second));
            }
        }
        (void) begin;
        return true;
    }

    static void smooth(ResidualMatrix& matrix, const std::vector<int>& bands,
                       std::size_t first, std::size_t last, int radius)
    {
        for (const auto bandIndex : bands)
        {
            auto* band = matrix.find(bandIndex);
            if (band == nullptr || band->energy.empty()) continue;
            const auto original = band->energy;
            for (auto frame = first; frame <= last && frame < band->energy.size(); ++frame)
            {
                double sum = 0.0; std::size_t count = 0;
                const auto begin = frame > static_cast<std::size_t>(radius) ? frame - radius : 0;
                const auto end = std::min(original.size() - 1, frame + static_cast<std::size_t>(radius));
                for (auto i = begin; i <= end; ++i) { sum += original[i]; ++count; }
                band->energy[frame] = count == 0 ? 0.0 : sum / static_cast<double>(count);
            }
        }
    }

    static void tilt(ResidualMatrix& matrix, const std::vector<int>& bands,
                     std::size_t first, std::size_t last, double step)
    {
        const auto center = static_cast<int>(bands.size() / 2);
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            auto* band = matrix.find(bands[i]);
            if (band == nullptr) continue;
            const auto offset = step * static_cast<double>(static_cast<int>(i) - center);
            for (auto frame = first; frame <= last && frame < band->energy.size(); ++frame)
                band->energy[frame] = dbToEnergy(energyToDb(band->energy[frame]) + offset);
        }
    }

    static void swap(ResidualMatrix& matrix, int a, int b, std::size_t first, std::size_t last)
    {
        auto* left = matrix.find(a); auto* right = matrix.find(b);
        if (left == nullptr || right == nullptr) return;
        for (auto frame = first; frame <= last; ++frame)
        {
            if (frame < left->energy.size() && frame < right->energy.size())
                std::swap(left->energy[frame], right->energy[frame]);
        }
    }

    static void redistribute(ResidualMatrix& matrix, const std::vector<int>& bands,
                             std::size_t first, std::size_t last)
    {
        for (auto frame = first; frame <= last; ++frame)
        {
            double total = 0.0; std::size_t count = 0;
            for (const auto bandIndex : bands)
            {
                const auto* band = matrix.find(bandIndex);
                if (band != nullptr && frame < band->energy.size()) { total += band->energy[frame]; ++count; }
            }
            if (count == 0) continue;
            const auto equal = total / static_cast<double>(count);
            for (const auto bandIndex : bands)
                if (auto* band = matrix.find(bandIndex); band != nullptr && frame < band->energy.size())
                    band->energy[frame] = equal;
        }
    }

    static ResidualEditResult copyBand(const ResidualMatrix& matrix,
                                       const std::vector<int>& bands,
                                       std::size_t first,
                                       std::size_t last,
                                       ResidualClipboard* clipboard)
    {
        if (clipboard == nullptr || bands.size() != 1) return failure("Copy requires one band and a clipboard.");
        const auto* band = matrix.find(bands.front());
        if (band == nullptr || band->energy.empty()) return failure("Selected band is unavailable.");
        clipboard->sourceBandIndex = bands.front();
        clipboard->energies.clear();
        for (auto frame = first; frame <= last && frame < band->energy.size(); ++frame)
            clipboard->energies.push_back(band->energy[frame]);
        bool finite = true;
        for (const auto value : clipboard->energies) finite = finite && finiteNonNegative(value);
        return { true, false, finite, 0.0, 0.0, finite ? std::string {} : "Clipboard contains invalid energy." };
    }

    static ResidualEditResult pasteBand(ResidualMatrix& matrix,
                                        const std::vector<int>& bands,
                                        std::size_t first,
                                        std::size_t last,
                                        const ResidualClipboard* clipboard)
    {
        if (clipboard == nullptr || !clipboard->valid() || bands.size() != 1)
            return failure("Paste requires one target band and a valid clipboard.");
        auto* band = matrix.find(bands.front());
        if (band == nullptr || band->energy.empty()) return failure("Target band is unavailable.");
        const auto before = selectedEnergy(matrix, bands, first, last);
        for (auto frame = first; frame <= last && frame < band->energy.size(); ++frame)
            band->energy[frame] = clipboard->energies[(frame - first) % clipboard->energies.size()];
        if (!validate(matrix)) return failure("Clipboard contains invalid energy.");
        return { true, true, true, before, selectedEnergy(matrix, bands, first, last), {} };
    }
};

} // namespace ats::engine
