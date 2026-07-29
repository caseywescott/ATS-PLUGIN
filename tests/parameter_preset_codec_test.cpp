#include "../src/platform/FileService.h"
#include "../src/state/ParameterPresetCodec.h"

#include <cassert>
#include <filesystem>

int main()
{
    ats::ParameterStore source;
    assert(source.write("partGain", 1.6f));
    assert(source.write("morphLaneResidualAmount", 0.65f));
    const auto payload = ats::state::ParameterPresetCodec::encode(source);

    const auto folder = std::filesystem::temp_directory_path() / "ats-parameter-preset-codec-test";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder, ec);
    assert(!ec);
    const ats::Path path { (folder / "roundtrip.atspreset").u8string() };
    ats::platform::FileService files;
    assert(files.writeAtomic(path, payload, false).succeeded());
    const auto loaded = files.read(path);
    assert(loaded.succeeded());

    ats::ParameterStore restored;
    std::string error;
    assert(ats::state::ParameterPresetCodec::decode(loaded.bytes, restored, error));
    assert(std::abs(restored.read("partGain") - source.read("partGain")) < 1.0e-3f);
    assert(std::abs(restored.read("morphLaneResidualAmount") - source.read("morphLaneResidualAmount")) < 1.0e-3f);

    auto malformed = loaded.bytes;
    malformed.pop_back();
    assert(!ats::state::ParameterPresetCodec::decode(malformed, restored, error));
    std::filesystem::remove_all(folder, ec);
    return 0;
}
