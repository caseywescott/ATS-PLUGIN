#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ats::ui
{

struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool contains(Point point) const noexcept
    { return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height; }
    [[nodiscard]] Point toLocal(Point point) const noexcept { return { point.x - x, point.y - y }; }
};

struct PointerEvent
{
    enum class Type : std::uint8_t { down, move, up, cancel };
    Type type = Type::move;
    Point windowPosition{};
    Point localPosition{};
    Point movement{};
    std::uint32_t pointerId = 0;
    bool shift = false;
    bool control = false;
    std::uint64_t targetId = 0;
};

class Frame
{
public:
    using PointerHandler = std::function<void(const PointerEvent&)>;

    explicit Frame(std::uint64_t id) : id_(id) {}
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    void setBounds(Rect bounds) noexcept { bounds_ = bounds; }
    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    void setClickThrough(bool clickThrough) noexcept { clickThrough_ = clickThrough; }
    void setZOrder(int z) noexcept { zOrder_ = z; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool clickThrough() const noexcept { return clickThrough_; }
    [[nodiscard]] int zOrder() const noexcept { return zOrder_; }
    void onPointer(PointerHandler handler) { handler_ = std::move(handler); }
    void receive(const PointerEvent& event) const { if (handler_) handler_(event); }

private:
    std::uint64_t id_ = 0;
    Rect bounds_{};
    bool enabled_ = true;
    bool clickThrough_ = false;
    int zOrder_ = 0;
    PointerHandler handler_;
};

class Dispatcher
{
public:
    Frame& root() noexcept { return root_; }

    void add(Frame& frame, Frame& parent)
    { parentFor(frame) = &parent; children_.push_back(&frame); }

    [[nodiscard]] Frame* hitTest(Point windowPosition) const noexcept
    {
        Frame* best = nullptr;
        int bestZ = -1;
        for (auto* frame : children_)
        {
            if (frame == &root_ || !frame->enabled() || frame->clickThrough() || !globalBounds(frame).contains(windowPosition))
                continue;
            if (frame->zOrder() >= bestZ)
            {
                best = frame;
                bestZ = frame->zOrder();
            }
        }
        return best;
    }

    void dispatch(PointerEvent event)
    {
        Frame* target = capture_ != nullptr ? capture_ : hitTest(event.windowPosition);
        if (target == nullptr) return;
        event.targetId = target->id();
        event.localPosition = globalBounds(target).toLocal(event.windowPosition);
        if (event.type == PointerEvent::Type::down)
        {
            capture_ = target;
            focus_ = target;
        }
        target->receive(event);
        if (event.type == PointerEvent::Type::up || event.type == PointerEvent::Type::cancel)
            capture_ = nullptr;
    }

    void setPopup(Frame* popup) noexcept { popup_ = popup; focus_ = popup; }
    void dismissPopup() noexcept { popup_ = nullptr; focus_ = nullptr; }
    // Called before the native editor is detached or destroyed. No input or
    // overlay ownership may survive the backend window lifetime.
    void resetForTeardown() noexcept
    {
        capture_ = nullptr;
        focus_ = nullptr;
        popup_ = nullptr;
    }
    [[nodiscard]] Frame* popup() const noexcept { return popup_; }
    [[nodiscard]] Frame* focused() const noexcept { return focus_; }
    [[nodiscard]] Frame* captured() const noexcept { return capture_; }

private:
    Frame*& parentFor(Frame& frame)
    {
        for (auto& link : parents_)
            if (link.first == &frame) return link.second;
        parents_.push_back({ &frame, &root_ });
        return parents_.back().second;
    }

    [[nodiscard]] Rect globalBounds(const Frame* frame) const noexcept
    {
        Rect result = frame->bounds();
        const Frame* current = frame;
        while (true)
        {
            const Frame* parent = nullptr;
            for (const auto& link : parents_)
                if (link.first == current) { parent = link.second; break; }
            if (parent == nullptr || parent == &root_) break;
            result.x += parent->bounds().x;
            result.y += parent->bounds().y;
            current = parent;
        }
        return result;
    }

    Frame root_ { 1 };
    std::vector<Frame*> children_;
    std::vector<std::pair<Frame*, Frame*>> parents_;
    Frame* capture_ = nullptr;
    Frame* focus_ = nullptr;
    Frame* popup_ = nullptr;
};

} // namespace ats::ui
