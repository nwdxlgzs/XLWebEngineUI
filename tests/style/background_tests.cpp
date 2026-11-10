#include "style/background.h"
#include <cstdlib>
#include <iostream>
using namespace xl::detail::cssbackground;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::cerr << __LINE__ << ": " #c "\n";                                   \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)
int main() {
  for (const auto *value :
       {"linear-gradient(red, blue)",
        "linear-gradient(to right top,red 10%,blue 30% 80%,green)",
        "linear-gradient(.25turn in oklch longer hue,red,50%,blue)",
        "linear-gradient(in xyz-d50,red -10px,blue calc(100% - 2em))",
        "radial-gradient(red,blue)",
        "radial-gradient(circle closest-side at right 10px bottom "
        "20%,currentColor,transparent)",
        "radial-gradient(ellipse 20px 40% at 30% 40% in srgb-linear,red,blue)",
        "conic-gradient(from 45deg at 40% 60%,red,blue 70%,red)",
        "repeating-linear-gradient(45deg,red 0 12px,blue 12px 24px)",
        "repeating-radial-gradient(circle 50px,red 0%,blue 50%)",
        "repeating-conic-gradient(from -.5turn,red 0deg 90deg,blue 25% .5turn)",
        "none,linear-gradient(red,blue)"}) {
    if (!parse("background-image", value)) {
      std::cerr << "rejected " << value << '\n';
      return 1;
    }
  }
  for (const auto *value :
       {"linear-gradient(red)", "linear-gradient(red,,blue)",
        "linear-gradient(to left right,red,blue)",
        "linear-gradient(20%,red,blue)",
        "linear-gradient(in srgb longer hue,red,blue)",
        "linear-gradient(red,20%,40%,blue)", "linear-gradient(20%,red,blue)",
        "linear-gradient(red,blue,20%)", "radial-gradient(circle 20%,red,blue)",
        "radial-gradient(circle 10px 20px,red,blue)",
        "radial-gradient(ellipse 10px,red,blue)",
        "conic-gradient(red 1px,blue)", "conic-gradient(from 20%,red,blue)",
        "radial-gradient(at,red,blue)", "linear-gradient(red 10%20%,blue)",
        "linear-gradient(red,blue);color:red", "none,",
        "linear-gradient(red,blue) rubbish"}) {
    if (parse("background-image", value)) {
      std::cerr << "accepted invalid " << value << '\n';
      return 1;
    }
  }
  auto layers = parse(
      "background",
      "linear-gradient(red,blue) left top / 20px 30% no-repeat padding-box, "
      "radial-gradient(circle,red,blue) center / cover border-box green");
  CHECK(layers && layers->style.images.size() == 2 && layers->color == "green");
  CHECK(layers->style.origins[0] == Box::padding &&
        layers->style.clips[0] == Box::padding);
  CHECK(layers->style.repeats[0][0] == Repeat::none &&
        layers->style.sizes[1].fit == 1);
  CHECK(parse("background", "none"));
  CHECK(parse("background", "red"));
  CHECK(parse("background", "revert-layer"));
  CHECK(!parse("background", "red,blue"));
  CHECK(!parse("background", "transparent red"));
  CHECK(!parse("background", "red / 20px"));
  CHECK(parse("background-position", "right 2em bottom 20%, top left, center"));
  CHECK(!parse("background-position", "left right"));
  CHECK(!parse("background-size", "-1px"));
  CHECK(parse("background-size", "calc(100% - 20px) auto,cover"));
  CHECK(!parse("background-repeat", "repeat-x repeat-y"));
  CHECK(parse("background-attachment", "fixed,local,scroll"));
  const auto gradient = layers->style.images[0];
  CHECK(gradient && gradient->stops.size() == 2);
  auto modern =
      parse("background-image", "linear-gradient(oklch(.6 .1 20),red)");
  CHECK(modern->style.images[0]->space == xl::detail::csscolor::Space::oklab);
  CHECK(resolve(Length{2, .5, 1, 1, .1, .1, 0, 0}, 100,
                Environment{10, 20, 100, 200}) == 112);
  std::cout << "Gradient syntax, layers, shorthand reset data and deferred "
               "lengths passed\n";
}
