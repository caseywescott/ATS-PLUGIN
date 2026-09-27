// VST3 editor size negotiation (audit T-404): preferred size, resizability,
// checkSizeConstraint clamping to EditorSizeLimits, and onSize adoption, on
// the real AtsVst3PlugView created by the real controller.

#include "../../src/host/vst3/AtsVst3Plugin.cpp"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "vst3_view_negotiation_test failed: " << what << '\n';
        std::abort();
    }
}

Steinberg::ViewRect rect(int width, int height)
{
    return Steinberg::ViewRect(0, 0, width, height);
}
}

int main()
{
    Steinberg::Vst::HostApplication hostApplication;
    Steinberg::IPtr<ats::vst3::AtsVst3Controller> controller(
        static_cast<ats::vst3::AtsVst3Controller*>(
            static_cast<Steinberg::Vst::IEditController*>(
                ats::vst3::AtsVst3Controller::createInstance(nullptr))),
        false);
    require(controller->initialize(&hostApplication) == Steinberg::kResultTrue, "controller initialize");

    Steinberg::IPtr<Steinberg::IPlugView> view(controller->createView(Steinberg::Vst::ViewType::kEditor),
                                               false);
    require(view != nullptr, "createView(editor)");
    require(controller->createView("not-an-editor") == nullptr, "unknown view type rejected");

    const ats::ui::EditorSizeLimits limits;
    Steinberg::ViewRect size;
    require(view->getSize(&size) == Steinberg::kResultTrue, "getSize");
    require(size.getWidth() == static_cast<int>(limits.defaultWidth) &&
                size.getHeight() == static_cast<int>(limits.defaultHeight),
            "preferred size is the design size");
    require(view->canResize() == Steinberg::kResultTrue, "view is resizable");

    struct Case { int width, height, expectedWidth, expectedHeight; const char* name; };
    const Case cases[] {
        { 1500, 1000, 1500, 1000, "within range unchanged" },
        { 720, 480, static_cast<int>(limits.minWidth), static_cast<int>(limits.minHeight), "below min clamps" },
        { 2500, 2000, static_cast<int>(limits.maxWidth), static_cast<int>(limits.maxHeight), "above max clamps" },
        { 720, 2000, static_cast<int>(limits.minWidth), static_cast<int>(limits.maxHeight), "axes clamp independently" },
        { static_cast<int>(limits.minWidth), static_cast<int>(limits.minHeight),
          static_cast<int>(limits.minWidth), static_cast<int>(limits.minHeight), "exact min accepted" },
        { static_cast<int>(limits.maxWidth), static_cast<int>(limits.maxHeight),
          static_cast<int>(limits.maxWidth), static_cast<int>(limits.maxHeight), "exact max accepted" },
    };
    for (const auto& c : cases)
    {
        auto proposed = rect(c.width, c.height);
        require(view->checkSizeConstraint(&proposed) == Steinberg::kResultTrue,
                std::string("checkSizeConstraint ") + c.name);
        require(proposed.getWidth() == c.expectedWidth && proposed.getHeight() == c.expectedHeight,
                std::string("constrained size: ") + c.name);
    }
    require(view->checkSizeConstraint(nullptr) == Steinberg::kResultFalse, "null constraint rect");

    // A host applies the negotiated size through onSize; getSize reports it back.
    auto negotiated = rect(1500, 1000);
    require(view->checkSizeConstraint(&negotiated) == Steinberg::kResultTrue, "negotiate 1500x1000");
    require(view->onSize(&negotiated) == Steinberg::kResultTrue, "onSize negotiated");
    require(view->getSize(&size) == Steinberg::kResultTrue && size.getWidth() == 1500 &&
                size.getHeight() == 1000,
            "getSize reports the adopted size");
    auto empty = rect(0, 0);
    require(view->onSize(&empty) == Steinberg::kResultFalse, "onSize rejects an empty rect");

    view = nullptr;
    controller->terminate();
    std::cout << "vst3_view_negotiation_test: preferred/min/max negotiation ok\n";
    return 0;
}
