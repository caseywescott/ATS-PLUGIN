#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ats
{

struct PartialCell
{
    double amplitude = 0.0;
    double frequencyHz = 1.0;
    double phase = 0.0;

    friend bool operator==(const PartialCell& a, const PartialCell& b) noexcept
    { return a.amplitude == b.amplitude && a.frequencyHz == b.frequencyHz && a.phase == b.phase; }
    friend bool operator!=(const PartialCell& a, const PartialCell& b) noexcept { return !(a == b); }
};

struct PartialMatrix
{
    int partialCount = 0;
    int frameCount = 0;
    std::vector<PartialCell> cells;
    std::uint64_t revision = 0;

    PartialMatrix() = default;
    PartialMatrix(int partials, int frames)
        : partialCount(std::max(0, partials)), frameCount(std::max(0, frames)),
          cells(static_cast<std::size_t>(partialCount) * static_cast<std::size_t>(frameCount)) {}

    [[nodiscard]] bool valid() const noexcept
    { return partialCount >= 0 && frameCount >= 0
             && cells.size() == static_cast<std::size_t>(partialCount) * static_cast<std::size_t>(frameCount); }

    [[nodiscard]] PartialCell& at(int partial, int frame) noexcept
    { return cells[static_cast<std::size_t>(partial) * static_cast<std::size_t>(frameCount)
                   + static_cast<std::size_t>(frame)]; }
    [[nodiscard]] const PartialCell& at(int partial, int frame) const noexcept
    { return cells[static_cast<std::size_t>(partial) * static_cast<std::size_t>(frameCount)
                   + static_cast<std::size_t>(frame)]; }
};

struct PartialSelection
{
    std::vector<int> partials;
    std::vector<int> frames;

    [[nodiscard]] bool empty() const noexcept { return partials.empty() || frames.empty(); }
    [[nodiscard]] bool contains(int partial, int frame) const noexcept
    {
        return std::find(partials.begin(), partials.end(), partial) != partials.end()
            && std::find(frames.begin(), frames.end(), frame) != frames.end();
    }
};

struct PartialPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct PartialViewport
{
    double timeMin = 0.0;
    double timeMax = 1.0;
    double frequencyMinHz = 20.0;
    double frequencyMaxHz = 20000.0;
};

struct PartialClipboard
{
    int partialCount = 0;
    int frameCount = 0;
    std::vector<PartialCell> cells;

    [[nodiscard]] bool valid() const noexcept
    { return partialCount > 0 && frameCount > 0
             && cells.size() == static_cast<std::size_t>(partialCount) * static_cast<std::size_t>(frameCount); }
    [[nodiscard]] const PartialCell& at(int partial, int frame) const noexcept
    { return cells[static_cast<std::size_t>(partial) * static_cast<std::size_t>(frameCount)
                   + static_cast<std::size_t>(frame)]; }
};

struct PartialModifiers
{
    bool shift = false;
    bool command = false;
    bool alt = false;
};

enum class PartialEditKind : std::uint8_t
{
    select,
    box,
    lasso,
    writeCell,
    scaleSelection,
    drawDb,
    drawFrequency,
    clipboardCopy,
    clipboardPaste,
    onset,
    gainBox,
    seek,
    pan,
    zoom,
    keyboard
};

struct PartialEditCommand
{
    PartialEditKind kind = PartialEditKind::select;
    PartialModifiers modifiers{};
    PartialSelection selection{};
    std::vector<PartialPoint> points{};
    int partial = -1;
    int frame = -1;
    int firstPartial = -1;
    int lastPartial = -1;
    int firstFrame = -1;
    int lastFrame = -1;
    double amplitude = 0.0;
    double frequencyHz = 1.0;
    double value = 1.0;
    double frequencyMinHz = 0.0;
    double frequencyMaxHz = 0.0;
    int onsetFrame = -1;
    int keyNote = -1;
    float keyVelocity = 0.0f;
    bool keyDown = false;
    double anchor = 0.5;
};

struct PartialEditResult
{
    bool accepted = false;
    bool changed = false;
    bool pointerCaptured = false;
    bool focusRestored = false;
    std::string error;
    PartialSelection selection{};
    PartialViewport viewport{};
    PartialClipboard clipboard{};
    int onsetFrame = -1;
    double seekNorm = -1.0;
    int keyNote = -1;
    float keyVelocity = 0.0f;
    bool keyDown = false;
    std::uint64_t revision = 0;
};

class PartialEditService
{
public:
    [[nodiscard]] const PartialSelection& selection() const noexcept { return selection_; }
    [[nodiscard]] const PartialClipboard& clipboard() const noexcept { return clipboard_; }
    [[nodiscard]] const PartialViewport& viewport() const noexcept { return viewport_; }
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }

    PartialEditResult apply(PartialMatrix& matrix, const PartialEditCommand& command)
    {
        PartialEditResult result;
        result.selection = selection_;
        result.viewport = viewport_;
        result.clipboard = clipboard_;
        result.revision = matrix.revision;

        if (!matrix.valid())
        {
            result.error = "Partial matrix is invalid.";
            return result;
        }

        PartialMatrix candidate = matrix;
        bool modelChanged = false;
        switch (command.kind)
        {
            case PartialEditKind::select:
                selectCell(matrix, command, result);
                break;
            case PartialEditKind::box:
                selectBox(matrix, command, result);
                break;
            case PartialEditKind::lasso:
                selectLasso(matrix, command, result);
                break;
            case PartialEditKind::writeCell:
                modelChanged = writeCell(candidate, command, result);
                break;
            case PartialEditKind::scaleSelection:
                modelChanged = scale(candidate, command, result);
                break;
            case PartialEditKind::drawDb:
                modelChanged = drawDb(candidate, command, result);
                break;
            case PartialEditKind::drawFrequency:
                modelChanged = drawFrequency(candidate, command, result);
                break;
            case PartialEditKind::clipboardCopy:
                copy(matrix, result);
                break;
            case PartialEditKind::clipboardPaste:
                modelChanged = paste(candidate, result);
                break;
            case PartialEditKind::onset:
                result.onsetFrame = std::clamp(command.onsetFrame, 0, std::max(0, matrix.frameCount - 1));
                result.accepted = command.onsetFrame >= 0 && matrix.frameCount > 0;
                break;
            case PartialEditKind::gainBox:
                modelChanged = gainBox(candidate, command, result);
                break;
            case PartialEditKind::seek:
                result.seekNorm = std::clamp(command.value, 0.0, 1.0);
                result.accepted = std::isfinite(command.value);
                if (!result.accepted) result.error = "Seek position must be finite.";
                break;
            case PartialEditKind::pan:
                pan(command, result);
                break;
            case PartialEditKind::zoom:
                zoom(command, result);
                break;
            case PartialEditKind::keyboard:
                result.accepted = command.keyNote >= 0 && command.keyNote <= 127
                                && std::isfinite(command.keyVelocity)
                                && command.keyVelocity >= 0.0f && command.keyVelocity <= 1.0f;
                result.keyNote = command.keyNote;
                result.keyVelocity = command.keyVelocity;
                result.keyDown = command.keyDown;
                if (!result.accepted) result.error = "Keyboard note or velocity is invalid.";
                break;
        }

        if (!result.error.empty())
            return result;

        if (modelChanged)
        {
            const auto before = matrix;
            candidate.revision = matrix.revision + 1;
            pushUndo(before, candidate, command.kind);
            matrix = std::move(candidate);
            result.changed = true;
            result.revision = matrix.revision;
        }
        result.accepted = true;
        result.selection = selection_;
        result.viewport = viewport_;
        result.clipboard = clipboard_;
        return result;
    }

    PartialEditResult undo(PartialMatrix& matrix)
    {
        PartialEditResult result;
        result.selection = selection_;
        result.viewport = viewport_;
        if (undo_.empty()) { result.error = "Nothing to undo."; return result; }
        redo_.push_back({ matrix, undo_.back().after, undo_.back().label });
        matrix = undo_.back().before;
        undo_.pop_back();
        result.accepted = result.changed = true;
        result.revision = matrix.revision;
        return result;
    }

    PartialEditResult redo(PartialMatrix& matrix)
    {
        PartialEditResult result;
        result.selection = selection_;
        result.viewport = viewport_;
        if (redo_.empty()) { result.error = "Nothing to redo."; return result; }
        undo_.push_back({ matrix, redo_.back().after, redo_.back().label });
        matrix = redo_.back().after;
        redo_.pop_back();
        result.accepted = result.changed = true;
        result.revision = matrix.revision;
        return result;
    }

private:
    struct Transaction { PartialMatrix before, after; std::string label; };

    static void normalize(PartialSelection& selection)
    {
        auto clean = [](std::vector<int>& values) {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        };
        clean(selection.partials);
        clean(selection.frames);
    }

    static PartialSelection clampedSelection(const PartialMatrix& matrix, PartialSelection selection)
    {
        selection.partials.erase(std::remove_if(selection.partials.begin(), selection.partials.end(),
                                                [&](int p) { return p < 0 || p >= matrix.partialCount; }),
                                selection.partials.end());
        selection.frames.erase(std::remove_if(selection.frames.begin(), selection.frames.end(),
                                              [&](int f) { return f < 0 || f >= matrix.frameCount; }),
                              selection.frames.end());
        normalize(selection);
        return selection;
    }

    void setSelection(PartialSelection next, const PartialEditCommand& command)
    {
        normalize(next);
        if (command.modifiers.shift)
        {
            selection_.partials.insert(selection_.partials.end(), next.partials.begin(), next.partials.end());
            selection_.frames.insert(selection_.frames.end(), next.frames.begin(), next.frames.end());
            normalize(selection_);
        }
        else selection_ = std::move(next);
    }

    static PartialSelection rectangular(int p0, int p1, int f0, int f1)
    {
        PartialSelection out;
        if (p0 > p1) std::swap(p0, p1);
        if (f0 > f1) std::swap(f0, f1);
        for (int p = p0; p <= p1; ++p) out.partials.push_back(p);
        for (int f = f0; f <= f1; ++f) out.frames.push_back(f);
        return out;
    }

    void selectCell(const PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result)
    {
        const bool valid = command.partial >= 0 && command.partial < matrix.partialCount
                        && command.frame >= 0 && command.frame < matrix.frameCount;
        if (!valid) { result.error = "Partial or frame index is out of range."; return; }
        setSelection({ { command.partial }, { command.frame } }, command);
        result.pointerCaptured = true;
    }

    void selectBox(const PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result)
    {
        const auto next = clampedSelection(matrix, rectangular(command.firstPartial, command.lastPartial,
                                                                command.firstFrame, command.lastFrame));
        if (next.empty()) { result.error = "Selection box is empty."; return; }
        setSelection(next, command);
        result.pointerCaptured = true;
    }

    void selectLasso(const PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result)
    {
        if (command.points.size() < 3) { result.error = "Lasso needs at least three points."; return; }
        PartialSelection next;
        auto inside = [&](double x, double y) {
            bool hit = false;
            for (std::size_t i = 0, j = command.points.size() - 1; i < command.points.size(); j = i++)
            {
                const auto& a = command.points[i];
                const auto& b = command.points[j];
                if (((a.y > y) != (b.y > y)) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                    hit = !hit;
            }
            return hit;
        };
        for (int p = 0; p < matrix.partialCount; ++p)
            for (int f = 0; f < matrix.frameCount; ++f)
                if (inside(static_cast<double>(f), static_cast<double>(p)))
                {
                    next.partials.push_back(p);
                    next.frames.push_back(f);
                }
        next = clampedSelection(matrix, std::move(next));
        if (next.empty()) { result.error = "Lasso selected no cells."; return; }
        setSelection(next, command);
        result.pointerCaptured = true;
    }

    static bool validCell(const PartialCell& cell) noexcept
    {
        return std::isfinite(cell.amplitude) && cell.amplitude >= 0.0
            && std::isfinite(cell.frequencyHz) && cell.frequencyHz > 1e-6
            && std::isfinite(cell.phase);
    }

    static bool finiteNonNegative(double value) noexcept { return std::isfinite(value) && value >= 0.0; }

    bool writeCell(PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result) const
    {
        if (command.partial < 0 || command.partial >= matrix.partialCount
            || command.frame < 0 || command.frame >= matrix.frameCount)
        { result.error = "Partial or frame index is out of range."; return false; }
        const PartialCell cell { command.amplitude, command.frequencyHz, matrix.at(command.partial, command.frame).phase };
        if (!validCell(cell)) { result.error = "Amplitude and frequency must be finite and valid."; return false; }
        if (matrix.at(command.partial, command.frame) == cell) return false;
        matrix.at(command.partial, command.frame) = cell;
        return true;
    }

    template <typename Function>
    static bool forSelection(PartialMatrix& matrix, const PartialSelection& selection, Function&& function)
    {
        bool changed = false;
        for (const int p : selection.partials)
            for (const int f : selection.frames)
                if (p >= 0 && p < matrix.partialCount && f >= 0 && f < matrix.frameCount)
                    changed = function(matrix.at(p, f), p, f) || changed;
        return changed;
    }

    bool scale(PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result) const
    {
        if (!finiteNonNegative(command.value)) { result.error = "Scale must be finite and non-negative."; return false; }
        if (selection_.empty()) { result.error = "No partial cells are selected."; return false; }
        return forSelection(matrix, selection_, [&](PartialCell& cell, int, int) {
            const double next = cell.amplitude * command.value;
            if (!finiteNonNegative(next)) { result.error = "Scale would produce an invalid amplitude."; return false; }
            if (cell.amplitude == next) return false;
            cell.amplitude = next;
            return true;
        });
    }

    double dbAt(const PartialEditCommand& command, int frame) const
    {
        if (command.points.empty()) return command.value;
        if (frame <= command.points.front().x) return command.points.front().y;
        if (frame >= command.points.back().x) return command.points.back().y;
        for (std::size_t i = 1; i < command.points.size(); ++i)
            if (frame <= command.points[i].x)
            {
                const auto& a = command.points[i - 1];
                const auto& b = command.points[i];
                const double t = (frame - a.x) / (b.x - a.x);
                return a.y + (b.y - a.y) * std::clamp(t, 0.0, 1.0);
            }
        return command.points.back().y;
    }

    bool drawDb(PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result) const
    {
        if (selection_.empty()) { result.error = "No partial cells are selected."; return false; }
        return forSelection(matrix, selection_, [&](PartialCell& cell, int, int frame) {
            const double db = dbAt(command, frame);
            const double next = cell.amplitude * std::pow(10.0, db / 20.0);
            if (!std::isfinite(db) || !finiteNonNegative(next)) { result.error = "dB draw would produce an invalid amplitude."; return false; }
            if (cell.amplitude == next) return false;
            cell.amplitude = next;
            return true;
        });
    }

    bool drawFrequency(PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result) const
    {
        if (selection_.empty() || command.points.empty()) { result.error = "Frequency draw needs a selection and points."; return false; }
        return forSelection(matrix, selection_, [&](PartialCell& cell, int, int frame) {
            double next = command.points.front().y;
            for (std::size_t i = 1; i < command.points.size(); ++i)
                if (frame >= command.points[i - 1].x && frame <= command.points[i].x)
                {
                    const auto& a = command.points[i - 1];
                    const auto& b = command.points[i];
                    const double t = (frame - a.x) / (b.x - a.x);
                    next = a.y + (b.y - a.y) * std::clamp(t, 0.0, 1.0);
                    break;
                }
            if (!std::isfinite(next) || next <= 1e-6) { result.error = "Frequency draw would produce an invalid frequency."; return false; }
            if (cell.frequencyHz == next) return false;
            cell.frequencyHz = next;
            return true;
        });
    }

    void copy(const PartialMatrix& matrix, PartialEditResult& result)
    {
        if (selection_.empty()) { result.error = "No partial cells are selected."; return; }
        clipboard_.partialCount = static_cast<int>(selection_.partials.size());
        clipboard_.frameCount = static_cast<int>(selection_.frames.size());
        clipboard_.cells.clear();
        for (const int p : selection_.partials)
            for (const int f : selection_.frames)
                clipboard_.cells.push_back(matrix.at(p, f));
        result.accepted = true;
    }

    bool paste(PartialMatrix& matrix, PartialEditResult& result) const
    {
        if (!clipboard_.valid() || selection_.empty()) { result.error = "Clipboard or selection is empty."; return false; }
        if (static_cast<int>(selection_.partials.size()) != clipboard_.partialCount
            || static_cast<int>(selection_.frames.size()) != clipboard_.frameCount)
        { result.error = "Clipboard shape does not match the selection."; return false; }
        bool changed = false;
        std::size_t index = 0;
        for (const int p : selection_.partials)
            for (const int f : selection_.frames)
            {
                if (!validCell(clipboard_.cells[index])) { result.error = "Clipboard contains an invalid cell."; return false; }
                changed = matrix.at(p, f) != clipboard_.cells[index] || changed;
                matrix.at(p, f) = clipboard_.cells[index++];
            }
        return changed;
    }

    bool gainBox(PartialMatrix& matrix, const PartialEditCommand& command, PartialEditResult& result) const
    {
        if (!finiteNonNegative(command.value)) { result.error = "Gain must be finite and non-negative."; return false; }
        const int p0 = std::max(0, command.firstPartial);
        const int p1 = std::min(matrix.partialCount - 1, command.lastPartial);
        const int f0 = std::max(0, command.firstFrame);
        const int f1 = std::min(matrix.frameCount - 1, command.lastFrame);
        if (p0 > p1 || f0 > f1) { result.error = "Gain box is empty."; return false; }
        return forSelection(matrix, rectangular(p0, p1, f0, f1), [&](PartialCell& cell, int, int) {
            if (command.frequencyMinHz > 0.0 && cell.frequencyHz < command.frequencyMinHz) return false;
            if (command.frequencyMaxHz > 0.0 && cell.frequencyHz > command.frequencyMaxHz) return false;
            const double next = cell.amplitude * command.value;
            if (!finiteNonNegative(next)) { result.error = "Gain would produce an invalid amplitude."; return false; }
            if (cell.amplitude == next) return false;
            cell.amplitude = next;
            return true;
        });
    }

    void pan(const PartialEditCommand& command, PartialEditResult& result)
    {
        if (!std::isfinite(command.points.size() > 0 ? command.points[0].x : 0.0)
            || !std::isfinite(command.points.size() > 0 ? command.points[0].y : 0.0))
        { result.error = "Pan delta must be finite."; return; }
        const double dx = command.points.empty() ? 0.0 : command.points[0].x;
        const double dy = command.points.empty() ? 0.0 : command.points[0].y;
        viewport_.timeMin += dx; viewport_.timeMax += dx;
        viewport_.frequencyMinHz = std::max(1e-6, viewport_.frequencyMinHz + dy);
        viewport_.frequencyMaxHz = std::max(viewport_.frequencyMinHz + 1e-6, viewport_.frequencyMaxHz + dy);
        result.accepted = true;
    }

    void zoom(const PartialEditCommand& command, PartialEditResult& result)
    {
        if (!std::isfinite(command.value) || command.value <= 0.0) { result.error = "Zoom must be positive and finite."; return; }
        const double anchor = std::clamp(command.anchor, 0.0, 1.0);
        const double t = viewport_.timeMin + (viewport_.timeMax - viewport_.timeMin) * anchor;
        const double f = viewport_.frequencyMinHz + (viewport_.frequencyMaxHz - viewport_.frequencyMinHz) * anchor;
        const double timeHalf = (viewport_.timeMax - viewport_.timeMin) / (2.0 * command.value);
        const double freqHalf = (viewport_.frequencyMaxHz - viewport_.frequencyMinHz) / (2.0 * command.value);
        viewport_.timeMin = t - timeHalf; viewport_.timeMax = t + timeHalf;
        viewport_.frequencyMinHz = std::max(1e-6, f - freqHalf);
        viewport_.frequencyMaxHz = std::max(viewport_.frequencyMinHz + 1e-6, f + freqHalf);
        result.accepted = true;
    }

    void pushUndo(const PartialMatrix& before, const PartialMatrix& after, PartialEditKind kind)
    {
        undo_.push_back({ before, after, std::to_string(static_cast<int>(kind)) });
        redo_.clear();
    }

    PartialSelection selection_{};
    PartialClipboard clipboard_{};
    PartialViewport viewport_{};
    std::vector<Transaction> undo_{};
    std::vector<Transaction> redo_{};
};

} // namespace ats
