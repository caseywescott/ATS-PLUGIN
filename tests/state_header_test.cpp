#include "AtsStateCodec.h"

#include <cassert>
#include <iostream>

int main()
{
    for (std::uint32_t version = 2; version <= 17; ++version)
    {
        const auto bytes = ats::AtsStateCodec::encodeHeader({ version });
        const auto decoded = ats::AtsStateCodec::decodeHeader(bytes.data(), bytes.size());
        assert(decoded && decoded->version == version);
    }
    const auto current = ats::AtsStateCodec::encodeHeader();
    assert(!ats::AtsStateCodec::decodeHeader(current.data(), 7));
    auto bad = current;
    bad[4] = static_cast<std::byte>(1);
    assert(!ats::AtsStateCodec::decodeHeader(bad.data(), bad.size()));
    std::cout << "state_header_test: ATSP v2-v17 gate ok\n";
}
