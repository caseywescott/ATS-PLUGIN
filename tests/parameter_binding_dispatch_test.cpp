#include "../src/ui/ParameterBinding.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{
struct Accepted
{
    std::vector<ats::ui::SetParameterAction> actions;
    void operator()(const ats::ui::SetParameterAction& action) { actions.push_back(action); }
};
}

int main()
{
    ats::ParameterStore store;
    Accepted accepted;
    ats::ui::ParameterActionDispatcher dispatcher(store, std::ref(accepted));
    ats::ui::ParameterBinding binding(store, "freq", dispatcher);

    assert(binding.valid());
    assert(binding.beginGesture(10.0f));
    assert(binding.gestureActive());
    assert(binding.updateGesture(0.5f));
    assert(binding.endGesture(2.0f));
    assert(!binding.gestureActive());
    assert(store.read("freq") == 2.0f);
    assert(accepted.actions.size() == 3);
    assert(accepted.actions[0].begin && !accepted.actions[0].end);
    assert(!accepted.actions[1].begin && !accepted.actions[1].end);
    assert(!accepted.actions[2].begin && accepted.actions[2].end);

    assert(binding.setValue(-1.0f));
    assert(store.read("freq") == 0.25f);
    ats::ui::ParameterBinding invalid(store, "missing", dispatcher);
    assert(!invalid.valid());
    assert(!invalid.setValue(1.0f));
    return 0;
}
