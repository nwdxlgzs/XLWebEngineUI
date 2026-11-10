#pragma once
#include <XLWebEngineUI/xl.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include <stdexcept>
#include "rounded_rect.h"

class SkCanvas;
namespace xl::detail {
// Axis-independent rectangular fast path plus immutable rounded intersections,
// in the same coordinate space as each paint/query item.
struct Clip {
    float left = -std::numeric_limits<float>::infinity();
    float top = -std::numeric_limits<float>::infinity();
    float right = std::numeric_limits<float>::infinity();
    float bottom = std::numeric_limits<float>::infinity();
    std::shared_ptr<const std::vector<RoundedRect>> curves = {};
    bool operator==(const Clip& other) const {
        return left==other.left&&top==other.top&&right==other.right&&bottom==other.bottom&&
            (curves==other.curves||(curves&&other.curves&&*curves==*other.curves));
    }
    Clip moved(float x, float y) const {
        Clip result{left+x,top+y,right+x,bottom+y,curves};
        if(curves&&(x||y)){auto next=std::make_shared<std::vector<RoundedRect>>(*curves);for(auto& curve:*next)curve=curve.moved(x,y);result.curves=std::move(next);}
        return result;
    }
    Clip intersect(const Clip& other) const {
        Clip result{std::max(left,other.left),std::max(top,other.top),std::min(right,other.right),std::min(bottom,other.bottom),curves?curves:other.curves};
        if(curves&&other.curves&&curves!=other.curves) {
            if(curves->size()+other.curves->size()>256)throw std::length_error("too many rounded clips");
            auto merged=std::make_shared<std::vector<RoundedRect>>(*curves);merged->insert(merged->end(),other.curves->begin(),other.curves->end());result.curves=std::move(merged);
        }
        return result;
    }
    Clip rounded(const RoundedRect& rect) const {
        Clip next{rect.rect.x,rect.rect.y,rect.rect.x+rect.rect.width,rect.rect.y+rect.rect.height,{}};
        if(rect.curved())next.curves=std::make_shared<const std::vector<RoundedRect>>(std::vector<RoundedRect>{rect});
        return intersect(next);
    }
    xl_node_bounds bounds(xl_node_bounds rect) const {
        const auto x = std::max(left, rect.x), y = std::max(top, rect.y);
        const auto r = std::min(right, rect.x + rect.width), b = std::min(bottom, rect.y + rect.height);
        xl_node_bounds result{x,y,std::max(0.0f,r-x),std::max(0.0f,b-y)};
        if(curves)for(const auto& curve:*curves)result=curve.bounds(result);
        return result;
    }
    bool contains(float x, float y) const {
        return x>=left&&x<right&&y>=top&&y<bottom&&(!curves||std::all_of(curves->begin(),curves->end(),[&](const auto& curve){return curve.contains(x,y);}));
    }
};
inline bool nonempty(const xl_node_bounds& rect) { return rect.width > 0 && rect.height > 0; }
void apply_clip(SkCanvas* canvas, const Clip& clip);
} // namespace xl::detail
