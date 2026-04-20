#include "AtsModel.h"

#include <cstdlib>

extern "C" {
#include "atsa.h"
}

namespace ats
{

AtsSoundPtr::~AtsSoundPtr() { reset(); }

AtsSoundPtr& AtsSoundPtr::operator=(AtsSoundPtr&& o) noexcept
{
    if (this != &o)
    {
        reset();
        ptr = o.ptr;
        o.ptr = nullptr;
    }
    return *this;
}

void AtsSoundPtr::reset(ATS_SOUND* p)
{
    if (ptr)
    {
        free_sound(ptr);
        std::free(ptr);
    }
    ptr = p;
}

} // namespace ats
