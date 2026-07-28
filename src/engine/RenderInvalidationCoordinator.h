#pragma once

#include "../core/AtsPrimitives.h"

#include <array>
#include <chrono>
#include <vector>

namespace ats::engine
{

enum class InvalidationKind : std::uint8_t
{
    frequencyShape,
    morphDraft,
    morphFinalize,
    parameterUndo,
    preview
};

struct InvalidationRequest
{
    InvalidationKind kind = InvalidationKind::preview;
    JobGeneration generation{};
    MonotonicClock::TimePoint due{};
};

class RenderInvalidationCoordinator
{
public:
    RenderInvalidationCoordinator()
        : slots_ {{ { InvalidationKind::frequencyShape }, { InvalidationKind::morphDraft },
                    { InvalidationKind::morphFinalize }, { InvalidationKind::parameterUndo },
                    { InvalidationKind::preview } }} {}

    static constexpr std::chrono::milliseconds kFrequencyShapeDelay { 80 };
    static constexpr std::chrono::milliseconds kMorphDraftDelay { 120 };
    static constexpr std::chrono::milliseconds kMorphFinalizeDelay { 350 };
    static constexpr std::chrono::milliseconds kParameterUndoDelay { 500 };

    [[nodiscard]] JobGeneration request(InvalidationKind kind, MonotonicClock::TimePoint now)
    {
        auto& slot = slots_[index(kind)];
        const auto generation = generations_.next();
        slot.pending = true;
        slot.generation = generation;
        slot.due = now + delay(kind);
        return generation;
    }

    [[nodiscard]] std::vector<InvalidationRequest> poll(MonotonicClock::TimePoint now)
    {
        std::vector<InvalidationRequest> ready;
        for (auto& slot : slots_)
            if (slot.pending && slot.due <= now)
            {
                ready.push_back({ slot.kind, slot.generation, slot.due });
                slot.pending = false;
            }
        return ready;
    }

    void invalidate(InvalidationKind kind) noexcept
    {
        auto& slot = slots_[index(kind)];
        slot.pending = false;
        slot.generation = generations_.next();
    }

    [[nodiscard]] bool isCurrent(InvalidationKind kind, JobGeneration generation) const noexcept
    {
        return generationsFor(kind).value == generation.value;
    }

    [[nodiscard]] bool pending(InvalidationKind kind) const noexcept { return slots_[index(kind)].pending; }

private:
    struct Slot
    {
        InvalidationKind kind = InvalidationKind::preview;
        bool pending = false;
        JobGeneration generation{};
        MonotonicClock::TimePoint due{};
    };

    [[nodiscard]] static constexpr std::size_t index(InvalidationKind kind) noexcept
    { return static_cast<std::size_t>(kind); }

    [[nodiscard]] static constexpr std::chrono::milliseconds delay(InvalidationKind kind) noexcept
    {
        switch (kind)
        {
        case InvalidationKind::frequencyShape: return kFrequencyShapeDelay;
        case InvalidationKind::morphDraft: return kMorphDraftDelay;
        case InvalidationKind::morphFinalize: return kMorphFinalizeDelay;
        case InvalidationKind::parameterUndo: return kParameterUndoDelay;
        case InvalidationKind::preview: return std::chrono::milliseconds(0);
        }
        return std::chrono::milliseconds(0);
    }

    [[nodiscard]] JobGeneration generationsFor(InvalidationKind kind) const noexcept
    { return slots_[index(kind)].generation; }

    GenerationSource generations_;
    std::array<Slot, 5> slots_;
};

} // namespace ats::engine
