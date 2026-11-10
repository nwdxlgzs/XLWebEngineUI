#include "flex_layout.h"
#include <algorithm>
#include <numeric>

namespace xl::detail {
namespace {
float margin(float value) { return std::isnan(value) ? 0 : value; }
float clamp(float value, float minimum, float maximum) { return std::max(minimum, std::min(value, maximum)); }
struct Line {
    size_t first, end;
    float cross = 0, baseline = 0;
};
std::pair<float, float> packing(FlexAlign align, float free, size_t count) {
    if (align == FlexAlign::center)
        return {free * .5f, 0};
    if (align == FlexAlign::end)
        return {free, 0};
    if (free <= 0) {
        if (align == FlexAlign::around || align == FlexAlign::evenly)
            return {free * .5f, 0};
        return {0, 0};
    }
    if (align == FlexAlign::between && count > 1)
        return {0, free / float(count - 1)};
    if (align == FlexAlign::around && count)
        return {free / float(count) * .5f, free / float(count)};
    if (align == FlexAlign::evenly)
        return {free / float(count + 1), free / float(count + 1)};
    return {0, 0};
}
void flexible(std::vector<FlexItem> &items, const Line &line, float available, float gap) {
    const auto count = line.end - line.first;
    float occupied = gap * float(count - 1), hypothetical = occupied;
    for (size_t i = line.first; i < line.end; ++i) {
        const auto &item = items[i];
        occupied += margin(item.before) + margin(item.after) + item.base;
        hypothetical +=
            margin(item.before) + margin(item.after) + clamp(item.base, item.minimum, item.maximum);
    }
    const bool growing = hypothetical < available;
    const auto initial_free = available - occupied;
    std::vector<bool> frozen(count, false);
    for (size_t i = line.first; i < line.end; ++i) {
        auto &item = items[i];
        const auto hypo = clamp(item.base, item.minimum, item.maximum);
        frozen[i - line.first] =
            (growing ? item.grow : item.shrink) == 0 || (growing ? item.base > hypo : item.base < hypo);
        item.main = frozen[i - line.first] ? hypo : item.base;
    }
    // Each iteration freezes at least one item. Size violations redistribute
    // remaining space instead of applying a single proportional pass.
    for (size_t iteration = 0; iteration <= count; ++iteration) {
        float free = available - gap * float(count - 1), factors = 0, weight = 0;
        size_t active = 0;
        for (size_t i = line.first; i < line.end; ++i) {
            const auto &item = items[i];
            free -=
                margin(item.before) + margin(item.after) + (frozen[i - line.first] ? item.main : item.base);
            if (frozen[i - line.first])
                continue;
            ++active;
            factors += growing ? item.grow : item.shrink;
            weight += growing ? item.grow : item.shrink * item.base;
        }
        if (!active)
            break;
        if (factors < 1 && std::abs(initial_free * factors) < std::abs(free))
            free = initial_free * factors;
        float violation = 0;
        std::vector<float> violations(count, 0);
        for (size_t i = line.first; i < line.end; ++i) {
            if (frozen[i - line.first])
                continue;
            auto &item = items[i];
            const auto share = weight > 0 ? (growing ? item.grow : item.shrink * item.base) / weight : 0;
            const auto proposed = item.base + free * share;
            item.main = clamp(proposed, item.minimum, item.maximum);
            violations[i - line.first] = item.main - proposed;
            violation += item.main - proposed;
        }
        for (size_t i = line.first; i < line.end; ++i) {
            const auto value = violations[i - line.first];
            if (std::abs(violation) < .0001f || (violation > 0 ? value > 0 : value < 0))
                frozen[i - line.first] = true;
        }
    }
}
} // namespace
FlexResult flex_layout(const FlexStyle &style, std::vector<FlexItem> &items, float main, float cross) {
    if (items.empty())
        return {std::isfinite(main) ? main : 0, std::isfinite(cross) ? cross : 0};
    const auto gap = style.column ? style.row_gap : style.column_gap;
    const auto cross_gap = style.column ? style.column_gap : style.row_gap;
    std::vector<Line> lines;
    size_t first = 0;
    float used = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        const auto outer =
            clamp(item.base, item.minimum, item.maximum) + margin(item.before) + margin(item.after);
        if (style.wrap && i > first && std::isfinite(main) && used + gap + outer > main + .0001f) {
            lines.push_back({first, i});
            first = i;
            used = 0;
        }
        used += (i > first ? gap : 0) + outer;
    }
    lines.push_back({first, items.size()});
    if (!std::isfinite(main)) {
        main = 0;
        for (const auto &line : lines) {
            auto length = gap * float(line.end - line.first - 1);
            for (auto i = line.first; i < line.end; ++i)
                length += clamp(items[i].base, items[i].minimum, items[i].maximum) + margin(items[i].before) +
                          margin(items[i].after);
            main = std::max(main, length);
        }
    }
    const auto item_align = [&](const FlexItem &item) {
        return item.align == FlexAlign::automatic ? style.items : item.align;
    };
    for (auto &line : lines) {
        flexible(items, line, main, gap);
        float baseline_descent = 0;
        for (auto i = line.first; i < line.end; ++i) {
            auto &item = items[i];
            const auto measured = item.measure(item.main, NAN);
            item.cross = clamp(measured.cross, item.cross_min, item.cross_max);
            item.baseline = measured.baseline;
            line.cross =
                std::max(line.cross, item.cross + margin(item.cross_before) + margin(item.cross_after));
            if (item_align(item) == FlexAlign::baseline && !std::isnan(item.cross_before) &&
                !std::isnan(item.cross_after)) {
                line.baseline = std::max(line.baseline, item.baseline + item.cross_before);
                baseline_descent = std::max(baseline_descent, item.cross - item.baseline + item.cross_after);
            }
        }
        line.cross = std::max(line.cross, line.baseline + baseline_descent);
    }
    auto total_cross = cross_gap * float(lines.size() - 1);
    for (const auto &line : lines)
        total_cross += line.cross;
    if (!std::isfinite(cross))
        cross = total_cross;
    if (!style.wrap)
        lines.front().cross = cross;
    else if (style.content == FlexAlign::stretch && cross > total_cross)
        for (auto &line : lines)
            line.cross += (cross - total_cross) / float(lines.size());
    total_cross = cross_gap * float(lines.size() - 1);
    for (const auto &line : lines)
        total_cross += line.cross;
    auto [cross_cursor, line_spacing] = packing(style.content, cross - total_cross, lines.size());
    if (!style.wrap)
        cross_cursor = line_spacing = 0;
    for (const auto &line : lines) {
        auto occupied = gap * float(line.end - line.first - 1);
        size_t autos = 0;
        for (auto i = line.first; i < line.end; ++i) {
            occupied += items[i].main + margin(items[i].before) + margin(items[i].after);
            autos += size_t(std::isnan(items[i].before)) + size_t(std::isnan(items[i].after));
        }
        const auto auto_margin = autos ? std::max(0.0f, main - occupied) / float(autos) : 0;
        auto [cursor, spacing] =
            packing(style.justify, auto_margin > 0 ? 0 : main - occupied, line.end - line.first);
        for (auto i = line.first; i < line.end; ++i) {
            auto &item = items[i];
            const auto align = item_align(item);
            const bool auto_before = std::isnan(item.cross_before), auto_after = std::isnan(item.cross_after);
            auto cross_before = margin(item.cross_before), cross_after = margin(item.cross_after);
            if (align == FlexAlign::stretch && item.cross_auto && !auto_before && !auto_after)
                item.cross = clamp(std::max(0.0f, line.cross - cross_before - cross_after), item.cross_min,
                                   item.cross_max);
            const auto free = line.cross - item.cross - cross_before - cross_after;
            float offset = cross_before;
            if (auto_before || auto_after) {
                if (free > 0 && auto_before)
                    offset += free / float(size_t(auto_before) + size_t(auto_after));
            } else if (align == FlexAlign::center)
                offset += free * .5f;
            else if (align == FlexAlign::end)
                offset += free;
            else if (align == FlexAlign::baseline)
                offset = line.baseline - item.baseline;
            cursor += std::isnan(item.before) ? auto_margin : item.before;
            item.main_position = cursor;
            item.cross_position = cross_cursor + offset;
            item.measure(item.main, item.cross);
            cursor += item.main + (std::isnan(item.after) ? auto_margin : item.after) + gap + spacing;
        }
        cross_cursor += line.cross + cross_gap + line_spacing;
    }
    return {main, cross};
}
} // namespace xl::detail
