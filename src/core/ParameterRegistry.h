#pragma once

#include "AtsPrimitives.h"

#include <memory>
#include <string_view>

namespace ats
{

enum class ParameterType : std::uint8_t { floatValue, integer, boolean, choice };
enum class ParameterSmoothing : std::uint8_t { none, onePole20Ms };
enum class ParameterUpdatePolicy : std::uint8_t { realtime, render, uiOnly };

struct ParameterDescriptor
{
    std::string_view id;
    std::string_view name;
    ParameterType type = ParameterType::floatValue;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    float interval = 0.0001f;
    float skew = 1.0f;
    const std::string_view* choices = nullptr;
    std::uint16_t choiceCount = 0;
    bool automatable = true;
    bool readOnly = false;
    std::string_view group;
    std::uint16_t denseIndex = 0;
    std::uint32_t hostNumericId = 0;
    ParameterSmoothing smoothing = ParameterSmoothing::none;
    ParameterUpdatePolicy updatePolicy = ParameterUpdatePolicy::render;
    bool participatesInState = true;
    bool participatesInPreset = true;

    [[nodiscard]] float clamp(float value) const noexcept;
    [[nodiscard]] float normalize(float value) const noexcept;
    [[nodiscard]] float denormalize(float normalized) const noexcept;
    [[nodiscard]] float snap(float value) const noexcept;
    [[nodiscard]] std::string text(float value) const;
    [[nodiscard]] bool fromText(std::string_view text, float& value) const;
};

class ParameterRegistry
{
public:
    [[nodiscard]] static const std::vector<ParameterDescriptor>& descriptors();
    [[nodiscard]] static const ParameterDescriptor* find(std::string_view id) noexcept;
    [[nodiscard]] static std::size_t indexOf(std::string_view id) noexcept;
};

struct ParameterChange
{
    std::uint16_t index = 0;
    float value = 0.0f;
    bool gestureBegin = false;
    bool gestureEnd = false;
};

class ParameterStore
{
public:
    ParameterStore();
    ParameterStore(const ParameterStore&) = delete;
    ParameterStore& operator=(const ParameterStore&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] float read(std::size_t index) const noexcept;
    [[nodiscard]] float read(std::string_view id) const noexcept;
    [[nodiscard]] bool write(std::size_t index, float value) noexcept;
    [[nodiscard]] bool write(std::string_view id, float value) noexcept;
    [[nodiscard]] std::vector<float> snapshot() const;
    void resetToDefaults() noexcept;

private:
    std::size_t size_ = 0;
    std::unique_ptr<std::atomic<float>[]> values_;
};

} // namespace ats
