// Licence: GPL-3.0-or-later
// LibreCast Studio — Slint pilot: minimal C++/Slint entry point.
//
// This is the smallest possible Slint + C++ round-trip to validate the pilot criteria
// (see docs/experimental/slint-visual-spec.md §B):
//   1. A .slint component compiles and renders (BroadcastControlSurface).
//   2. The DSL's callbacks invoke host C++ code (transition_triggered, buffer_size_changed).
//   3. Host C++ pushes reactive properties into Slint (is_streaming, asio_buffer_samples).
//
// It is a STUB by design: dead-label telemetry, no engine integration. It exists to measure
// toolchain/event-loop integration cost, not to ship UI.

#include <iostream>

#include "BroadcastControlSurface.h"

int main()
{
    auto ui = BroadcastControlSurface::create();

    // DSL -> C++: wire Slint callbacks to host handlers.
    ui->on_transition_triggered([ui](slint::SharedString type) {
        std::cerr << "[Slint pilot] transition_triggered(" << type << ")\n";
        // In a real build this would drive the compositor's cut/fade/T-Bar path.
    });

    ui->on_buffer_size_changed([ui](int samples) {
        std::cerr << "[Slint pilot] buffer_size_changed(" << samples << ") -> host config\n";
        // In a real build this would push a buffer-size request into the ASIO/CoreAudio layer.
    });

    // C++ -> DSL: reactive property push (demonstrates declarative state styling).
    ui->set_is_streaming(false);
    ui->set_asio_buffer_samples(64);

    ui->run();

    return 0;
}
