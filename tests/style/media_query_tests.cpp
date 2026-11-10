#include "style/media_query.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <vector>
int main() {
    xl::detail::ViewEnvironment env;
    env.width = 800;
    env.height = 600;
    env.screen_width = 1920;
    env.screen_height = 1080;
    env.pixel_ratio = 2;
    env.pointer = 2;
    env.hover = 1;
    env.any_pointer = 3;
    env.any_hover = 1;
    const auto check = [&](const std::string &query, bool expected,
                           const std::source_location &at = std::source_location::current()) {
        xl::detail::MediaQuery parsed(query);
        const auto result = parsed.matches(env);
        if (result != expected) {
            std::cerr << at.line() << " query " << query << " -> " << parsed.serialized() << " expected " << expected
                      << " got " << result << '\n';
            std::exit(1);
        }
        if (xl::detail::MediaQuery(parsed.serialized()).matches(env) != result) {
            std::cerr << "serialization changed result " << query << " -> " << parsed.serialized() << '\n';
            std::exit(1);
        }
    };
    for (const auto *query : {"",
                              " ",
                              "/**/",
                              "all",
                              "screen",
                              "only screen",
                              "not print",
                              "not unknown",
                              "screen, print",
                              "bad&syntax, screen",
                              "(width)",
                              "(width:800px)",
                              "(min-width:50em)",
                              "(width:50rem)",
                              "(width:8.333333333333334in)",
                              "(height:600px)",
                              "(800px <= width)",
                              "(width >= 800px)",
                              "(400px < width <= 800px)",
                              "(900px >= width > 400px)",
                              "(width > -1px)",
                              "(min-width:-1px)",
                              "(aspect-ratio:4/3)",
                              "(min-aspect-ratio:1)",
                              "(orientation:landscape)",
                              "(resolution:192dpi)",
                              "(min-resolution:2dppx)",
                              "(resolution:2x)",
                              "(color:8)",
                              "(min-color:1)",
                              "(grid:0)",
                              "(color-index:0)",
                              "(monochrome:0)",
                              "(device-width:1920px)",
                              "(device-aspect-ratio:16/9)",
                              "(pointer:fine)",
                              "(any-pointer:coarse)",
                              "(any-pointer:fine)",
                              "(hover:hover)",
                              "(any-hover:hover)",
                              "(prefers-color-scheme:light)",
                              "not (prefers-color-scheme:dark)",
                              "(prefers-reduced-motion:no-preference)",
                              "(forced-colors:none)",
                              "(prefers-contrast:no-preference)",
                              "(update:fast)",
                              "(overflow-block:scroll)",
                              "(color-gamut:srgb)",
                              "(dynamic-range:standard)",
                              "(width:100vw)",
                              "(height:100dvh)",
                              "not ((width < 800px) or (height > 600px))",
                              "(unknown-feature) or (width:800px)",
                              "(width:800px) and (height:600px)",
                              "SCREEN AND (min-WIDTH:800PX)",
                              "(width:800px),",
                              "(w\\69 dth:800px)",
                              "(width:800px"})
        check(query, true);
    for (const auto *query : {"print",
                              "not all",
                              "only print",
                              "only (width)",
                              "or and (color)",
                              "screen or (color)",
                              "(width < 800px)",
                              "(800px < width)",
                              "(width:0)",
                              "(width:1)",
                              "(width:-1px)",
                              "(max-width:-1px)",
                              "(width:50%)",
                              "(width:2unknown)",
                              "(width:800px !important)",
                              "(width:800px) and (height:600px) or (color)",
                              "(color:2.5)",
                              "(resolution:2)",
                              "(orientation:square)",
                              "(min-orientation:portrait)",
                              "(unknown-feature)",
                              "not (unknown-feature)",
                              "not unknown(future)",
                              "(unknown-feature) and (width:800px)",
                              "(unknown-feature) or (width:1px)",
                              "(width > = 400px)",
                              "(400px < width > 200px)",
                              "(grid:2)",
                              "not (min-grid:0)",
                              "(prefers-reduced-motion)",
                              "(hover:none)",
                              "(any-pointer:none)",
                              "(color-gamut:p3)",
                              "all;",
                              ";",
                              "(width:600px), (height:800px)"})
        check(query, false);
    env.color_scheme = 1;
    env.reduced_motion = 1;
    env.contrast = 1;
    env.forced_colors = 1;
    check("(prefers-color-scheme:dark) and (prefers-reduced-motion:reduce) and (prefers-contrast:more) and "
          "(forced-colors:active)",
          true);
    env.width = 600;
    env.height = 800;
    check("(orientation:portrait)", true);
    check("(width <= 700px)", true);
    check("(aspect-ratio:3/4)", true);
    check(std::string(65537, 'a'), false);
    std::cout << "media list recovery, Kleene logic, ranges, units, devices, preferences and serialization passed\n";
}
