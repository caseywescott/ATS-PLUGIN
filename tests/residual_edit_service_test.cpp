#include "../src/engine/ResidualEditService.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::engine::ResidualMatrix matrix {
        { { 1, { 1.0, 2.0, 3.0, 4.0 } }, { 2, { 10.0, 20.0, 30.0, 40.0 } },
          { 3, { 100.0, 200.0, 300.0, 400.0 } } }
    };
    const ats::engine::ResidualSelection all { { 1, 2 }, 1, 2 };
    auto result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::scale, all, { 2.0 });
    assert(result.success && result.changed && result.energyAfter == 2.0 * result.energyBefore);
    assert(matrix.find(1)->energy[1] == 4.0);

    const auto before = matrix;
    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::scale, all, { -1.0 });
    assert(!result.success && matrix.bands == before.bands);

    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::redistribute, { { 1, 2 }, 0, 3 });
    assert(result.success);
    for (int frame = 0; frame < 4; ++frame)
        assert(std::abs(matrix.find(1)->energy[frame] - matrix.find(2)->energy[frame]) < 1.0e-12);

    ats::engine::ResidualClipboard clipboard;
    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::copy, { { 1 }, 1, 2 }, {}, &clipboard);
    assert(result.success && clipboard.valid() && clipboard.energies.size() == 2);
    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::paste, { { 3 }, 0, 3 }, {}, &clipboard);
    assert(result.success && matrix.find(3)->energy[0] == clipboard.energies[0]);

    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::drawDb, { { 1 }, 0, 3 },
        { 1.0, 1, 0.0, { { 0, -20.0 }, { 3, -10.0 } } });
    assert(result.success && std::isfinite(matrix.find(1)->energy[1]));
    result = ats::engine::ResidualEditService::apply(
        matrix, ats::engine::ResidualEditOperation::swap, { { 1, 2 }, 0, 3 }, {}, nullptr);
    assert(result.success);
    return 0;
}
