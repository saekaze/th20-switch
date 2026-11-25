// Native implementations of the browser hooks (EM_JS functions) that the
// upstream TH20 sources declare. The ones that matter natively ask the host
// (imports from "th20n"); the rest are browser-page notifications.
#include <cstdint>
#define TH20N_IMPORT(name) __attribute__((import_module("th20n"), import_name(name)))
extern "C" {
TH20N_IMPORT("clip_control") std::int32_t th20n_clip_control();
TH20N_IMPORT("output_units") std::int32_t th20n_output_units();

// portable/sdl/Renderer.cpp
int touhou_clip_control() { return th20n_clip_control(); }      // EXT_clip_control enabled
int touhou_output_units() { return th20n_output_units(); }      // picture size / (4,3)
// web/cpp/sdl/InputHost.cpp: no browser-owned keyboard; SDL's state is used.
int th20_browser_keyboard() { return 0; }
void th20_reset_browser_keyboard() {}
// web/cpp/sdl/FileHost.cpp: the page persisted IDBFS here; files are on disk.
void browser_save_changed() {}
}
