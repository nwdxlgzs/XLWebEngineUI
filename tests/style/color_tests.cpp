#include "style/color.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace xl::detail::csscolor;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::cerr << __LINE__ << ": " #c "\n";                                                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main() {
    const auto pixel = [](std::string_view css, uint32_t expected, bool dark = false,
                          const std::source_location &at = std::source_location::current()) {
        auto color = parse(css);
        if (!color) {
            std::cerr << at.line() << " rejected " << css << '\n';
            std::exit(1);
        }
        const auto actual = argb(resolve(color, {from_argb(0xff00ff00), dark}));
        if (actual != expected) {
            std::cerr << at.line() << ' ' << css << " expected " << std::hex << expected << " got " << actual << '\n';
            std::exit(1);
        }
    };
    pixel("red", 0xffff0000);
    pixel("ReBeCcApUrPlE", 0xff663399);
    pixel("#f00", 0xffff0000);
    pixel("#ff000080", 0x80ff0000);
    pixel("#f008", 0x88ff0000);
    pixel("rgb(255 0 0)", 0xffff0000);
    pixel("rgba(100%,0%,0%,.5)", 0x80ff0000);
    pixel("rgb(100% 0 0 / 50%)", 0x80ff0000);
    pixel("rgb(none 0 0)", 0xff000000);
    pixel("hsl(0 100% 50%)", 0xffff0000);
    pixel("hsl(-360deg,100%,50%)", 0xffff0000);
    pixel("hsl(.5turn 100% 50% / 50%)", 0x8000ffff);
    pixel("hwb(120 0% 0%)", 0xff00ff00);
    pixel("hwb(60 50% 50%)", 0xff808080);
    pixel("hwb(0 100% 100%)", 0xff808080);
    pixel("lab(54.2917% 80.8125 69.8851)", 0xffff0000);
    pixel("oklch(62.7955% .257683 29.2339)", 0xffff0000);
    pixel("color(srgb 1 0 0)", 0xffff0000);
    pixel("color(srgb-linear 0.5 0.5 0.5)", 0xffbcbcbc);
    pixel("oklab(0% 0 0)", 0xff000000);
    pixel("oklch(100% .4 20)", 0xffffffff);
    pixel("color-mix(in srgb, red, blue)", 0xff800080);
    pixel("color-mix(in srgb-linear, red, blue)", 0xffbc00bc);
    CHECK(argb(resolve(parse("color-mix(red,blue)"))) == argb(resolve(parse("color-mix(in oklab,red,blue)"))));
    pixel("color-mix(in srgb, red 25%, blue 25%)", 0x80800080);
    pixel("color-mix(in srgb, transparent, red)", 0x80ff0000);
    pixel("color-mix(in srgb, rgb(none 0 0), red)", 0xffff0000);
    pixel("color-mix(in hsl, hsl(350 100% 50%), hsl(10 100% 50%))", 0xffff0000);
    pixel("color-mix(in hsl longer hue, hsl(350 100% 50%), hsl(10 100% 50%))", 0xff00ffff);
    pixel("color-mix(in hsl,red,hsl(180 0% 50%))", 0xff80bf40);
    pixel("light-dark(red, blue)", 0xffff0000);
    pixel("light-dark(red, blue)", 0xff0000ff, true);
    pixel("color-mix(in srgb, currentColor, #000)", 0xff008000);
    pixel("hsl(calc(720 - 360) 100% clamp(0%, 50%, 80%))", 0xffff0000);
    pixel("oklch(62.7955% calc(.20 + .057683) calc(20 + 9.2339))", 0xffff0000);
    pixel("r\\65 d", 0xffff0000);
    pixel("rgb(/*comment*/255 0 0)", 0xffff0000);
    pixel("#\\66 00", 0xffff0000);
    pixel("rgb(10%20%30%)", 0xff1a334d);
    pixel("rgb(0/**/0/**/0)", 0xff000000);
    pixel("hsl(0/**/100%/**/50%)", 0xffff0000);
    pixel("color(srgb .2/**/.4/**/.6)", 0xff336699);
    for (const auto *invalid : {"",
                                "red blue",
                                "#ggg",
                                "#12345",
                                "rgb(0, 50%, 1)",
                                "rgb(0 0 0, .5)",
                                "rgb(0 0 0 /)",
                                "rgb(0 0)",
                                "hwb(0, 0%, 0%)",
                                "lab(50 0 0 1)",
                                "hsl(0 100% 50% junk)",
                                "color(unknown 1 0 0)",
                                "color(lab 1 0 0)",
                                "color-mix(in srgb, red 0%, blue 0%)",
                                "color-mix(in srgb longer hue,red,blue)",
                                "color-mix(in srgb,red 101%,blue)",
                                "color-mix(in srgb,red -100%,blue)",
                                "light-dark(red)",
                                "rgb(calc(2/0) 0 0)",
                                "hsl(calc(2+2) 100% 50%)",
                                "hsl(50% 100% 50%)",
                                "hsl(0,10,10)",
                                "rgb(calc(50% + 1) 0 0)",
                                "rgb(calc(50% * 50%) 0 0)",
                                "rgb(calc(1,) 0 0)",
                                "rgb(clamp(1,2,3,) 0 0)"})
        CHECK(!parse(invalid));
    for (const auto space :
         {Space::srgb, Space::linear_srgb, Space::p3, Space::linear_p3, Space::a98, Space::prophoto, Space::rec2020,
          Space::xyz65, Space::xyz50, Space::lab, Space::lch, Space::oklab, Space::oklch, Space::hsl, Space::hwb}) {
        Value original{Space::srgb, {.2, .4, .7}, .35, 0};
        auto returned = convert(convert(original, space), Space::srgb);
        for (unsigned i = 0; i < 3; ++i)
            CHECK(std::abs(original.channels[i] - returned.channels[i]) < .000002);
        CHECK(returned.alpha == original.alpha);
    }
    CHECK(dark_scheme("light dark", false, true));
    CHECK(!dark_scheme("only light", true, true));
    CHECK(convert({Space::lab, {50, 0, 0}, 1, 6}, Space::lch).missing == 6);
    CHECK(convert({Space::lab, {50, 0, 0}, 1, 2}, Space::lch).missing == 4);
    CHECK(convert({Space::lch, {50, 0, 0}, 1, 6}, Space::lab).missing == 6);
    CHECK(dark_scheme("dark", false, false));
    CHECK(!dark_scheme("normal", true, true));
    CHECK(!dark_scheme("normal/**/", true, true));
    CHECK(valid_scheme("only light dark"));
    CHECK(!valid_scheme("only only light"));
    CHECK(!valid_scheme("normal light"));
    std::cout << "CSS color syntax, spaces, interpolation, missing components, alpha and scheme passed\n";
}
