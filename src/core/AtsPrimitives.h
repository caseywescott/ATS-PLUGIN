#pragma once

// Framework-free contracts shared by the engine and host adapters.
// These types deliberately own no host, UI, filesystem, or SDK objects.

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ats
{

struct AudioBufferView
{
    float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint32_t frameCount = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return (channelCount == 0 || channels != nullptr) &&
               (channelCount == 0 || frameCount == 0 || channels[0] != nullptr);
    }

    void clear() const noexcept
    {
        if (channels == nullptr)
            return;
        for (std::uint32_t ch = 0; ch < channelCount; ++ch)
            if (channels[ch] != nullptr)
                std::fill_n(channels[ch], frameCount, 0.0f);
    }

    void add(const AudioBufferView& source, float gain = 1.0f) const noexcept
    {
        const auto count = std::min(channelCount, source.channelCount);
        const auto frames = std::min(frameCount, source.frameCount);
        for (std::uint32_t ch = 0; ch < count; ++ch)
            if (channels[ch] != nullptr && source.channels[ch] != nullptr)
                for (std::uint32_t i = 0; i < frames; ++i)
                    channels[ch][i] += source.channels[ch][i] * gain;
    }
};

struct MidiEvent
{
    enum class Type : std::uint8_t { noteOn, noteOff, allNotesOff };

    std::uint32_t sampleOffset = 0;
    Type type = Type::noteOff;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
};

struct MidiEventList
{
    const MidiEvent* events = nullptr;
    std::uint32_t count = 0;

    [[nodiscard]] const MidiEvent* begin() const noexcept { return events; }
    [[nodiscard]] const MidiEvent* end() const noexcept { return events == nullptr ? nullptr : events + count; }
};

struct Error
{
    std::string code;
    std::string text;

    [[nodiscard]] explicit operator bool() const noexcept { return !code.empty() || !text.empty(); }
};

struct Path
{
    std::string utf8;

    [[nodiscard]] bool empty() const noexcept { return utf8.empty(); }
    [[nodiscard]] const std::string& string() const noexcept { return utf8; }
};

class ByteWriter
{
public:
    void reserve(std::size_t size) { bytes_.reserve(size); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<std::byte> take() && noexcept { return std::move(bytes_); }

    void writeU8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void writeU16(std::uint16_t value)
    {
        writeU8(static_cast<std::uint8_t>(value));
        writeU8(static_cast<std::uint8_t>(value >> 8));
    }
    void writeU32(std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            writeU8(static_cast<std::uint8_t>(value >> shift));
    }
    void writeU64(std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            writeU8(static_cast<std::uint8_t>(value >> shift));
    }
    void writeI32(std::int32_t value) { writeU32(static_cast<std::uint32_t>(value)); }
    void writeF32(float value)
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t), "unexpected float representation");
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        writeU32(bits);
    }
    void writeF64(double value)
    {
        static_assert(sizeof(double) == sizeof(std::uint64_t), "unexpected double representation");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        writeU64(bits);
    }
    void writeBytes(const void* data, std::size_t size)
    {
        const auto* first = static_cast<const std::byte*>(data);
        bytes_.insert(bytes_.end(), first, first + size);
    }

private:
    std::vector<std::byte> bytes_;
};

class ByteReader
{
public:
    ByteReader(const std::byte* data, std::size_t size) noexcept : data_(data), size_(size) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return position_ <= size_ ? size_ - position_ : 0; }
    bool readU8(std::uint8_t& value) noexcept
    {
        if (!require(1)) return false;
        value = std::to_integer<std::uint8_t>(data_[position_++]);
        return true;
    }
    bool readU16(std::uint16_t& value) noexcept
    {
        std::uint8_t a = 0, b = 0;
        if (!readU8(a) || !readU8(b)) return false;
        value = static_cast<std::uint16_t>(a) | (static_cast<std::uint16_t>(b) << 8);
        return true;
    }
    bool readU32(std::uint32_t& value) noexcept
    {
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!readU8(byte)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }
    bool readU64(std::uint64_t& value) noexcept
    {
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!readU8(byte)) return false;
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }
    bool readI32(std::int32_t& value) noexcept
    {
        std::uint32_t bits = 0;
        if (!readU32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool readF32(float& value) noexcept
    {
        std::uint32_t bits = 0;
        if (!readU32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool readF64(double& value) noexcept
    {
        std::uint64_t bits = 0;
        if (!readU64(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool readBytes(void* destination, std::size_t size) noexcept
    {
        if (!require(size)) return false;
        std::memcpy(destination, data_ + position_, size);
        position_ += size;
        return true;
    }

private:
    bool require(std::size_t count) noexcept
    {
        if (count > size_ - std::min(position_, size_))
        {
            ok_ = false;
            position_ = size_;
            return false;
        }
        return true;
    }

    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    bool ok_ = true;
};

struct JobGeneration
{
    std::uint64_t value = 0;
    friend bool operator==(JobGeneration a, JobGeneration b) noexcept { return a.value == b.value; }
    friend bool operator!=(JobGeneration a, JobGeneration b) noexcept { return !(a == b); }
};

class GenerationSource
{
public:
    [[nodiscard]] JobGeneration next() noexcept
    {
        return { generation_.fetch_add(1, std::memory_order_acq_rel) + 1 };
    }
    [[nodiscard]] JobGeneration current() const noexcept
    {
        return { generation_.load(std::memory_order_acquire) };
    }

private:
    std::atomic<std::uint64_t> generation_{0};
};

class CancellationToken
{
public:
    CancellationToken() : state_(std::make_shared<State>()) {}

    void cancel() noexcept { state_->cancelled.store(true, std::memory_order_release); }
    [[nodiscard]] bool isCancelled() const noexcept
    { return state_->cancelled.load(std::memory_order_acquire); }

private:
    struct State
    {
        std::atomic<bool> cancelled { false };
    };

    explicit CancellationToken(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
    std::shared_ptr<State> state_;

    friend class CancellationSource;
};

class CancellationSource
{
public:
    CancellationSource() : state_(std::make_shared<CancellationToken::State>()) {}

    [[nodiscard]] CancellationToken token() const noexcept
    { return CancellationToken { state_ }; }

    void cancel() noexcept { state_->cancelled.store(true, std::memory_order_release); }
    [[nodiscard]] bool isCancelled() const noexcept
    { return state_->cancelled.load(std::memory_order_acquire); }

private:
    std::shared_ptr<CancellationToken::State> state_;
};

template <typename T>
struct JobResult
{
    enum class Status : std::uint8_t { success, cancelled, failed };

    JobGeneration generation{};
    Status status = Status::failed;
    T value{};
    Error error{};
};

class OnePoleSmoother
{
public:
    void prepare(double sampleRate, double timeMs) noexcept
    {
        const auto samples = std::max(1.0, sampleRate * timeMs * 0.001);
        coefficient_ = static_cast<float>(std::exp(-1.0 / samples));
        prepared_ = true;
    }
    void reset(float value) noexcept { current_ = target_ = value; }
    void setTarget(float value) noexcept { target_ = value; }
    [[nodiscard]] float process() noexcept
    {
        if (!prepared_) return target_;
        current_ = target_ + coefficient_ * (current_ - target_);
        return current_;
    }
    [[nodiscard]] float current() const noexcept { return current_; }

private:
    float coefficient_ = 0.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
    bool prepared_ = false;
};

class MonotonicClock
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    [[nodiscard]] static TimePoint now() noexcept { return std::chrono::steady_clock::now(); }
    [[nodiscard]] static std::int64_t millisecondsSince(TimePoint start, TimePoint end) noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }
};

} // namespace ats
