#include "grid_layout.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace xl::detail {
namespace {
using Kind = GridBreadthKind;
float fixed(GridBreadth value, float available) {
    if (value.kind == Kind::fixed)
        return value.value;
    if (value.kind == Kind::percentage && std::isfinite(available))
        return value.value * available;
    return NAN;
}
GridAxis expand(GridAxis axis, float available, float gap) {
    if (axis.repeat_start < 0)
        return axis;
    const int start = axis.repeat_start, length = axis.repeat_count;
    const auto pattern_size = [&](const GridTrack &track) {
        auto size = fixed(track.maximum, available);
        const auto minimum = fixed(track.minimum, available);
        if (!std::isfinite(size))
            size = minimum;
        else if (std::isfinite(minimum))
            size = std::max(minimum, size);
        return std::isfinite(size) ? std::max(1.0f, size) : 1.0f;
    };
    float pattern = 0, other = 0;
    for (size_t i = 0; i < axis.tracks.size(); ++i)
        (int(i) >= start && int(i) < start + length ? pattern : other) += pattern_size(axis.tracks[i]);
    const auto static_count = int(axis.tracks.size()) - length;
    const auto count =
        std::isfinite(available)
            ? std::max(1, int(std::clamp(std::floor((available - other - gap * float(static_count - 1)) /
                                                    (pattern + gap * length)),
                                         0.0f, float(grid_track_limit / length))))
            : 1;
    GridAxis result;
    result.auto_fit = axis.auto_fit;
    result.repeat_start = start;
    result.repeat_count = length * count;
    const auto append = [&](int index) {
        result.names.back().insert(result.names.back().end(), axis.names[index].begin(),
                                   axis.names[index].end());
        result.tracks.push_back(axis.tracks[index]);
        result.names.emplace_back();
    };
    for (int i = 0; i < start; ++i)
        append(i);
    for (int repeat = 0; repeat < count; ++repeat) {
        for (int i = start; i < start + length; ++i)
            append(i);
        result.names.back().insert(result.names.back().end(), axis.names[start + length].begin(),
                                   axis.names[start + length].end());
    }
    for (int i = start + length; i < int(axis.tracks.size()); ++i)
        append(i);
    result.names.back().insert(result.names.back().end(), axis.names.back().begin(), axis.names.back().end());
    if (result.tracks.size() > grid_track_limit)
        throw std::length_error("grid track limit");
    return result;
}
bool named(const GridAxis &axis, int line, const std::string &name) {
    if (line < 0 || line >= int(axis.names.size()))
        return true; // Implicit lines carry the requested name.
    const auto &names = axis.names[line];
    return std::find(names.begin(), names.end(), name) != names.end();
}
std::optional<int> resolve(const GridLine &line, const GridAxis &axis, bool end) {
    if (line.span || (!line.number && line.name.empty()))
        return {};
    const int count = int(axis.tracks.size());
    if (line.name.empty())
        return line.number > 0 ? line.number - 1 : count + 1 + line.number;
    if (!line.number) {
        const auto area = line.name + (end ? "-end" : "-start");
        for (int i = 0; i <= count; ++i) {
            const auto &names = axis.names[i];
            if (std::find(names.begin(), names.end(), area) != names.end())
                return i;
        }
    }
    int remaining = line.number ? std::abs(line.number) : 1;
    const int step = line.number < 0 ? -1 : 1;
    for (int index = step > 0 ? 0 : count; std::abs(index) <= grid_track_limit; index += step)
        if (named(axis, index, line.name) && --remaining == 0)
            return index;
    throw std::length_error("grid named line limit");
}
int span_end(const GridLine &span, const GridAxis &axis, int start, int step) {
    int remaining = std::max(1, span.number);
    for (int line = start + step; std::abs(line) <= grid_track_limit; line += step)
        if ((span.name.empty() || named(axis, line, span.name)) && --remaining == 0)
            return line;
    throw std::length_error("grid span limit");
}
struct Range {
    int start = 0, span = 1;
    bool automatic = true;
};
Range range(GridPlacement placement, const GridAxis &axis) {
    if (placement.start.span && placement.end.span)
        placement.end = {};
    auto start = resolve(placement.start, axis, false), end = resolve(placement.end, axis, true);
    if (start && end) {
        if (*end < *start)
            std::swap(start, end);
        if (*end == *start)
            *end = *start + 1;
        return {*start, *end - *start, false};
    }
    if (start)
        return {*start, placement.end.span ? span_end(placement.end, axis, *start, 1) - *start : 1, false};
    if (end) {
        const auto first = placement.start.span ? span_end(placement.start, axis, *end, -1) : *end - 1;
        return {first, *end - first, false};
    }
    return {0,
            placement.start.span ? std::max(1, placement.start.number)
            : placement.end.span ? std::max(1, placement.end.number)
                                 : 1,
            true};
}
struct Placement {
    Range columns, rows;
};
struct Extent {
    int before = 0, count = 0;
};
std::pair<Extent, Extent> place(const GridStyle &style, const GridAxis &columns, const GridAxis &rows,
                                std::vector<GridItem> &items) {
    std::vector<Placement> placements;
    int left = 0, top = 0, right = int(columns.tracks.size()), bottom = int(rows.tracks.size());
    for (const auto &item : items) {
        Placement next{range(item.columns, columns), range(item.rows, rows)};
        if (!next.columns.automatic) {
            left = std::min(left, next.columns.start);
            right = std::max(right, next.columns.start + next.columns.span);
        }
        if (!next.rows.automatic) {
            top = std::min(top, next.rows.start);
            bottom = std::max(bottom, next.rows.start + next.rows.span);
        }
        placements.push_back(next);
    }
    int minor_count = std::max(1, style.column_flow ? bottom - top : right - left);
    for (auto &item : placements) {
        if (!item.columns.automatic)
            item.columns.start -= left;
        if (!item.rows.automatic)
            item.rows.start -= top;
        const auto &minor = style.column_flow ? item.rows : item.columns;
        minor_count = std::max(minor_count, minor.span);
    }
    std::vector<unsigned char> occupied(grid_track_limit * grid_track_limit, 0);
    const auto fits = [&](int major, int minor, int major_span, int minor_span) {
        if (major < 0 || minor < 0 || major + major_span > grid_track_limit ||
            minor + minor_span > grid_track_limit)
            return false;
        for (int r = major; r < major + major_span; ++r)
            for (int c = minor; c < minor + minor_span; ++c)
                if (occupied[size_t(r * grid_track_limit + c)])
                    return false;
        return true;
    };
    const auto mark = [&](int major, int minor, int major_span, int minor_span) {
        if (major < 0 || minor < 0 || major + major_span > grid_track_limit ||
            minor + minor_span > grid_track_limit)
            throw std::length_error("grid placement limit");
        for (int r = major; r < major + major_span; ++r)
            for (int c = minor; c < minor + minor_span; ++c)
                occupied[size_t(r * grid_track_limit + c)] = 1;
    };
    for (auto &item : placements)
        if (!item.columns.automatic && !item.rows.automatic) {
            const auto &major = style.column_flow ? item.columns : item.rows;
            const auto &minor = style.column_flow ? item.rows : item.columns;
            mark(major.start, minor.start, major.span, minor.span);
        }
    std::vector<int> locked_cursor(grid_track_limit, 0);
    for (auto &item : placements) {
        auto &major = style.column_flow ? item.columns : item.rows;
        auto &minor = style.column_flow ? item.rows : item.columns;
        if (major.automatic || !minor.automatic)
            continue;
        if (major.start >= grid_track_limit)
            throw std::length_error("grid placement limit");
        minor.start = style.dense ? 0 : locked_cursor[major.start];
        while (!fits(major.start, minor.start, major.span, minor.span)) {
            if (++minor.start >= grid_track_limit)
                throw std::length_error("grid placement limit");
        }
        locked_cursor[major.start] = minor.start + minor.span;
        minor_count = std::max(minor_count, minor.start + minor.span);
        minor.automatic = false;
        mark(major.start, minor.start, major.span, minor.span);
    }
    int cursor_major = 0, cursor_minor = 0;
    for (auto &item : placements) {
        auto &major = style.column_flow ? item.columns : item.rows;
        auto &minor = style.column_flow ? item.rows : item.columns;
        if (!major.automatic)
            continue;
        if (style.dense)
            cursor_major = cursor_minor = 0;
        if (!minor.automatic) {
            if (minor.start < cursor_minor)
                ++cursor_major;
            cursor_minor = minor.start;
            while (!fits(cursor_major, cursor_minor, major.span, minor.span)) {
                if (++cursor_major >= grid_track_limit)
                    throw std::length_error("grid placement limit");
            }
        } else {
            while (true) {
                if (cursor_minor + minor.span > minor_count) {
                    cursor_minor = 0;
                    ++cursor_major;
                }
                if (fits(cursor_major, cursor_minor, major.span, minor.span))
                    break;
                if (cursor_major >= grid_track_limit)
                    throw std::length_error("grid placement limit");
                ++cursor_minor;
            }
            minor.start = cursor_minor;
        }
        major.start = cursor_major;
        mark(major.start, minor.start, major.span, minor.span);
    }
    right -= left;
    bottom -= top;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &next = placements[i];
        auto &item = items[i];
        item.column = next.columns.start;
        item.column_span = next.columns.span;
        item.row = next.rows.start;
        item.row_span = next.rows.span;
        right = std::max(right, item.column + item.column_span);
        bottom = std::max(bottom, item.row + item.row_span);
    }
    return {{-left, std::max(right, style.column_flow ? 1 : minor_count)},
            {-top, std::max(bottom, style.column_flow ? minor_count : 1)}};
}
struct Contribution {
    int start, span;
    float minimum, min_content, maximum;
};
struct AxisResult {
    std::vector<float> starts, sizes;
    float size = 0;
};
AxisResult size_axis(const GridAxis &axis, const std::vector<GridTrack> &implicit, Extent extent,
                     const std::vector<Contribution> &contributions, float available, float gap,
                     FlexAlign align, int intrinsic) {
    if (extent.count > grid_track_limit)
        throw std::length_error("grid track limit");
    const auto count = size_t(extent.count);
    struct Track {
        GridTrack definition;
        float base = 0, limit = INFINITY, flex = 0;
        bool collapsed = false;
    };
    std::vector<Track> tracks(count);
    for (int i = 0; i < extent.count; ++i) {
        const auto index = i - extent.before;
        auto &track = tracks[i];
        track.definition =
            index >= 0 && index < int(axis.tracks.size())
                ? axis.tracks[index]
                : implicit[size_t((index >= 0 ? index - int(axis.tracks.size())
                                              : index % int(implicit.size()) + int(implicit.size())) %
                                  int(implicit.size()))];
        if (axis.auto_fit && index >= axis.repeat_start && index < axis.repeat_start + axis.repeat_count) {
            track.collapsed = std::none_of(contributions.begin(), contributions.end(), [&](const auto &item) {
                return item.start <= i && item.start + item.span > i;
            });
            if (track.collapsed)
                continue;
        }
        const auto minimum = fixed(track.definition.minimum, available);
        const auto maximum = fixed(track.definition.maximum, available);
        track.base = std::isfinite(minimum) ? minimum : 0;
        if (std::isfinite(maximum) && !track.definition.fit_content)
            track.limit = std::max(track.base, maximum);
        if (track.definition.maximum.kind == Kind::fraction)
            track.flex = track.definition.maximum.value;
    }
    const auto gutters = [&](int start, int span) {
        int visible = 0;
        for (int i = start; i < start + span; ++i)
            visible += !tracks[i].collapsed;
        return gap * float(std::max(0, visible - 1));
    };
    auto ordered = contributions;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.span < b.span; });
    const auto distribute = [&](const Contribution &item, bool limits, float target) {
        auto used = gutters(item.start, item.span);
        std::vector<int> eligible;
        for (int i = item.start; i < item.start + item.span; ++i) {
            auto &track = tracks[i];
            used += limits ? (std::isfinite(track.limit) ? track.limit : track.base) : track.base;
            const auto breadth = limits ? track.definition.maximum : track.definition.minimum;
            if (!track.collapsed && !std::isfinite(fixed(breadth, available)) &&
                (!limits || breadth.kind != Kind::fraction))
                eligible.push_back(i);
            if (limits && track.definition.fit_content)
                eligible.push_back(i);
        }
        float remaining = std::max(0.0f, target - used);
        while (remaining > .0001f && !eligible.empty()) {
            const auto share = remaining / float(eligible.size());
            float consumed = 0;
            for (const auto index : eligible) {
                auto &track = tracks[index];
                auto &value = limits ? track.limit : track.base;
                if (!std::isfinite(value))
                    value = track.base;
                const auto cap = limits
                                     ? (track.definition.fit_content
                                            ? std::max(track.base, fixed(track.definition.maximum, available))
                                            : INFINITY)
                                 : track.definition.minimum.kind == Kind::min_content ||
                                         track.definition.minimum.kind == Kind::max_content
                                     ? INFINITY
                                     : track.limit;
                const auto added = std::max(0.0f, std::min(share, cap - value));
                value += added;
                consumed += added;
            }
            remaining -= consumed;
            eligible.erase(
                std::remove_if(eligible.begin(), eligible.end(),
                               [&](int index) {
                                   const auto &track = tracks[index];
                                   const auto cap =
                                       limits ? (track.definition.fit_content
                                                     ? std::max(track.base,
                                                                fixed(track.definition.maximum, available))
                                                     : INFINITY)
                                       : track.definition.minimum.kind == Kind::min_content ||
                                               track.definition.minimum.kind == Kind::max_content
                                           ? INFINITY
                                           : track.limit;
                                   return (limits ? track.limit : track.base) >= cap - .0001f;
                               }),
                eligible.end());
            if (consumed < .0001f)
                break;
        }
    };
    for (const auto &item : ordered) {
        float target = item.minimum;
        for (int i = item.start; i < item.start + item.span; ++i) {
            const auto min = tracks[i].definition.minimum.kind;
            if (min == Kind::min_content)
                target = std::max(target, item.min_content);
            if (min == Kind::max_content)
                target = std::max(target, item.maximum);
        }
        distribute(item, false, target);
        float maximum = item.min_content;
        for (int i = item.start; i < item.start + item.span; ++i)
            if (tracks[i].definition.maximum.kind != Kind::min_content)
                maximum = item.maximum;
        distribute(item, true, maximum);
    }
    for (auto &track : tracks) {
        if (track.collapsed) {
            track.base = track.limit = 0;
            continue;
        }
        if (!std::isfinite(track.limit) && track.definition.maximum.kind != Kind::fraction)
            track.limit = track.base;
        track.limit = std::max(track.base, track.limit);
    }
    const auto total = [&] {
        float size = gutters(0, extent.count);
        for (const auto &track : tracks)
            size += track.base;
        return size;
    };
    if (intrinsic != 1) {
        float free = std::isfinite(available) ? std::max(0.0f, available - total()) : INFINITY;
        std::vector<size_t> grow;
        for (size_t i = 0; i < count; ++i)
            if (tracks[i].limit > tracks[i].base && tracks[i].definition.maximum.kind != Kind::fraction)
                grow.push_back(i);
        while (free > .0001f && !grow.empty()) {
            const auto share = free / float(grow.size());
            float used = 0;
            for (const auto i : grow) {
                const auto add = std::min(share, tracks[i].limit - tracks[i].base);
                tracks[i].base += add;
                used += add;
            }
            if (std::isfinite(free))
                free -= used;
            grow.erase(std::remove_if(grow.begin(), grow.end(),
                                      [&](size_t i) { return tracks[i].base >= tracks[i].limit - .0001f; }),
                       grow.end());
        }
        float fraction = 0;
        if (std::isfinite(available)) {
            std::vector<bool> frozen(count, false);
            for (size_t pass = 0; pass <= count; ++pass) {
                float remaining = available - gutters(0, extent.count), factors = 0;
                for (size_t i = 0; i < count; ++i)
                    if (!tracks[i].flex || frozen[i])
                        remaining -= tracks[i].base;
                    else
                        factors += tracks[i].flex;
                fraction = std::max(0.0f, remaining) / std::max(1.0f, factors);
                bool changed = false;
                for (size_t i = 0; i < count; ++i)
                    if (tracks[i].flex && !frozen[i] && fraction * tracks[i].flex < tracks[i].base) {
                        frozen[i] = true;
                        changed = true;
                    }
                if (!changed)
                    break;
            }
        } else {
            for (const auto &track : tracks)
                if (track.flex)
                    fraction = std::max(fraction, track.base / std::max(1.0f, track.flex));
            for (const auto &item : contributions) {
                float need = item.maximum - gutters(item.start, item.span), factors = 0;
                for (int i = item.start; i < item.start + item.span; ++i)
                    if (tracks[i].flex)
                        factors += tracks[i].flex;
                    else
                        need -= tracks[i].base;
                if (factors)
                    fraction = std::max(fraction, need / std::max(1.0f, factors));
            }
        }
        for (auto &track : tracks)
            if (track.flex)
                track.base = std::max(track.base, fraction * track.flex);
    }
    if (!intrinsic && align == FlexAlign::stretch && std::isfinite(available) && available > total()) {
        size_t autos = 0;
        for (const auto &track : tracks)
            autos += !track.collapsed && track.definition.maximum.kind == Kind::automatic;
        const auto add = autos ? (available - total()) / float(autos) : 0;
        for (auto &track : tracks)
            if (!track.collapsed && track.definition.maximum.kind == Kind::automatic)
                track.base += add;
    }
    AxisResult result;
    result.size = total();
    result.starts.resize(count);
    result.sizes.resize(count);
    const auto free = std::isfinite(available) && !intrinsic ? available - result.size : 0;
    float cursor = align == FlexAlign::center                                ? free * .5f
                   : align == FlexAlign::end || align == FlexAlign::flow_end ? free
                                                                             : 0;
    const auto visible = size_t(
        std::count_if(tracks.begin(), tracks.end(), [](const auto &track) { return !track.collapsed; }));
    float spacing = 0;
    if (free > 0 && visible) {
        if (align == FlexAlign::between && visible > 1)
            spacing = free / float(visible - 1);
        if (align == FlexAlign::around) {
            spacing = free / float(visible);
            cursor = spacing * .5f;
        }
        if (align == FlexAlign::evenly)
            cursor = spacing = free / float(visible + 1);
    }
    bool previous = false;
    for (size_t i = 0; i < count; ++i) {
        if (!tracks[i].collapsed && previous)
            cursor += gap + spacing;
        result.starts[i] = cursor;
        result.sizes[i] = tracks[i].base;
        cursor += tracks[i].base;
        if (!tracks[i].collapsed)
            previous = true;
    }
    return result;
}
float area_size(const AxisResult &axis, int start, int span) {
    return axis.starts[start + span - 1] + axis.sizes[start + span - 1] - axis.starts[start];
}
} // namespace
GridResult grid_layout(const GridStyle &style, std::vector<GridItem> &items, float width, float height,
                       float column_gap, float row_gap, float row_gap_ratio, FlexAlign justify_content,
                       FlexAlign align_content, int intrinsic) {
    const auto columns = expand(style.columns, width, column_gap), rows = expand(style.rows, height, row_gap);
    const auto [column_extent, row_extent] = place(style, columns, rows, items);
    std::vector<Contribution> widths;
    for (const auto &item : items) {
        auto minimum = item.minimum;
        if (item.automatic_minimum && item.column_span > 1) {
            for (int i = item.column; i < item.column + item.column_span; ++i) {
                const auto index = i - column_extent.before;
                const auto track = index >= 0 && index < int(columns.tracks.size())
                                       ? columns.tracks[index]
                                       : style.auto_columns[size_t((index % int(style.auto_columns.size()) +
                                                                    int(style.auto_columns.size())) %
                                                                   int(style.auto_columns.size()))];
                if (track.maximum.kind == Kind::fraction) {
                    minimum = 0;
                    break;
                }
            }
        }
        widths.push_back({item.column, item.column_span, minimum, item.min_content, item.max_content});
    }
    const auto horizontal = size_axis(columns, style.auto_columns, column_extent, widths, width, column_gap,
                                      justify_content, intrinsic);
    if (intrinsic)
        {GridResult result;result.width=horizontal.size;return result;}
    std::vector<Contribution> heights;
    std::vector<float> baselines(size_t(row_extent.count), 0), descents(size_t(row_extent.count), 0);
    std::vector<GridMeasure> measurements;
    for (auto &item : items) {
        item.x = horizontal.starts[item.column];
        item.width = area_size(horizontal, item.column, item.column_span);
        const auto measured = item.measure(item.width);
        measurements.push_back(measured);
        if (item.baseline) {
            baselines[item.row] = std::max(baselines[item.row], measured.baseline);
            if (item.row_span == 1)
                descents[item.row] = std::max(descents[item.row], measured.height - measured.baseline);
        }
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        const auto &measured = measurements[i];
        const auto height = measured.height + (item.baseline ? baselines[item.row] - measured.baseline : 0);
        heights.push_back({item.row, item.row_span, height, height, height});
    }
    for (int row = 0; row < row_extent.count; ++row)
        if (baselines[row] + descents[row] > 0)
            heights.push_back({row, 1, baselines[row] + descents[row], baselines[row] + descents[row],
                               baselines[row] + descents[row]});
    auto vertical = size_axis(rows, style.auto_rows, row_extent, heights, height, row_gap, align_content, 0);
    const auto natural_height = vertical.size;
    const auto percentage = [](const auto &track) {
        return track.minimum.kind == Kind::percentage || track.maximum.kind == Kind::percentage;
    };
    if (!std::isfinite(height) &&
        (std::isfinite(row_gap_ratio) || std::any_of(rows.tracks.begin(), rows.tracks.end(), percentage) ||
         std::any_of(style.auto_rows.begin(), style.auto_rows.end(), percentage))) {
        if (std::isfinite(row_gap_ratio))
            row_gap = natural_height * row_gap_ratio;
        vertical =
            size_axis(rows, style.auto_rows, row_extent, heights, natural_height, row_gap, align_content, 0);
    }
    for (auto &item : items) {
        item.y = vertical.starts[item.row];
        item.height = area_size(vertical, item.row, item.row_span);
    }
    GridResult result;
    result.width=std::isfinite(width)?width:horizontal.size;result.height=std::isfinite(height)?height:natural_height;
    result.columns=columns;result.rows=rows;result.column_before=column_extent.before;result.row_before=row_extent.before;
    result.column_starts=horizontal.starts;result.row_starts=vertical.starts;
    for(size_t i=0;i<horizontal.starts.size();++i)result.column_ends.push_back(horizontal.starts[i]+horizontal.sizes[i]);
    for(size_t i=0;i<vertical.starts.size();++i)result.row_ends.push_back(vertical.starts[i]+vertical.sizes[i]);
    return result;
}
std::array<float,4> grid_positioned_area(const GridResult& grid,GridPlacement columns,GridPlacement rows,float width,float height,bool rtl) {
    (void)height;
    const auto axis=[](GridPlacement placement,const GridAxis& definition,int before,const std::vector<float>& starts,const std::vector<float>& ends) {
        auto start=resolve(placement.start,definition,false),end=resolve(placement.end,definition,true);
        if(start&&placement.end.span)end=span_end(placement.end,definition,*start,1);
        if(end&&placement.start.span)start=span_end(placement.start,definition,*end,-1);
        if(start&&end){if(*start>*end)std::swap(start,end);if(*start==*end)end.reset();}
        // Out-of-flow placement never creates implicit tracks.
        if(start&&(*start+before<0||*start+before>int(starts.size())))start.reset();
        if(end&&(*end+before<0||*end+before>int(ends.size())))end.reset();
        const auto edge=[&](int line,bool ending) {
            const auto index=line+before;
            if(index==int(starts.size()))return ends.empty()?0:ends.back();
            if(ending&&index>0)return ends[index-1];
            return starts.empty()?0:starts[index];
        };
        return std::pair<float,float>{start?edge(*start,false):NAN,end?edge(*end,true):NAN};
    };
    auto x=axis(columns,grid.columns,grid.column_before,grid.column_starts,grid.column_ends);
    const auto y=axis(rows,grid.rows,grid.row_before,grid.row_starts,grid.row_ends);
    if(rtl)x={width-x.second,width-x.first};
    return {x.first,y.first,x.second,y.second};
}
} // namespace xl::detail
