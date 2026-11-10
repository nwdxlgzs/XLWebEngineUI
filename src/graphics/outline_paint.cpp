#include "outline_paint.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include "include/effects/SkDashPathEffect.h"
#include "sk_rounded.h"
#include "include/pathops/SkPathOps.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace xl::detail {
namespace {
SkPath combine(const SkPath& a, const SkPath& b, SkPathOp op) {
    const auto path = Op(a, b, op);
    if (!path) throw std::runtime_error("outline path operation failed");
    return *path;
}
SkPath offset(const SkPath& source, float amount) {
    if (!amount || source.isEmpty()) return source;
    SkPaint stroke;
    stroke.setStyle(SkPaint::kStroke_Style);
    stroke.setStrokeWidth(std::abs(amount) * 2);
    stroke.setStrokeJoin(SkPaint::kMiter_Join);
    const auto band = skpathutils::FillPathWithPaint(source, stroke);
    return combine(source, band, amount > 0 ? kUnion_SkPathOp : kDifference_SkPathOp);
}
uint32_t shade(uint32_t color, bool light) {
    uint32_t result = 0xff000000;
    for (unsigned shift : {0u, 8u, 16u}) {
        const auto value = (color >> shift) & 255;
        result |= (light ? value + (255 - value) / 3 : value * 2 / 3) << shift;
    }
    return result;
}
struct Component {
    SkPath base;
    SkRect bounds;
    float offset;
    std::optional<RoundedRect> rounded;
    SkPath edge(float distance) const {
        if(rounded&&offset>=-bounds.width()/2&&offset>=-bounds.height()/2) {
            const auto amount=offset+distance;const auto rect=rounded->inset(-amount,-amount,-amount,-amount);
            if(rect.rect.width<=0||rect.rect.height<=0)return {};
            return SkPathBuilder().addRRect(sk_rounded(rect)).detach();
        }
        if (offset >= -bounds.width() / 2 && offset >= -bounds.height() / 2)
            return xl::detail::offset(base, offset + distance);
        // Extreme negative offsets clamp each axis separately, keeping a visible
        // minimum of twice the outline width even for a disconnected fragment.
        const float dx = std::max(offset, -bounds.width() / 2) + distance;
        const float dy = std::max(offset, -bounds.height() / 2) + distance;
        const auto next = bounds.makeOutset(dx, dy);
        if (next.width() <= 0 || next.height() <= 0) return {};
        SkMatrix matrix;
        matrix.setRectToRect(bounds, next, SkMatrix::kFill_ScaleToFit);
        return base.makeTransform(matrix);
    }
};
}
bool outline_equal(const OutlinePaint& a, const OutlinePaint& b) {
    if(a.radii!=b.radii)return false;
    if (a.style != b.style || a.clip != b.clip || a.fragments.size() != b.fragments.size()) return false;
    for (size_t i = 0; i < a.fragments.size(); ++i) {
        const auto& x = a.fragments[i]; const auto& y = b.fragments[i];
        if (x.x != y.x || x.y != y.y || x.width != y.width || x.height != y.height) return false;
    }
    return true;
}
struct OutlineShape::Impl {
    OutlineStyle style;
    Clip clip;
    SkPath ring, outer, middle, first, last, half_outer, half_inner;
    sk_sp<SkPathEffect> dash;
    struct Shade { SkPath path; uint32_t color; };
    std::vector<Shade> shades;
    xl_node_bounds ink{};
};
OutlineShape::OutlineShape(const OutlinePaint& source) : impl_(std::make_unique<Impl>()) {
    auto& out = *impl_;
    out.style = source.style; out.clip = source.clip;
    if (!source.style.visible() || source.fragments.empty()) return;
    if (source.fragments.size() > 4096) throw std::invalid_argument("too many outline fragments");
    const auto n = source.fragments.size();
    std::vector<size_t> parents(n); std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](size_t i) { while (parents[i] != i) i = parents[i]; return i; };
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < i; ++j) {
            const auto& a = source.fragments[i]; const auto& b = source.fragments[j];
            if (a.x <= b.x + b.width && b.x <= a.x + a.width && a.y <= b.y + b.height && b.y <= a.y + a.height)
                parents[root(i)] = root(j);
        }
    std::vector<SkPathBuilder> builders(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& r = source.fragments[i];
        if (nonempty(r)) {
            const auto radii=source.radii.empty()?CornerRadii{}:source.radii.at(i);
            builders[root(i)].addRRect(sk_rounded({r,radii}));
        }
    }
    const float width = source.style.used_width();
    const auto add = [](SkPath& destination, const SkPath& value) {
        destination = destination.isEmpty() ? value : combine(destination, value, kUnion_SkPathOp);
    };
    for (auto& builder : builders) {
        const auto path = builder.detach();
        if (path.isEmpty()) continue;
        const auto base = Simplify(path);
        if (!base) throw std::runtime_error("outline fragment union failed");
        const auto rounded=source.fragments.size()==1&&!source.radii.empty()&&RoundedRect{source.fragments[0],source.radii[0]}.curved()
            ?std::optional<RoundedRect>{{source.fragments[0],source.radii[0]}}:std::nullopt;
        const Component component{*base, base->getBounds(), source.style.offset,rounded};
        const auto a = component.edge(0), b = component.edge(width);
        add(out.ring, combine(b, a, kDifference_SkPathOp));
        add(out.outer, b);
        add(out.middle, component.edge(width / 2));
        if (source.style.style == BorderStyle::double_line && width >= 3) {
            add(out.first, combine(component.edge(width / 3), a, kDifference_SkPathOp));
            add(out.last, combine(b, component.edge(width * 2 / 3), kDifference_SkPathOp));
        }
        if (source.style.style == BorderStyle::groove || source.style.style == BorderStyle::ridge) {
            add(out.half_inner, combine(component.edge(width / 2), a, kDifference_SkPathOp));
            add(out.half_outer, combine(b, component.edge(width / 2), kDifference_SkPathOp));
        }
    }
    const auto bounds = out.ring.getBounds();
    out.ink = source.clip.bounds({bounds.x(), bounds.y(), bounds.width(), bounds.height()});
    const auto style = source.style.automatic ? BorderStyle::solid : source.style.style;
    if (style == BorderStyle::double_line && width >= 3) out.first = combine(out.first, out.last, kUnion_SkPathOp);
    if (style == BorderStyle::dotted || style == BorderStyle::dashed) {
        const float intervals[]{style == BorderStyle::dotted ? 0.0f : width * 3, width * (style == BorderStyle::dotted ? 2 : 3)};
        out.dash = SkDashPathEffect::Make(intervals, 0);
    }
    const auto band = [&](const SkPath& path, const SkPath& boundary, bool top_light, float thickness) {
        out.shades.push_back({path, shade(source.style.color, top_light)});
        SkPathBuilder dark_edges;
        SkPath::Iter iterator(boundary, false); SkPoint points[4];
        for (;;) {
            const auto verb = iterator.next(points);
            if (verb == SkPath::kDone_Verb) break;
            if (verb != SkPath::kLine_Verb) continue;
            auto dx = points[1].x() - points[0].x(), dy = points[1].y() - points[0].y();
            const float length = std::hypot(dx, dy);
            if (!length) continue;
            const float mx = (points[0].x() + points[1].x()) / 2, my = (points[0].y() + points[1].y()) / 2;
            const float step = std::max(.01f, thickness * .01f);
            // PathOps can reverse contour winding. Determine the filled side
            // geometrically; the inner edge of a ring must not shade its opposite side.
            if (!boundary.contains(mx - dy / length * step, my + dx / length * step)) { dx = -dx; dy = -dy; }
            if (dx < 0 || dy > 0) dark_edges.moveTo(points[0]).lineTo(points[1]);
        }
        SkPaint stroke;
        stroke.setStyle(SkPaint::kStroke_Style); stroke.setStrokeWidth(thickness * 2);
        stroke.setStrokeCap(SkPaint::kSquare_Cap);
        const auto area = skpathutils::FillPathWithPaint(dark_edges.detach(), stroke);
        out.shades.push_back({combine(area, path, kIntersect_SkPathOp), shade(source.style.color, !top_light)});
    };
    if (style == BorderStyle::groove || style == BorderStyle::ridge) {
        band(out.half_outer, out.outer, style == BorderStyle::ridge, width / 2);
        band(out.half_inner, out.middle, style == BorderStyle::groove, width / 2);
    } else if (style == BorderStyle::inset || style == BorderStyle::outset)
        band(out.ring, out.outer, style == BorderStyle::outset, width);
}
OutlineShape::~OutlineShape() = default;
xl_node_bounds OutlineShape::ink_bounds() const { return impl_->ink; }
void OutlineShape::paint(SkCanvas* canvas) const {
    const auto& value = *impl_;
    if (value.ring.isEmpty() || !nonempty(value.ink)) return;
    SkAutoCanvasRestore restore(canvas, true);
    apply_clip(canvas, value.clip);
    SkPaint paint; paint.setAntiAlias(true); paint.setColor(value.style.color);
    const auto width = value.style.used_width();
    const auto style = value.style.automatic ? BorderStyle::solid : value.style.style;
    if (style == BorderStyle::dotted || style == BorderStyle::dashed) {
        canvas->clipPath(value.ring, true);
        paint.setStyle(SkPaint::kStroke_Style); paint.setStrokeWidth(width);
        if (style == BorderStyle::dotted) paint.setStrokeCap(SkPaint::kRound_Cap);
        paint.setPathEffect(value.dash);
        canvas->drawPath(value.middle, paint);
    } else if (style == BorderStyle::double_line && width >= 3) {
        canvas->drawPath(value.first, paint);
    } else if (style == BorderStyle::inset || style == BorderStyle::outset ||
               style == BorderStyle::groove || style == BorderStyle::ridge) {
        const auto bounds = value.ring.getBounds();
        canvas->saveLayerAlphaf(&bounds, float(value.style.color >> 24) / 255);
        for (const auto& layer : value.shades) {
            paint.setColor(layer.color); canvas->drawPath(layer.path, paint);
        }
        canvas->restore();
    } else canvas->drawPath(value.ring, paint);
}
}
