#include "box_paint.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/effects/SkDashPathEffect.h"
#include "sk_rounded.h"
#include "image_content.h"
#include <algorithm>
#include <cmath>

namespace xl::detail {
void apply_clip(SkCanvas* canvas, const Clip& clip) {
    if (clip == Clip{}) return;
    SkRect current;
    if (!canvas->getLocalClipBounds(&current)) { canvas->clipRect(SkRect::MakeEmpty()); return; }
    // Tightening a damage tile to a curve's mathematical bounds removes its
    // antialias coverage. Apply only the rectangular axes here; Skia evaluates
    // each curve against the original tile and retains its edge samples.
    auto axes = clip;
    axes.curves.reset();
    const auto bounds = axes.bounds({current.x(), current.y(), current.width(), current.height()});
    canvas->clipRect(SkRect::MakeXYWH(bounds.x, bounds.y, bounds.width, bounds.height));
    if(clip.curves)for(const auto& curve:*clip.curves)canvas->clipRRect(sk_rounded(curve),true);
}
namespace {
SkPath ring(const RoundedRect& outer,const RoundedRect& inner) {
    SkPathBuilder builder;builder.setFillType(SkPathFillType::kEvenOdd);builder.addRRect(sk_rounded(outer));
    if(inner.rect.width>0&&inner.rect.height>0)builder.addRRect(sk_rounded(inner));return builder.detach();
}
SkPath side_region(const RoundedRect& rect,const std::array<BorderSide,4>& borders,size_t side) {
    const auto& r=rect.rect;
    const std::array<SkPoint,4> outer{{{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}}};
    const std::array<SkPoint,4> inner{{{r.x+borders[3].width,r.y+borders[0].width},{r.x+r.width-borders[1].width,r.y+borders[0].width},
        {r.x+r.width-borders[1].width,r.y+r.height-borders[2].width},{r.x+borders[3].width,r.y+r.height-borders[2].width}}};
    std::vector<SkPoint> polygon(outer.begin(),outer.end());
    const auto next=(side+1)%4;const auto test=SkPoint{(outer[side].x()+outer[next].x())*.5f,(outer[side].y()+outer[next].y())*.5f};
    for(const auto corner:{side,next}) {
        const double dx=inner[corner].x()-outer[corner].x(),dy=inner[corner].y()-outer[corner].y();if(!dx&&!dy)continue;
        const auto cross=[&](SkPoint p){return dx*(p.y()-outer[corner].y())-dy*(p.x()-outer[corner].x());};
        const auto sign=cross(test)>=0?1.:-1.;std::vector<SkPoint> clipped;
        if(polygon.empty())break;
        auto previous=polygon.back();auto a=sign*cross(previous);
        for(const auto point:polygon) {
            const auto b=sign*cross(point);
            if((a>=0)!=(b>=0)) {const auto factor=a/(a-b);clipped.push_back({float(previous.x()+(point.x()-previous.x())*factor),float(previous.y()+(point.y()-previous.y())*factor)});}
            if(b>=0)clipped.push_back(point);previous=point;a=b;
        }
        polygon=std::move(clipped);
    }
    return SkPath::Polygon(polygon,true);
}
void curved_border(SkCanvas* canvas,const RoundedRect& outer,const std::array<BorderSide,4>& sides) {
    const auto edge=[&](float fraction){return outer.inset(sides[0].width*fraction,sides[1].width*fraction,sides[2].width*fraction,sides[3].width*fraction);};
    const auto inner=edge(1);SkPaint paint;paint.setAntiAlias(true);
    for(size_t side=0;side<4;++side) {
        const auto& border=sides[side];if(border.width<=0||border.style==BorderStyle::none||border.style==BorderStyle::hidden||!(border.color>>24))continue;
        SkAutoCanvasRestore restore(canvas,true);canvas->clipPath(side_region(outer,sides,side),false);
        const auto shade=[&](bool light){uint32_t result=border.color&0xff000000;for(unsigned shift:{0u,8u,16u}){const auto value=(border.color>>shift)&255;result|=(light?value+(255-value)/3:value*2/3)<<shift;}return result;};
        const auto band=[&](float first,float last,uint32_t color){paint.setColor(color);canvas->drawPath(ring(edge(first),edge(last)),paint);};
        if(border.style==BorderStyle::double_line&&border.width>=3){band(0,1/3.f,border.color);band(2/3.f,1,border.color);}
        else if(border.style==BorderStyle::dotted||border.style==BorderStyle::dashed) {
            canvas->clipPath(ring(outer,inner),true);paint.setStyle(SkPaint::kStroke_Style);paint.setStrokeWidth(border.width);paint.setColor(border.color);
            const bool dotted=border.style==BorderStyle::dotted;paint.setStrokeCap(dotted?SkPaint::kRound_Cap:SkPaint::kButt_Cap);
            const float intervals[]{dotted?0:3*border.width,(dotted?2:3)*border.width};paint.setPathEffect(SkDashPathEffect::Make(intervals,0));
            canvas->drawRRect(sk_rounded(edge(.5f)),paint);paint.setPathEffect(nullptr);paint.setStyle(SkPaint::kFill_Style);
        } else if(border.style==BorderStyle::groove||border.style==BorderStyle::ridge) {
            const auto light=(side==0||side==3)==(border.style==BorderStyle::ridge);band(0,.5f,shade(light));band(.5f,1,shade(!light));
        } else if(border.style==BorderStyle::inset||border.style==BorderStyle::outset)band(0,1,shade((side==0||side==3)==(border.style==BorderStyle::outset)));
        else band(0,1,border.color);
    }
}
}
void paint_box(SkCanvas* canvas, const BoxPaint& box) {
    const auto& r = box.rect;
    if (r.width <= 0 || r.height <= 0) return;
    SkAutoCanvasRestore restore(canvas, box.clip != Clip{});
    apply_clip(canvas, box.clip);
    SkPaint paint;
    paint.setAntiAlias(true);
    if (box.disclosure) {
        const auto left = r.x + r.width * .15f, right = r.x + r.width * .65f;
        const auto top = r.y + r.height * .25f, bottom = r.y + r.height * .75f;
        const SkPoint points[]{
            {box.disclosure == 3 ? right : left, top},
            {box.disclosure == 2 ? right : box.disclosure == 3 ? left : right, box.disclosure == 2 ? top : (top + bottom) * .5f},
            {box.disclosure == 2 ? (left + right) * .5f : box.disclosure == 3 ? right : left, bottom}};
        paint.setColor(r.argb);
        canvas->drawPath(SkPath::Polygon(points, true), paint);
        return;
    }
    paint_background(canvas,box);
    if(box.image) {
        SkAutoCanvasRestore image_clip(canvas,true);
        const auto inner=RoundedRect{{r.x,r.y,r.width,r.height},box.radii}.inset(
            box.borders[0].width,box.borders[1].width,box.borders[2].width,box.borders[3].width);
        if(inner.curved())canvas->clipRRect(sk_rounded(inner),true);
        else canvas->clipRect(SkRect::MakeXYWH(inner.rect.x,inner.rect.y,inner.rect.width,inner.rect.height));
        box.image->paint(canvas,{r.x+box.image_rect.x,r.y+box.image_rect.y,box.image_rect.width,box.image_rect.height});
    }
    if(std::any_of(box.radii.begin(),box.radii.end(),[](const auto& value){return value.x>0&&value.y>0;})) {
        curved_border(canvas,{{r.x,r.y,r.width,r.height},box.radii},box.borders);return;
    }
    const auto& b = box.borders;
    const auto top = std::min(b[0].width, r.height), bottom = std::min(b[2].width, r.height);
    const auto left = std::min(b[3].width, r.width), right = std::min(b[1].width, r.width);
    const std::array<SkPoint, 4> outer{
        {{r.x, r.y}, {r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}}};
    const std::array<SkPoint, 4> inner{{{r.x + left, r.y + top},
                                        {r.x + r.width - right, r.y + top},
                                        {r.x + r.width - right, r.y + r.height - bottom},
                                        {r.x + left, r.y + r.height - bottom}}};
    for (size_t side = 0; side < 4; ++side) {
        const auto& border = b[side];
        if (border.width <= 0 || border.style == BorderStyle::none || border.style == BorderStyle::hidden ||
            !(border.color >> 24))
            continue;
        const auto next = (side + 1) % 4;
        const SkPoint polygon[]{outer[side], outer[next], inner[next], inner[side]};
        canvas->save();
        // Wedges meet on internal diagonals: antialias only the outer paint,
        // otherwise adjacent solid sides both lose coverage along the seam.
        canvas->clipPath(SkPath::Polygon(polygon, true), false);
        const bool horizontal = side % 2 == 0, reverse = side == 1 || side == 2;
        const auto strip = [&](float offset, float thickness, uint32_t color) {
            paint.setColor(color);
            canvas->drawRect(horizontal
                                 ? SkRect::MakeXYWH(r.x, reverse ? r.y + r.height - offset - thickness : r.y + offset,
                                                    r.width, thickness)
                                 : SkRect::MakeXYWH(reverse ? r.x + r.width - offset - thickness : r.x + offset, r.y,
                                                    thickness, r.height),
                             paint);
        };
        const auto shade = [&](bool light) {
            uint32_t color = border.color & 0xff000000;
            for (unsigned shift : {0u, 8u, 16u}) {
                const auto channel = (border.color >> shift) & 255;
                color |= (light ? channel + (255 - channel) / 3 : channel * 2 / 3) << shift;
            }
            return color;
        };
        if (border.style == BorderStyle::double_line && border.width >= 3) {
            strip(0, border.width / 3, border.color);
            strip(border.width * 2 / 3, border.width / 3, border.color);
        } else if ((border.style == BorderStyle::dotted || border.style == BorderStyle::dashed) &&
                   border.width >= .25f) {
            const auto length = horizontal ? r.width : r.height;
            const auto dash = double(border.width) * (border.style == BorderStyle::dotted ? 1 : 3);
            const auto count = std::max(1.0, std::floor((double(length) + dash) / (2 * double(dash))));
            const auto gap = count > 1 ? std::max(0.0, (length - count * dash) / (count - 1)) : 0;
            const auto clip = canvas->getLocalClipBounds();
            const auto origin = horizontal ? r.x : r.y;
            const auto begin = std::max(0.0, double(horizontal ? clip.left() : clip.top()) - origin - dash);
            const auto end =
                std::min(double(length), double(horizontal ? clip.right() : clip.bottom()) - origin + dash);
            const auto period = dash + gap;
            paint.setColor(border.color);
            const auto first = std::floor(begin / period) * period;
            const auto steps = size_t(std::clamp(std::ceil((end - first) / period), 0.0, 65536.0));
            for (size_t index = 0; index < steps; ++index) {
                const auto position = first + double(index) * period;
                const auto center = reverse ? (horizontal ? r.y + r.height : r.x + r.width) - border.width * .5f
                                            : (horizontal ? r.y : r.x) + border.width * .5f;
                if (border.style == BorderStyle::dotted)
                    canvas->drawCircle(horizontal ? r.x + position + dash * .5f : center,
                                       horizontal ? center : r.y + position + dash * .5f, border.width * .5f, paint);
                else
                    canvas->drawRect(
                        horizontal ? SkRect::MakeXYWH(r.x + position, center - border.width * .5f, dash, border.width)
                                   : SkRect::MakeXYWH(center - border.width * .5f, r.y + position, border.width, dash),
                        paint);
                if (count == 1) break;
            }
        } else if (border.style == BorderStyle::groove || border.style == BorderStyle::ridge) {
            const auto light = (side == 0 || side == 3) == (border.style == BorderStyle::ridge);
            strip(0, border.width * .5f, shade(light));
            strip(border.width * .5f, border.width * .5f, shade(!light));
        } else if (border.style == BorderStyle::inset || border.style == BorderStyle::outset)
            strip(0, border.width, shade((side == 0 || side == 3) == (border.style == BorderStyle::outset)));
        else
            strip(0, border.width, border.color);
        canvas->restore();
    }
}
} // namespace xl::detail
