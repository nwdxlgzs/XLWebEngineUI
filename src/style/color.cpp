#include "color.h"
#include "document/document.h"
#include "include/utils/SkParse.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <lexbor/css/syntax/tokenizer.h>
#include <limits>
#include <vector>

namespace xl::detail::csscolor {
namespace {
using Vec = std::array<double, 3>;
using Mat = std::array<Vec, 3>;
constexpr double pi = 3.14159265358979323846;
constexpr Mat srgb_xyz{{{.4123907992659595, .357584339383878, .1804807884018343},
                        {.21263900587151036, .715168678767756, .07219231536073371},
                        {.01933081871559185, .11919477979462599, .9505321522496607}}};
constexpr Mat p3_xyz{{{.4865709486482162, .26566769316909306, .1982172852343625},
                      {.2289745640697488, .6917385218365064, .079286914093745},
                      {0, .04511338185890264, 1.043944368900976}}};
constexpr Mat a98_xyz{{{.5766690429101305, .1855582379065463, .1882286462349947},
                       {.297344975250536, .6273635662554661, .0752914584939979},
                       {.0270313613864123, .0706888525358272, .9913375368376388}}};
constexpr Mat pro_xyz{{{.7977666449006423, .1351812974005331, .0313477341283922},
                       {.2880748288194013, .711835234241873, .00008993693872564},
                       {0, 0, .8251046025104601}}};
constexpr Mat rec_xyz{{{.6369580483012914, .14461690358620832, .1688809751641721},
                       {.2627002120112671, .6779980715188708, .05930171646986196},
                       {0, .028072693049087428, 1.060985057710791}}};
constexpr Mat d50_d65{{{.955473421488075, -.02309845494876471, .06325924320057072},
                       {-.0283697093338637, 1.0099953980813041, .021041441191917323},
                       {.012314014864481998, -.020507649298898964, 1.330365926242124}}};
Vec multiply(const Mat &m, Vec v) {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}
Mat inverse(const Mat &m) {
    Mat r{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            r[j][i] = m[(i + 1) % 3][(j + 1) % 3] * m[(i + 2) % 3][(j + 2) % 3] -
                      m[(i + 1) % 3][(j + 2) % 3] * m[(i + 2) % 3][(j + 1) % 3];
    const auto d = m[0][0] * r[0][0] + m[0][1] * r[1][0] + m[0][2] * r[2][0];
    for (auto &row : r)
        for (auto &n : row)
            n /= d;
    return r;
}
double signpow(double value, double exponent) { return std::copysign(std::pow(std::abs(value), exponent), value); }
double hue(double h) {
    h = std::fmod(h, 360.);
    return h < 0 ? h + 360 : h;
}
double srgb_linear(double v) {
    return std::abs(v) <= .04045 ? v / 12.92 : std::copysign(std::pow((std::abs(v) + .055) / 1.055, 2.4), v);
}
double linear_srgb(double v) {
    return std::abs(v) <= .0031308 ? v * 12.92 : std::copysign(1.055 * std::pow(std::abs(v), 1 / 2.4) - .055, v);
}
Vec linear(Vec v, Space space, bool encode = false) {
    for (auto &n : v) {
        if (space == Space::srgb || space == Space::p3)
            n = encode ? linear_srgb(n) : srgb_linear(n);
        else if (space == Space::a98)
            n = signpow(n, encode ? 256. / 563 : 563. / 256);
        else if (space == Space::prophoto)
            n = encode ? (std::abs(n) <= 1. / 512 ? n * 16 : signpow(n, 1 / 1.8))
                       : (std::abs(n) <= 16. / 512 ? n / 16 : signpow(n, 1.8));
        else if (space == Space::rec2020) {
            constexpr double a = 1.09929682680944, b = .018053968510807;
            n = encode ? (std::abs(n) < b ? n * 4.5 : std::copysign(a * std::pow(std::abs(n), .45) - (a - 1), n))
                       : (std::abs(n) < b * 4.5 ? n / 4.5
                                                : std::copysign(std::pow((std::abs(n) + a - 1) / a, 1 / .45), n));
        }
    }
    return v;
}
Vec polar(Vec v, bool to_polar) {
    if (to_polar)
        return {v[0], std::hypot(v[1], v[2]), hue(std::atan2(v[2], v[1]) * 180 / pi)};
    return {v[0], v[1] * std::cos(v[2] * pi / 180), v[1] * std::sin(v[2] * pi / 180)};
}
Vec hsl_rgb(Vec v) {
    const double h = hue(v[0]) / 60, s = v[1] / 100, l = v[2] / 100;
    const double chroma = (1 - std::abs(2 * l - 1)) * s, x = chroma * (1 - std::abs(std::fmod(h, 2.) - 1)),
                 m = l - chroma / 2;
    Vec rgb = h < 1   ? Vec{chroma, x, 0}
              : h < 2 ? Vec{x, chroma, 0}
              : h < 3 ? Vec{0, chroma, x}
              : h < 4 ? Vec{0, x, chroma}
              : h < 5 ? Vec{x, 0, chroma}
                      : Vec{chroma, 0, x};
    for (auto &n : rgb)
        n += m;
    return rgb;
}
Vec rgb_hsl(Vec v) {
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    const auto d = *hi - *lo, l = (*hi + *lo) / 2;
    double h = 0, s = 0;
    if (d > 1e-12) {
        h = *hi == v[0] ? (v[1] - v[2]) / d : *hi == v[1] ? (v[2] - v[0]) / d + 2 : (v[0] - v[1]) / d + 4;
        s = d / (1 - std::abs(2 * l - 1));
    }
    if (s < 0) {
        s = -s;
        h += 3;
    }
    return {hue(h * 60), s * 100, l * 100};
}
Vec xyz(Value v) {
    auto c = v.channels;
    if (v.space == Space::hsl || v.space == Space::hwb) {
        if (v.space == Space::hsl)
            c = hsl_rgb(c);
        else {
            const double w = c[1] / 100, b = c[2] / 100;
            if (w + b >= 1)
                c = {w / (w + b), w / (w + b), w / (w + b)};
            else {
                c = hsl_rgb({c[0], 100, 50});
                for (auto &n : c)
                    n = n * (1 - w - b) + w;
            }
        }
        return multiply(srgb_xyz, linear(c, Space::srgb));
    }
    if (v.space == Space::oklch)
        c = polar(c, false);
    if (v.space == Space::oklch || v.space == Space::oklab) {
        const auto l = std::pow(c[0] + .3963377774 * c[1] + .2158037573 * c[2], 3);
        const auto m = std::pow(c[0] - .1055613458 * c[1] - .0638541728 * c[2], 3);
        const auto s = std::pow(c[0] - .0894841775 * c[1] - 1.291485548 * c[2], 3);
        return multiply(srgb_xyz, {4.0767416621 * l - 3.3077115913 * m + .2309699292 * s,
                                   -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s,
                                   -.0041960863 * l - .7034186147 * m + 1.707614701 * s});
    }
    if (v.space == Space::lch)
        c = polar(c, false);
    if (v.space == Space::lch || v.space == Space::lab) {
        const double y = (c[0] + 16) / 116, x = y + c[1] / 500, z = y - c[2] / 200;
        const auto f = [](double n) { return n > 6. / 29 ? n * n * n : (116 * n - 16) / (24389. / 27); };
        return multiply(d50_d65, {f(x) * (.3457 / .3585), f(y), f(z) * ((1 - .3457 - .3585) / .3585)});
    }
    if (v.space == Space::xyz50)
        return multiply(d50_d65, c);
    if (v.space == Space::xyz65)
        return c;
    const auto &matrix = v.space == Space::p3 || v.space == Space::linear_p3 ? p3_xyz
                         : v.space == Space::a98                             ? a98_xyz
                         : v.space == Space::prophoto                        ? pro_xyz
                         : v.space == Space::rec2020                         ? rec_xyz
                                                                             : srgb_xyz;
    auto value = multiply(matrix, linear(c, v.space));
    return v.space == Space::prophoto ? multiply(d50_d65, value) : value;
}
int angle_index(Space space) {
    return space == Space::hsl || space == Space::hwb ? 0 : space == Space::lch || space == Space::oklch ? 2 : -1;
}
unsigned powerless(Value value) {
    auto missing = value.missing;
    if ((value.space == Space::lch && value.channels[1] <= .000004) ||
        (value.space == Space::oklch && value.channels[1] <= .000004))
        missing |= 4;
    if ((value.space == Space::hsl && std::abs(value.channels[1]) <= .000004) ||
        (value.space == Space::hwb && value.channels[1] + value.channels[2] >= 100))
        missing |= 1;
    return missing;
}
int category(Space space, unsigned i) {
    if (i == 3)
        return 9;
    if (space == Space::hsl)
        return i == 0 ? 8 : i == 1 ? 7 : 6;
    if (space == Space::hwb)
        return i == 0 ? 8 : i == 1 ? 10 : 11;
    if (space == Space::lab || space == Space::oklab)
        return i == 0 ? 6 : i == 1 ? 4 : 5;
    if (space == Space::lch || space == Space::oklch)
        return i == 0 ? 6 : i == 1 ? 7 : 8;
    return static_cast<int>(i);
}
std::string lower(std::string text) {
    for (auto &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
bool wide(std::string_view text) {
    return text == "initial" || text == "inherit" || text == "unset" || text == "revert" || text == "revert-layer";
}
} // namespace

Value convert(Value value, Space space) {
    if (space == value.space)
        return value;
    value.missing = powerless(value);
    const auto source = value;
    for (unsigned i = 0; i < 3; ++i)
        if (value.missing & (1u << i))
            value.channels[i] = 0;
    if (value.missing & 8)
        value.alpha = 0;
    auto c = xyz(value);
    static const auto xyz_srgb = inverse(srgb_xyz), xyz_p3 = inverse(p3_xyz), xyz_a98 = inverse(a98_xyz),
                      xyz_pro = inverse(pro_xyz), xyz_rec = inverse(rec_xyz), d65_d50 = inverse(d50_d65);
    if (space == Space::xyz50)
        c = multiply(d65_d50, c);
    else if (space == Space::lab || space == Space::lch) {
        c = multiply(d65_d50, c);
        c[0] /= .3457 / .3585;
        c[2] /= (1 - .3457 - .3585) / .3585;
        for (auto &n : c)
            n = n > 216. / 24389 ? std::cbrt(n) : ((24389. / 27) * n + 16) / 116;
        c = {116 * c[1] - 16, 500 * (c[0] - c[1]), 200 * (c[1] - c[2])};
        if (space == Space::lch)
            c = polar(c, true);
    } else if (space == Space::oklab || space == Space::oklch) {
        const auto rgb = multiply(xyz_srgb, c);
        const auto l = std::cbrt(.4122214708 * rgb[0] + .5363325363 * rgb[1] + .0514459929 * rgb[2]);
        const auto m = std::cbrt(.2119034982 * rgb[0] + .6806995451 * rgb[1] + .1073969566 * rgb[2]);
        const auto s = std::cbrt(.0883024619 * rgb[0] + .2817188376 * rgb[1] + .6299787005 * rgb[2]);
        c = {.2104542553 * l + .793617785 * m - .0040720468 * s, 1.9779984951 * l - 2.428592205 * m + .4505937099 * s,
             .0259040371 * l + .7827717662 * m - .808675766 * s};
        if (space == Space::oklch)
            c = polar(c, true);
    } else if (space != Space::xyz65) {
        if (space == Space::prophoto)
            c = multiply(d65_d50, c);
        c = multiply(space == Space::p3 || space == Space::linear_p3 ? xyz_p3
                     : space == Space::a98                           ? xyz_a98
                     : space == Space::prophoto                      ? xyz_pro
                     : space == Space::rec2020                       ? xyz_rec
                                                                     : xyz_srgb,
                     c);
        c = linear(c, space == Space::hsl || space == Space::hwb ? Space::srgb : space, true);
        if (space == Space::hsl)
            c = rgb_hsl(c);
        else if (space == Space::hwb) {
            const auto h = rgb_hsl(c)[0];
            const auto [lo, hi] = std::minmax_element(c.begin(), c.end());
            c = {h, *lo * 100, (1 - *hi) * 100};
        }
    }
    unsigned missing = source.missing & 8;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if ((source.missing & (1u << i)) && category(source.space, i) == category(space, j))
                missing |= 1u << j;
    unsigned source_remaining = 0, target_remaining = 0;
    for (unsigned i = 0; i < 3; ++i) {
        bool matched = false;
        for (unsigned j = 0; j < 3; ++j)
            matched |= category(source.space, i) == category(space, j);
        if (!matched)
            source_remaining |= 1u << i;
        matched = false;
        for (unsigned j = 0; j < 3; ++j)
            matched |= category(space, i) == category(source.space, j);
        if (!matched)
            target_remaining |= 1u << i;
    }
    if (source_remaining && (source.missing & source_remaining) == source_remaining)
        missing |= target_remaining;
    Value result{space, c, source.alpha, missing};
    result.missing = powerless(result);
    return result;
}

Value from_argb(uint32_t value) {
    return {Space::srgb,
            {double((value >> 16) & 255) / 255, double((value >> 8) & 255) / 255, double(value & 255) / 255},
            double(value >> 24) / 255,
            0};
}
Value output_srgb(Value value) {
    for (unsigned i = 0; i < 3; ++i)
        if (value.missing & (1u << i))
            value.channels[i] = 0;
    if (value.missing & 8)
        value.alpha = 0;
    value.missing = 0;
    auto rgb = convert(value, Space::srgb);
    const auto in_gamut = [](const Value &c) {
        return std::all_of(c.channels.begin(), c.channels.end(), [](double n) { return n >= -1e-7 && n <= 1 + 1e-7; });
    };
    const auto clip = [](Value c) {
        for (auto &n : c.channels)
            n = std::clamp(n, 0., 1.);
        return c;
    };
    if (!in_gamut(rgb)) {
        auto polar_value = convert(value, Space::oklch);
        if (polar_value.channels[0] >= 1)
            rgb.channels = {1, 1, 1};
        else if (polar_value.channels[0] <= 0)
            rgb.channels = {0, 0, 0};
        else {
            const auto delta = [](const Value &a, const Value &b) {
                const auto x = convert(a, Space::oklab), y = convert(b, Space::oklab);
                return std::hypot(std::hypot(x.channels[0] - y.channels[0], x.channels[1] - y.channels[1]),
                                  x.channels[2] - y.channels[2]);
            };
            auto clipped = clip(rgb);
            if (delta(clipped, polar_value) >= .02) {
                double lo = 0, hi = polar_value.channels[1];
                bool lower_in = true;
                for (unsigned step = 0; hi - lo > .0001 && step < 64; ++step) {
                    polar_value.channels[1] = (lo + hi) / 2;
                    auto next = convert(polar_value, Space::srgb);
                    if (lower_in && in_gamut(next)) {
                        lo = polar_value.channels[1];
                        continue;
                    }
                    clipped = clip(next);
                    const auto error = delta(clipped, polar_value);
                    if (error < .02) {
                        if (.02 - error < .0001)
                            break;
                        lower_in = false;
                        lo = polar_value.channels[1];
                    } else
                        hi = polar_value.channels[1];
                }
            }
            rgb = clipped;
        }
    }
    rgb.alpha=value.alpha;
    return rgb;
}
uint32_t argb(Value value) {
    const auto rgb=output_srgb(value);
    const auto byte = [](double n) {
        return static_cast<uint32_t>(std::floor(std::clamp(n, 0., 1.) * 255 + .5 + 1e-9));
    };
    if (byte(rgb.alpha) == 0)
        return 0;
    return byte(rgb.alpha) << 24 | byte(rgb.channels[0]) << 16 | byte(rgb.channels[1]) << 8 | byte(rgb.channels[2]);
}

struct Expression {
    enum Kind { literal, current, mix, light_dark } kind = literal;
    Value value;
    Color first, second;
    double weight = .5, alpha_multiplier = 1;
    unsigned hue_method = 0;
};
namespace {
using Type = lxb_css_syntax_token_type_t;
struct Token {
    Type type;
    std::string text;
    size_t end = 0;
    bool space = false;
};
struct Number {
    double value;
    unsigned kind;
}; // number, percentage, angle
struct Parser {
    std::vector<Token> tokens;
    size_t nodes = 0;
    bool tokenize(std::string_view source) {
        if (source.size() > 65536)
            return false;
        auto *lexer = lxb_css_syntax_tokenizer_create();
        if (!lexer)
            throw std::bad_alloc();
        struct End {
            lxb_css_syntax_tokenizer_t *p;
            ~End() { lxb_css_syntax_tokenizer_destroy(p); }
        } end{lexer};
        if (lxb_css_syntax_tokenizer_init(lexer) != LXB_STATUS_OK)
            throw std::bad_alloc();
        lxb_css_syntax_tokenizer_buffer_set(lexer, reinterpret_cast<const lxb_char_t *>(source.data()), source.size());
        std::vector<size_t> stack;
        bool space = false;
        while (auto *t = lxb_css_syntax_token(lexer)) {
            if (t->type == LXB_CSS_SYNTAX_TOKEN__EOF)
                break;
            if (t->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE || t->type == LXB_CSS_SYNTAX_TOKEN_COMMENT) {
                space |= t->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE;
                lxb_css_syntax_token_consume(lexer);
                continue;
            }
            const auto *raw = lxb_css_syntax_token_base(t);
            std::string text(reinterpret_cast<const char *>(raw->begin), raw->length);
            if (t->type == LXB_CSS_SYNTAX_TOKEN_IDENT || t->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
                t->type == LXB_CSS_SYNTAX_TOKEN_HASH) {
                const auto *id = lxb_css_syntax_token_ident(t);
                text.assign(reinterpret_cast<const char *>(id->data), id->length);
            }
            tokens.push_back({t->type, lower(std::move(text)), 0, space});
            space = false;
            if (tokens.size() > 8192)
                return false;
            const auto i = tokens.size() - 1;
            if (t->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || t->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
                if (stack.size() >= 64)
                    return false;
                stack.push_back(i);
            } else if (t->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
                if (stack.empty())
                    return false;
                tokens[stack.back()].end = i;
                stack.pop_back();
            }
            lxb_css_syntax_token_consume(lexer);
        }
        if (lexer->status == LXB_STATUS_ERROR_MEMORY_ALLOCATION)
            throw std::bad_alloc();
        return lexer->status == LXB_STATUS_OK && stack.empty();
    }
    bool is(size_t i, std::string_view text) const { return i < tokens.size() && tokens[i].text == text; }
    std::optional<Number> atom(size_t &i, size_t end, double percent, bool angle, unsigned depth) const {
        if (i >= end || depth > 32)
            return {};
        const auto &token = tokens[i];
        if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
            if (token.end >= end)
                return {};
            const auto name = token.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS ? "calc" : token.text;
            if (name != "calc" && name != "min" && name != "max" && name != "clamp")
                return {};
            const auto close = token.end;
            ++i;
            std::vector<Number> values;
            do {
                auto v = sum(i, close, percent, angle, depth + 1);
                if (!v)
                    return {};
                values.push_back(*v);
                if (i == close)
                    break;
                if (!is(i++, ","))
                    return {};
                if (i == close)
                    return {};
            } while (i < close);
            if (i != close || values.empty() || (name == "calc" && values.size() != 1) ||
                (name == "clamp" && values.size() != 3))
                return {};
            if (std::any_of(values.begin(), values.end(),
                            [&](const Number &n) { return n.kind != values.front().kind; }))
                return {};
            i = close + 1;
            const auto less = [](const Number &a, const Number &b) { return a.value < b.value; };
            const auto value = name == "min"     ? std::min_element(values.begin(), values.end(), less)->value
                               : name == "max"   ? std::max_element(values.begin(), values.end(), less)->value
                               : name == "clamp" ? std::max(values[0].value, std::min(values[1].value, values[2].value))
                                                 : values[0].value;
            return Number{value, values.front().kind};
        }
        if (token.type != LXB_CSS_SYNTAX_TOKEN_NUMBER && token.type != LXB_CSS_SYNTAX_TOKEN_PERCENTAGE &&
            token.type != LXB_CSS_SYNTAX_TOKEN_DIMENSION)
            return {};
        auto text = std::string_view(token.text);
        if (text.starts_with('+'))
            text.remove_prefix(1);
        double value;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || !std::isfinite(value) || std::abs(value) > 1e9)
            return {};
        const std::string_view unit(parsed.ptr, size_t(text.data() + text.size() - parsed.ptr));
        unsigned kind = 0;
        if (unit == "%") {
            value *= percent / 100;
            kind = 1;
        } else if (!unit.empty()) {
            kind = 2;
            if (unit == "turn")
                value *= 360;
            else if (unit == "grad")
                value *= .9;
            else if (unit == "rad")
                value *= 180 / pi;
            else if (unit != "deg")
                return {};
        }
        ++i;
        return Number{value, kind};
    }
    std::optional<Number> product(size_t &i, size_t end, double percent, bool angle, unsigned depth) const {
        auto value = atom(i, end, percent, angle, depth);
        if (!value)
            return {};
        while (i < end && (is(i, "*") || is(i, "/"))) {
            const bool divide = is(i++, "/");
            auto right = atom(i, end, percent, angle, depth);
            if (!right || (divide && right->value == 0))
                return {};
            if (divide) {
                if (right->kind && right->kind != value->kind)
                    return {};
                if (right->kind)
                    value->kind = 0;
                value->value /= right->value;
            } else {
                if (value->kind && right->kind)
                    return {};
                value->kind = std::max(value->kind, right->kind);
                value->value *= right->value;
            }
        }
        return std::isfinite(value->value) ? value : std::nullopt;
    }
    std::optional<Number> sum(size_t &i, size_t end, double percent, bool angle, unsigned depth) const {
        auto value = product(i, end, percent, angle, depth);
        if (!value)
            return {};
        while (i < end && (is(i, "+") || is(i, "-"))) {
            if (!tokens[i].space || i + 1 == end || !tokens[i + 1].space)
                return {};
            const bool sub = is(i++, "-");
            auto right = product(i, end, percent, angle, depth);
            if (!right || right->kind != value->kind)
                return {};
            value->value += sub ? -right->value : right->value;
        }
        return std::isfinite(value->value) ? value : std::nullopt;
    }
    std::optional<double> component(size_t &i, size_t end, double percent, bool angle, unsigned bit, unsigned &missing,
                                    bool legacy, unsigned *kind = nullptr) const {
        if (is(i, "none")) {
            if (legacy)
                return {};
            missing |= bit;
            ++i;
            return 0;
        }
        const auto value = atom(i, end, percent, angle, 0);
        if (!value || (angle && value->kind == 1) || (!angle && value->kind == 2))
            return {};
        if (kind)
            *kind = value->kind;
        return value->value;
    }
    static std::optional<Space> space(std::string_view text) {
        if (text == "display-p3-linear")
            return Space::linear_p3;
        constexpr std::pair<std::string_view, Space> names[]{{"srgb", Space::srgb},
                                                             {"srgb-linear", Space::linear_srgb},
                                                             {"display-p3", Space::p3},
                                                             {"a98-rgb", Space::a98},
                                                             {"prophoto-rgb", Space::prophoto},
                                                             {"rec2020", Space::rec2020},
                                                             {"xyz", Space::xyz65},
                                                             {"xyz-d65", Space::xyz65},
                                                             {"xyz-d50", Space::xyz50},
                                                             {"lab", Space::lab},
                                                             {"lch", Space::lch},
                                                             {"oklab", Space::oklab},
                                                             {"oklch", Space::oklch},
                                                             {"hsl", Space::hsl},
                                                             {"hwb", Space::hwb}};
        for (const auto &[name, value] : names)
            if (name == text)
                return value;
        return {};
    }
    Color color(size_t &i, size_t end, unsigned depth = 0) {
        if (i >= end || depth > 32 || ++nodes > 1024)
            return {};
        auto out = std::make_shared<Expression>();
        const auto token = tokens[i++];
        if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            if (token.text == "currentcolor") {
                out->kind = Expression::current;
                return out;
            }
            SkColor value;
            if (token.text == "transparent")
                value = 0;
            else if (token.text == "rebeccapurple")
                value = 0xff663399;
            else if (!SkParse::FindNamedColor(token.text.data(), token.text.size(), &value))
                return {};
            out->value = from_argb(value);
            return out;
        }
        if (token.type == LXB_CSS_SYNTAX_TOKEN_HASH) {
            auto hex = std::string_view(token.text);
            if (hex.starts_with('#'))
                hex.remove_prefix(1);
            if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8)
                return {};
            unsigned raw = 0;
            const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), raw, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size())
                return {};
            if (hex.size() < 5) {
                const unsigned a = hex.size() == 4 ? (raw & 15) * 17 : 255;
                if (hex.size() == 4)
                    raw >>= 4;
                out->value =
                    from_argb(a << 24 | ((raw >> 8) & 15) * 17 << 16 | ((raw >> 4) & 15) * 17 << 8 | (raw & 15) * 17);
            } else
                out->value = from_argb(hex.size() == 6 ? 0xff000000 | raw : (raw >> 8) | (raw & 255) << 24);
            return out;
        }
        if (token.type != LXB_CSS_SYNTAX_TOKEN_FUNCTION || token.end >= end)
            return {};
        const auto close = token.end;
        if (token.text == "light-dark") {
            out->kind = Expression::light_dark;
            out->first = color(i, close, depth + 1);
            if (!out->first || !is(i++, ","))
                return {};
            out->second = color(i, close, depth + 1);
            if (!out->second || i != close)
                return {};
            i = close + 1;
            return out;
        }
        if (token.text == "color-mix") {
            out->kind = Expression::mix;
            out->value.space = Space::oklab;
            if (is(i, "in")) {
                ++i;
                if (i >= close)
                    return {};
                const auto selected = space(tokens[i++].text);
                if (!selected)
                    return {};
                out->value.space = *selected;
                if (i < close && !is(i, ",")) {
                    if (angle_index(*selected) < 0)
                        return {};
                    if (is(i, "shorter"))
                        out->hue_method = 0;
                    else if (is(i, "longer"))
                        out->hue_method = 1;
                    else if (is(i, "increasing"))
                        out->hue_method = 2;
                    else if (is(i, "decreasing"))
                        out->hue_method = 3;
                    else
                        return {};
                    ++i;
                    if (!is(i++, "hue"))
                        return {};
                }
                if (!is(i++, ","))
                    return {};
            }
            std::optional<double> weight[2];
            for (unsigned part = 0; part < 2; ++part) {
                if (i < close && tokens[i].type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE) {
                    const auto n = atom(i, close, 1, false, 0);
                    if (!n)
                        return {};
                    weight[part] = n->value;
                }
                auto value = color(i, close, depth + 1);
                if (!value)
                    return {};
                if (i < close && tokens[i].type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE) {
                    if (weight[part])
                        return {};
                    const auto n = atom(i, close, 1, false, 0);
                    if (!n)
                        return {};
                    weight[part] = n->value;
                }
                if (weight[part] && (*weight[part] < 0 || *weight[part] > 1))
                    return {};
                (part ? out->second : out->first) = std::move(value);
                if (part == 0 && !is(i++, ","))
                    return {};
            }
            if (i != close)
                return {};
            if (!weight[0] && !weight[1])
                weight[0] = weight[1] = .5;
            else if (!weight[0])
                weight[0] = 1 - *weight[1];
            else if (!weight[1])
                weight[1] = 1 - *weight[0];
            const auto total = *weight[0] + *weight[1];
            if (total <= 0)
                return {};
            out->weight = *weight[0] / total;
            out->alpha_multiplier = std::min(total, 1.);
            i = close + 1;
            return out;
        }
        const bool rgb = token.text == "rgb" || token.text == "rgba", hsl = token.text == "hsl" || token.text == "hsla";
        auto selected = space(token.text);
        if (rgb)
            selected = Space::srgb;
        if (hsl)
            selected = Space::hsl;
        if (token.text == "color") {
            if (i >= close)
                return {};
            selected = space(tokens[i++].text);
            if (!selected || *selected == Space::lab || *selected == Space::lch || *selected == Space::oklab ||
                *selected == Space::oklch || angle_index(*selected) >= 0)
                return {};
        } else if (!rgb && !hsl && token.text != "hwb" && token.text != "lab" && token.text != "lch" &&
                   token.text != "oklab" && token.text != "oklch")
            return {};
        if (!selected)
            return {};
        out->value.space = *selected;
        bool legacy = false;
        for (size_t j = i; j < close; ++j) {
            if (tokens[j].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
                j = tokens[j].end;
                continue;
            }
            if (is(j, ",")) {
                legacy = true;
                break;
            }
        }
        if (legacy && !rgb && !hsl)
            return {};
        unsigned first_kind = 0;
        for (unsigned ch = 0; ch < 3; ++ch) {
            if (ch && legacy) {
                if (!is(i++, ","))
                    return {};
            }
            const double percent = rgb                                                  ? 255
                                   : *selected == Space::lab                            ? (ch ? 125 : 100)
                                   : *selected == Space::lch                            ? (ch == 0   ? 100
                                                                                           : ch == 1 ? 150
                                                                                                     : 360)
                                   : *selected == Space::oklab                          ? (ch ? .4 : 1)
                                   : *selected == Space::oklch                          ? (ch == 0   ? 1
                                                                                           : ch == 1 ? .4
                                                                                                     : 360)
                                   : *selected == Space::hsl || *selected == Space::hwb ? 100
                                                                                        : 1;
            unsigned kind = 0;
            const auto n = component(i, close, percent, static_cast<int>(ch) == angle_index(*selected), 1u << ch,
                                     out->value.missing, legacy, &kind);
            if (!n)
                return {};
            out->value.channels[ch] = *n;
            if (!ch)
                first_kind = kind;
            if (legacy && ((rgb && kind != first_kind) || (hsl && ch && kind != 1)))
                return {};
        }
        if (i < close) {
            if (!is(i++, legacy ? "," : "/"))
                return {};
            const auto n = component(i, close, 1, false, 8, out->value.missing, legacy);
            if (!n)
                return {};
            out->value.alpha = std::clamp(*n, 0., 1.);
        }
        if (i != close)
            return {};
        auto &c = out->value.channels;
        if (rgb)
            for (auto &n : c)
                n = std::clamp(n / 255, 0., 1.);
        if (*selected == Space::hsl || *selected == Space::hwb) {
            c[0] = hue(c[0]);
            c[1] = std::clamp(c[1], 0., 100.);
            c[2] = std::clamp(c[2], 0., 100.);
        }
        if (*selected == Space::lab || *selected == Space::lch)
            c[0] = std::clamp(c[0], 0., 100.);
        if (*selected == Space::oklab || *selected == Space::oklch)
            c[0] = std::clamp(c[0], 0., 1.);
        if (*selected == Space::lch || *selected == Space::oklch) {
            c[1] = std::max(0., c[1]);
            c[2] = hue(c[2]);
        }
        i = close + 1;
        return out;
    }
};
} // namespace

Color parse(std::string_view source) {
    Parser parser;
    if (!parser.tokenize(source))
        return {};
    size_t i = 0;
    auto result = parser.color(i, parser.tokens.size());
    return i == parser.tokens.size() ? result : Color{};
}
Value resolve(const Color &expression, const Context &context) {
    if (!expression)
        return {};
    const auto &e = *expression;
    if (e.kind == Expression::literal)
        return e.value;
    if (e.kind == Expression::current)
        return context.current;
    if (e.kind == Expression::light_dark)
        return resolve(context.dark ? e.second : e.first, context);
    auto result=interpolate(resolve(e.first,context),resolve(e.second,context),e.value.space,1-e.weight,e.hue_method);
    result.alpha*=e.alpha_multiplier;
    return result;
}
bool legacy(const Color& expression) {
    return expression&&(expression->kind==Expression::current||(expression->kind==Expression::literal&&
        (expression->value.space==Space::srgb||expression->value.space==Space::hsl)&&!expression->value.missing));
}
Value interpolate(Value first,Value second,Space space,double position,unsigned hue_method) {
    auto a=convert(first,space),b=convert(second,space);
    const auto weight=1-position;
    Value result{space, {0, 0, 0}, 1, a.missing & b.missing};
    for (unsigned i = 0; i < 4; ++i) {
        auto &x = i == 3 ? a.alpha : a.channels[i];
        auto &y = i == 3 ? b.alpha : b.channels[i];
        if (a.missing & (1u << i))
            x = y;
        if (b.missing & (1u << i))
            y = x;
        if (result.missing & (1u << i))
            x = y = 0;
    }
    const auto hi = angle_index(result.space);
    if (hi >= 0) {
        auto &x = a.channels[hi];
        auto &y = b.channels[hi];
        const auto difference = y - x;
        if (hue_method == 0) {
            if (difference > 180)
                x += 360;
            else if (difference < -180)
                y += 360;
        } else if (hue_method == 1) {
            if (difference > 0 && difference < 180)
                x += 360;
            else if (difference <= 0 && difference > -180)
                y += 360;
        } else if (hue_method == 2) {
            if (y < x)
                y += 360;
        } else if (x < y)
            x += 360;
    }
    const auto alpha = a.alpha * weight + b.alpha * (1 - weight);
    for (unsigned i = 0; i < 3; ++i) {
        result.channels[i] =
            static_cast<int>(i) == hi ? hue(a.channels[i] * weight + b.channels[i] * (1 - weight))
            : alpha ? (a.channels[i] * a.alpha * weight + b.channels[i] * b.alpha * (1 - weight)) / alpha
                    : 0;
    }
    result.alpha = alpha;
    return result;
}
bool property(std::string_view name) {
    return name == "color" || name == "background-color" || name == "border-top-color" ||
           name == "border-right-color" || name == "border-bottom-color" || name == "border-left-color" ||
           name == "outline-color";
}
std::string trim(std::string_view text) {
    const auto a = text.find_first_not_of(" \t\r\n\f");
    if (a == text.npos)
        return {};
    const auto b = text.find_last_not_of(" \t\r\n\f");
    return std::string(text.substr(a, b - a + 1));
}
bool valid(std::string_view value) {
    Parser parser;
    if (!parser.tokenize(value))
        return false;
    if (parser.tokens.size() == 1 && parser.tokens[0].type == LXB_CSS_SYNTAX_TOKEN_IDENT && wide(parser.tokens[0].text))
        return true;
    size_t i = 0;
    const auto expression = parser.color(i, parser.tokens.size());
    return expression && i == parser.tokens.size();
}
bool dark_scheme(std::string_view value, bool inherited, bool preferred) {
    const auto text = lower(trim(value));
    if (text.empty() || text == "inherit" || text == "unset")
        return inherited;
    if (text == "initial" || text == "normal")
        return false;
    Parser parser;
    if (!parser.tokenize(value))
        return inherited;
    if (parser.tokens.size() == 1) {
        const auto &key = parser.tokens.front().text;
        if (key == "initial" || key == "normal")
            return false;
        if (key == "inherit" || key == "unset")
            return inherited;
    }
    bool light = false, dark = false, first = false, found = false, only = false;
    for (const auto &token : parser.tokens) {
        if (token.type != LXB_CSS_SYNTAX_TOKEN_IDENT)
            return inherited;
        if (token.text == "normal" || wide(token.text))
            return inherited;
        if (token.text == "only") {
            if (only)
                return inherited;
            only = true;
            continue;
        }
        if (token.text == "light" || token.text == "dark") {
            if (!found) {
                first = token.text == "dark";
                found = true;
            }
            light |= token.text == "light";
            dark |= token.text == "dark";
        }
    }
    return preferred && dark ? true : !preferred && light ? false : found ? first : false;
}
bool valid_scheme(std::string_view value) {
    Parser parser;
    if (!parser.tokenize(value) || parser.tokens.empty())
        return false;
    if (parser.tokens.size() == 1 && parser.tokens[0].type == LXB_CSS_SYNTAX_TOKEN_IDENT &&
        (parser.tokens[0].text == "normal" || wide(parser.tokens[0].text)))
        return true;
    bool only = false, choice = false;
    for (const auto &token : parser.tokens) {
        if (token.type != LXB_CSS_SYNTAX_TOKEN_IDENT || token.text == "normal" || token.text == "default" ||
            wide(token.text))
            return false;
        if (token.text == "only") {
            if (only)
                return false;
            only = true;
        } else
            choice = true;
    }
    return choice;
}
} // namespace xl::detail::csscolor
