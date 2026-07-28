#include "UiDispatcher.h"

#include <cassert>
#include <iostream>

int main()
{
    ats::ui::Dispatcher dispatcher;
    ats::ui::Frame parent { 10 };
    ats::ui::Frame child { 11 };
    ats::ui::Frame tooltip { 12 };
    parent.setBounds({ 10, 20, 100, 100 });
    child.setBounds({ 20, 20, 20, 20 });
    child.setZOrder(2);
    tooltip.setBounds({ 0, 0, 300, 300 });
    tooltip.setZOrder(99);
    tooltip.setClickThrough(true);
    dispatcher.add(parent, dispatcher.root());
    dispatcher.add(child, parent);
    dispatcher.add(tooltip, dispatcher.root());

    ats::ui::PointerEvent received;
    child.onPointer([&received](const auto& event) { received = event; });
    dispatcher.dispatch({ ats::ui::PointerEvent::Type::down, { 35, 45 }, {}, {}, 1 });
    assert(received.targetId == 11 && received.localPosition.x == 5 && received.localPosition.y == 5);
    assert(dispatcher.captured() == &child && dispatcher.focused() == &child);
    dispatcher.dispatch({ ats::ui::PointerEvent::Type::move, { 500, 500 }, {}, { 465, 455 }, 1 });
    assert(received.type == ats::ui::PointerEvent::Type::move && received.localPosition.x == 470);
    dispatcher.dispatch({ ats::ui::PointerEvent::Type::up, { 500, 500 }, {}, {}, 1 });
    assert(dispatcher.captured() == nullptr);

    ats::ui::Frame popup { 20 };
    dispatcher.setPopup(&popup);
    assert(dispatcher.popup() == &popup && dispatcher.focused() == &popup);
    dispatcher.dismissPopup();
    assert(dispatcher.popup() == nullptr && dispatcher.focused() == nullptr);
    std::cout << "ui_dispatcher_test: local targeting, capture, click-through, popup focus ok\n";
}
