#include "../src/state/PresetService.h"

#include <cassert>
#include <filesystem>

int main()
{
    const auto folder = std::filesystem::temp_directory_path() / "ats-preset-service-test";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder, ec);
    assert(!ec);

    ats::platform::FileService files;
    ats::state::PresetService presets(files, { folder.u8string() });
    assert(ats::state::PresetService::factoryPresets().size() == 12);
    assert(ats::state::PresetService::withExtension("demo") == "demo.atspreset");

    const std::vector<std::byte> payload { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
    assert(presets.save("zeta", payload, false).succeeded());
    assert(presets.save("alpha.atspreset", payload, false).succeeded());
    assert(!presets.save("alpha", payload, false).succeeded());
    const auto all = presets.entries();
    assert(all.size() == 14);
    assert(all[0].factory && all[11].factory);
    assert(all[12].name == "User: alpha");
    assert(all[13].name == "User: zeta");

    const auto loaded = presets.load(all[12].path);
    assert(loaded.succeeded() && loaded.bytes == payload);
    ats::CancellationToken cancellation;
    cancellation.cancel();
    assert(presets.save("cancelled", payload, false, &cancellation).cancelled());

    std::filesystem::remove_all(folder, ec);
    return 0;
}
