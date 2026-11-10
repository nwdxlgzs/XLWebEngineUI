#pragma once
namespace xl::detail {
struct ViewEnvironment {
    int width = 640, height = 480;
    int screen_width = 0, screen_height = 0, color_depth = 24;
    // The current native renderer maps one CSS coordinate to one drawable
    // pixel; separate DPI-aware CSS viewport conversion remains future work.
    double pixel_ratio = 1;
    unsigned color_scheme = 0, reduced_motion = 0, contrast = 0, forced_colors = 0;
    unsigned pointer = 0, hover = 0, any_pointer = 0, any_hover = 0;
    bool hidden = false;
    bool operator==(const ViewEnvironment&) const = default;
};
}
