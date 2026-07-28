#include "UndoHistory.h"

#include <cassert>
#include <cstddef>
#include <iostream>

namespace
{
std::vector<std::byte> state(std::uint8_t value)
{ return { static_cast<std::byte>(value) }; }
}

int main()
{
    ats::UndoHistory history(2, 2);
    history.begin("first", state(0));
    assert(history.commit(state(1)));
    history.begin("second", state(1));
    assert(history.commit(state(2)));
    assert(history.canUndo() && history.undoDescription() == "second");
    const auto undoState = history.undo(state(2));
    assert(undoState && std::to_integer<std::uint8_t>((*undoState)[0]) == 1);
    assert(history.canRedo());
    const auto redoState = history.redo(state(1));
    assert(redoState && std::to_integer<std::uint8_t>((*redoState)[0]) == 2);
    history.begin("new edit", state(2));
    assert(history.commit(state(3)));
    assert(!history.canRedo());
    std::cout << "undo_history_test: transaction and redo rules ok\n";
}
