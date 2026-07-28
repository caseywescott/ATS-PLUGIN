#pragma once

#include "AtsPrimitives.h"

#include <deque>
#include <optional>

namespace ats
{

// Snapshot history is intentionally value-based. The engine supplies an
// immutable byte/model snapshot; this service owns transaction boundaries and
// never mutates a snapshot after it enters the history.
class UndoHistory
{
public:
    struct Entry
    {
        std::string label;
        std::vector<std::byte> before;
        std::vector<std::byte> after;
    };

    explicit UndoHistory(std::size_t byteLimit = 256u * 1024u * 1024u,
                         std::size_t minimumEntries = 8) noexcept
        : byteLimit_(byteLimit), minimumEntries_(minimumEntries) {}

    void clear() noexcept
    {
        entries_.clear();
        redo_.clear();
        pending_.reset();
        bytes_ = 0;
    }

    void begin(std::string label, std::vector<std::byte> before)
    { pending_ = Entry { std::move(label), std::move(before), {} }; }

    bool commit(std::vector<std::byte> after)
    {
        if (!pending_) return false;
        pending_->after = std::move(after);
        if (pending_->before == pending_->after) { pending_.reset(); return false; }
        bytes_ += entryBytes(*pending_);
        entries_.push_back(std::move(*pending_));
        pending_.reset();
        redo_.clear();
        trim();
        return true;
    }

    [[nodiscard]] bool canUndo() const noexcept { return !entries_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::string undoDescription() const { return entries_.empty() ? std::string {} : entries_.back().label; }
    [[nodiscard]] std::string redoDescription() const { return redo_.empty() ? std::string {} : redo_.back().label; }

    // The caller installs the returned snapshot into the model. The current
    // bytes are supplied so redo/undo remains lossless and model-agnostic.
    [[nodiscard]] std::optional<std::vector<std::byte>> undo(const std::vector<std::byte>& current)
    {
        if (entries_.empty()) return std::nullopt;
        Entry entry = std::move(entries_.back());
        entries_.pop_back();
        bytes_ -= entryBytes(entry);
        entry.after = current;
        redo_.push_back(std::move(entry));
        return redo_.back().before;
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> redo(const std::vector<std::byte>& current)
    {
        if (redo_.empty()) return std::nullopt;
        Entry entry = std::move(redo_.back());
        redo_.pop_back();
        entry.before = current;
        bytes_ += entryBytes(entry);
        entries_.push_back(std::move(entry));
        return entries_.back().after;
    }

    [[nodiscard]] std::size_t entryCount() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t byteCount() const noexcept { return bytes_; }

private:
    [[nodiscard]] static std::size_t entryBytes(const Entry& entry) noexcept
    { return entry.label.size() + entry.before.size() + entry.after.size(); }

    void trim() noexcept
    {
        while (entries_.size() > minimumEntries_ && bytes_ > byteLimit_)
        {
            bytes_ -= entryBytes(entries_.front());
            entries_.pop_front();
        }
    }

    std::size_t byteLimit_;
    std::size_t minimumEntries_;
    std::size_t bytes_ = 0;
    std::deque<Entry> entries_;
    std::deque<Entry> redo_;
    std::optional<Entry> pending_;
};

} // namespace ats
