#pragma once

#include "EditorHost.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace ats::ui
{

class UiActionDispatcher
{
public:
    explicit UiActionDispatcher(std::size_t capacity = 256) : capacity_(capacity) {}

    bool submit(EditorAction action)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) return false;
        queue_.push_back(std::move(action));
        return true;
    }

    [[nodiscard]] std::optional<EditorAction> tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        auto action = std::move(queue_.front());
        queue_.pop_front();
        return action;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<EditorAction> queue_;
};

class ActionResultDispatcher
{
public:
    explicit ActionResultDispatcher(std::size_t capacity = 256) : capacity_(capacity) {}

    bool publish(ActionResult result)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (results_.size() >= capacity_) return false;
        results_.push_back(std::move(result));
        return true;
    }

    [[nodiscard]] std::optional<ActionResult> tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (results_.empty()) return std::nullopt;
        auto result = std::move(results_.front());
        results_.pop_front();
        return result;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return results_.size();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<ActionResult> results_;
};

} // namespace ats::ui
