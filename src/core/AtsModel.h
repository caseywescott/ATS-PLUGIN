#pragma once

extern "C" {
#include "atsa.h"
}

namespace ats
{

/** Owns an ATS_SOUND allocated by tracker / ats_load; calls free_sound + free in dtor. */
class AtsSoundPtr
{
public:
    AtsSoundPtr() = default;
    explicit AtsSoundPtr(ATS_SOUND* p) : ptr(p) {}
    ~AtsSoundPtr();

    AtsSoundPtr(const AtsSoundPtr&) = delete;
    AtsSoundPtr& operator=(const AtsSoundPtr&) = delete;

    AtsSoundPtr(AtsSoundPtr&& o) noexcept : ptr(o.ptr) { o.ptr = nullptr; }
    AtsSoundPtr& operator=(AtsSoundPtr&& o) noexcept;

    [[nodiscard]] ATS_SOUND* get() const { return ptr; }
    [[nodiscard]] explicit operator bool() const { return ptr != nullptr; }

    void reset(ATS_SOUND* p = nullptr);

private:
    ATS_SOUND* ptr = nullptr;
};

} // namespace ats
