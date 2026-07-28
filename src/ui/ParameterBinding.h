#pragma once

#include "../core/ParameterRegistry.h"
#include "EditorHost.h"

#include <functional>
#include <string>

namespace ats::ui
{

// The widget-facing half of a parameter connection. It emits the same typed
// action shape for host automation and UI gestures; the store remains the
// model owner and is updated by ParameterActionDispatcher.
class ParameterBinding
{
public:
    ParameterBinding(ats::ParameterStore& store, std::string id, ActionSink& sink)
        : store_(store), id_(std::move(id)), sink_(sink) {}

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] bool valid() const noexcept { return ats::ParameterRegistry::find(id_) != nullptr; }
    [[nodiscard]] float value() const noexcept { return store_.read(id_); }
    [[nodiscard]] bool gestureActive() const noexcept { return gestureActive_; }

    bool beginGesture(float value)
    {
        if (gestureActive_) return false;
        if (!emit(value, true, false)) return false;
        gestureActive_ = true;
        return true;
    }

    bool updateGesture(float value)
    {
        return gestureActive_ && emit(value, false, false);
    }

    bool endGesture(float value)
    {
        if (!gestureActive_) return false;
        const bool emitted = emit(value, false, true);
        gestureActive_ = false;
        return emitted;
    }

    void cancelGesture() noexcept { gestureActive_ = false; }

    bool setValue(float value)
    {
        return emit(value, false, false);
    }

private:
    bool emit(float value, bool begin, bool end)
    {
        const auto* descriptor = ats::ParameterRegistry::find(id_);
        if (descriptor == nullptr || descriptor->readOnly) return false;
        sink_.dispatch(SetParameterAction { id_, descriptor->snap(value), begin, end });
        return true;
    }

    ats::ParameterStore& store_;
    std::string id_;
    ActionSink& sink_;
    bool gestureActive_ = false;
};

// The single commit point for parameter changes. A host wrapper can use this
// dispatcher for automation and the editor can use it for widget actions.
class ParameterActionDispatcher final : public ActionSink
{
public:
    using AcceptedAction = std::function<void(const SetParameterAction&)>;

    explicit ParameterActionDispatcher(ats::ParameterStore& store, AcceptedAction accepted = {})
        : store_(store), accepted_(std::move(accepted)) {}

    void dispatch(EditorAction action) override
    {
        if (auto* parameter = std::get_if<SetParameterAction>(&action))
        {
            const auto* descriptor = ats::ParameterRegistry::find(parameter->id);
            if (descriptor == nullptr || descriptor->readOnly) return;
            const auto value = descriptor->snap(parameter->value);
            if (!store_.write(parameter->id, value)) return;
            if (accepted_ != nullptr)
                accepted_(SetParameterAction { parameter->id, value, parameter->begin, parameter->end });
        }
    }

private:
    ats::ParameterStore& store_;
    AcceptedAction accepted_;
};

} // namespace ats::ui
