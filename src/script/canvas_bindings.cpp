#include "canvas_bindings.h"
#include "document/dom_internal.h"
#include "graphics/canvas_content.h"
#include "include/core/SkFontMgr.h"
#include "style/color.h"
#include "style/font_shorthand.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace xl::detail {
namespace {
struct Value {
  JSContext *context;
  JSValue value;
  ~Value() { JS_FreeValue(context, value); }
};
std::string string(JSContext *context, JSValueConst value) {
  size_t count;
  const char *text = JS_ToCStringLen(context, &count, value);
  if (!text)
    throw DocumentFailure{XL_SCRIPT_ERROR};
  std::string result;
  try {
    result.assign(text, count);
  } catch (...) {
    JS_FreeCString(context, text);
    throw;
  }
  JS_FreeCString(context, text);
  return result;
}
std::vector<double> numbers(JSContext *context, JSValueConst value,
                            size_t limit = 2048) {
  Value size{context, JS_GetPropertyStr(context, value, "length")};
  uint32_t count;
  if (JS_IsException(size.value) ||
      JS_ToUint32(context, &count, size.value) < 0)
    throw DocumentFailure{XL_SCRIPT_ERROR};
  if (count > limit)
    throw DocumentFailure{XL_INVALID_ARGUMENT};
  std::vector<double> result;
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Value entry{context, JS_GetPropertyUint32(context, value, i)};
    double number;
    if (JS_IsException(entry.value) ||
        JS_ToFloat64(context, &number, entry.value) < 0)
      throw DocumentFailure{XL_SCRIPT_ERROR};
    result.push_back(number);
  }
  return result;
}
} // namespace
JSValue canvas_native(JSContext *context, JSValueConst, int argc,
                      JSValueConst *argv) {
  try {
    if (argc < 2)
      throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto &doc = *static_cast<Document::Impl *>(JS_GetContextOpaque(context));
    uint64_t id;
    if (JS_ToBigUint64(context, &id, argv[0]) < 0)
      return JS_EXCEPTION;
    const auto op = string(context, argv[1]);
    const auto arg = [&](int i) -> JSValueConst {
      if (i >= argc)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      return argv[i];
    };
    if (op == "width" || op == "height") {
      const auto [w, h] = doc.canvas_size(id);
      return JS_NewUint32(context, op == "width" ? w : h);
    }
    auto &canvas = doc.canvas(id);
    if (op == "generation")
      return JS_NewFloat64(context, double(canvas.generation()));
    if (op == "color") {
      auto source = string(context, arg(2));
      auto parsed = csscolor::parse(source);
      if (!parsed)
        return JS_NULL;
      csscolor::Context colors;
      std::transform(source.begin(), source.end(), source.begin(),
                     [](unsigned char c) {
                       return char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
                     });
      if (source.find("currentcolor") != source.npos)
        try {
          if (auto inherited = csscolor::parse(doc.style(id, "color")))
            colors.current = csscolor::resolve(inherited);
        } catch (const DocumentFailure &e) {
          if (e.status != XL_NOT_FOUND)
            throw;
        }
      return JS_NewUint32(context,
                          csscolor::argb(csscolor::resolve(parsed, colors)));
    }
    if (op == "font")
      return JS_NewBool(context, bool(canvas_font(string(context, arg(2)))));
    if (op == "style") {
      canvas.style(string(context, arg(2)), string(context, arg(3)));
      return JS_UNDEFINED;
    }
    if (op == "matrix") {
      const auto m = canvas.matrix();
      Value array{context, JS_NewArray(context)};
      for (uint32_t i = 0; i < 6; ++i)
        if (JS_SetPropertyUint32(context, array.value, i,
                                 JS_NewFloat64(context, m[i])) < 0)
          return JS_EXCEPTION;
      return JS_DupValue(context, array.value);
    }
    if (op == "measureText") {
      doc.ensure_canvas_text();
      const auto m = canvas.measure(string(context, arg(2)),
                                    doc.text_engine.get(), doc.text_session);
      Value result{context, JS_NewObject(context)};
      const std::pair<const char *, float> values[]{
          {"width", m.width},
          {"actualBoundingBoxLeft", m.left},
          {"actualBoundingBoxRight", m.right},
          {"actualBoundingBoxAscent", m.ascent},
          {"actualBoundingBoxDescent", m.descent},
          {"fontBoundingBoxAscent", m.font_ascent},
          {"fontBoundingBoxDescent", m.font_descent},
          {"emHeightAscent", m.font_ascent},
          {"emHeightDescent", m.font_descent}};
      for (const auto &[name, value] : values)
        if (JS_SetPropertyStr(context, result.value, name,
                              JS_NewFloat64(context, value)) < 0)
          return JS_EXCEPTION;
      return JS_DupValue(context, result.value);
    }
    if (op == "getPixels") {
      const auto a = numbers(context, arg(2), 4);
      if (a.size() != 4)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      for (auto v : a)
        if (!std::isfinite(v) || std::abs(v) > INT32_MAX)
          throw DocumentFailure{XL_INVALID_ARGUMENT};
      int64_t x = int64_t(a[0]), y = int64_t(a[1]), w = int64_t(a[2]),
              h = int64_t(a[3]);
      if (!w || !h)
        return JS_ThrowRangeError(context, "IndexSizeError");
      if (w < 0) {
        x += w;
        w = -w;
      }
      if (h < 0) {
        y += h;
        h = -h;
      }
      if (canvas.needs_text())
        doc.ensure_canvas_text();
      doc.render_canvases();
      const auto pixels =
          canvas.pixels(int(std::clamp<int64_t>(x, INT32_MIN, INT32_MAX)),
                        int(std::clamp<int64_t>(y, INT32_MIN, INT32_MAX)),
                        uint32_t(w), uint32_t(h));
      return JS_NewArrayBufferCopy(context, pixels.data(), pixels.size());
    }
    if (op == "dataURL") {
      if (canvas.needs_text())
        doc.ensure_canvas_text();
      doc.render_canvases();
      const auto encoded = canvas.data_url();
      return JS_NewStringLen(context, encoded.data(), encoded.size());
    }
    if (op == "reset") {
      canvas.reset(canvas.width(), canvas.height());
      doc.canvas_changed();
      return JS_UNDEFINED;
    }
    if (op == "putPixels") {
      const auto a = numbers(context, arg(3), 8);
      if (a.size() != 8)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      for (auto v : a)
        if (!std::isfinite(v) || std::abs(v) > INT32_MAX)
          throw DocumentFailure{XL_INVALID_ARGUMENT};
      if (a[0] <= 0 || a[1] <= 0)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      const auto w = uint32_t(a[0]), h = uint32_t(a[1]);
      size_t offset = 0, length = 0, bytes_per_element = 0;
      Value buffer{context,
                   JS_GetTypedArrayBuffer(context, arg(2), &offset, &length,
                                          &bytes_per_element)};
      if (JS_IsException(buffer.value))
        return JS_EXCEPTION;
      size_t capacity = 0;
      auto *bytes = JS_GetArrayBuffer(context, &capacity, buffer.value);
      if (!bytes || bytes_per_element != 1 || length != uint64_t(w) * h * 4 ||
          offset > capacity || length > capacity - offset)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      double x = a[4], y = a[5], dw = a[6], dh = a[7];
      if (dw < 0) {
        x += dw;
        dw = -dw;
      }
      if (dh < 0) {
        y += dh;
        dh = -dh;
      }
      canvas.put_pixels({bytes + offset, length}, w, h, int(a[2]), int(a[3]),
                        {float(x), float(y), float(dw), float(dh)});
      doc.canvas_changed();
      return JS_UNDEFINED;
    }
    if (op == "drawImage") {
      uint64_t source_id;
      if (JS_ToBigUint64(context, &source_id, arg(2)) < 0)
        return JS_EXCEPTION;
      auto *node = doc.resolve(source_id);
      std::shared_ptr<const ImageDrawing> drawing;
      float width = 0, height = 0;
      if (node->local_name == LXB_TAG_CANVAS) {
        auto &source = doc.canvas(source_id);
        if (source.needs_text())
          doc.ensure_canvas_text();
        doc.render_canvases();
        width = float(source.width());
        height = float(source.height());
        drawing = source.drawing();
      } else if (doc.image_node(node)) {
        const auto content = doc.image_content(node);
        if (!content)
          return JS_ThrowTypeError(context,
                                   "InvalidStateError: image unavailable");
        const auto &size = content->metrics();
        width = size.width.value_or(300);
        height = size.height.value_or(size.ratio ? width / *size.ratio : 150);
        if (size.text)
          doc.ensure_canvas_text();
        drawing = content->drawing(
            width, height,
            doc.text_engine ? doc.text_engine->svg_fonts() : sk_sp<SkFontMgr>{},
            doc.text_session,
            doc.text_engine ? doc.text_engine->font_revision() : 0);
      } else
        return JS_ThrowTypeError(context, "Canvas or image expected");
      const auto a = numbers(context, arg(3), 8);
      for (auto v : a)
        if (!std::isfinite(v) || std::abs(v) > 1e12)
          return JS_UNDEFINED;
      xl_node_bounds src{0, 0, width, height}, dst;
      if (a.size() == 2)
        dst = {float(a[0]), float(a[1]), width, height};
      else if (a.size() == 4)
        dst = {float(a[0]), float(a[1]), float(a[2]), float(a[3])};
      else if (a.size() == 8) {
        src = {float(a[0]), float(a[1]), float(a[2]), float(a[3])};
        dst = {float(a[4]), float(a[5]), float(a[6]), float(a[7])};
      } else
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      for (auto *b : {&src, &dst}) {
        if (b->width < 0) {
          b->x += b->width;
          b->width = -b->width;
        }
        if (b->height < 0) {
          b->y += b->height;
          b->height = -b->height;
        }
      }
      // Clip the source first and proportionately trim the destination.
      if (src.width > 0 && src.height > 0) {
        const auto x = std::max(0.f, src.x), y = std::max(0.f, src.y),
                   r = std::min(width, src.x + src.width),
                   b = std::min(height, src.y + src.height);
        const auto sx = dst.width / src.width, sy = dst.height / src.height;
        dst.x += (x - src.x) * sx;
        dst.y += (y - src.y) * sy;
        dst.width = (r - x) * sx;
        dst.height = (b - y) * sy;
        src = {x, y, r - x, b - y};
        canvas.image(drawing, src, dst);
        doc.canvas_changed();
      }
      return JS_UNDEFINED;
    }
    if (op == "gradient") {
      CanvasGradient gradient;
      uint32_t kind;
      if (JS_ToUint32(context, &kind, arg(3)) < 0)
        return JS_EXCEPTION;
      if (kind > 2)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      gradient.kind = CanvasGradient::Kind(kind);
      const auto p = numbers(context, arg(4), 6),
                 m = numbers(context, arg(5), 6),
                 stops = numbers(context, arg(6), 8192);
      if (p.size() > 6 || m.size() != 6 || stops.size() % 2)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      for (size_t i = 0; i < p.size(); ++i) {
        if (!std::isfinite(p[i]))
          throw DocumentFailure{XL_INVALID_ARGUMENT};
        gradient.points[i] = float(p[i]);
      }
      for (size_t i = 0; i < 6; ++i) {
        if (!std::isfinite(m[i]))
          throw DocumentFailure{XL_INVALID_ARGUMENT};
        gradient.matrix[i] = float(m[i]);
      }
      for (size_t i = 0; i < stops.size(); i += 2) {
        if (!std::isfinite(stops[i]) || stops[i] < 0 || stops[i] > 1 ||
            !std::isfinite(stops[i + 1]) || stops[i + 1] < 0 ||
            stops[i + 1] > UINT32_MAX)
          throw DocumentFailure{XL_INVALID_ARGUMENT};
        gradient.stops.emplace_back(float(stops[i]), uint32_t(stops[i + 1]));
      }
      canvas.gradient(JS_ToBool(context, arg(2)) != 0, gradient);
      return JS_UNDEFINED;
    }
    if (op == "fillText" || op == "strokeText") {
      const auto args = numbers(context, arg(3), 3);
      if (args.size() != 3)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
      canvas.text(string(context, arg(2)), float(args[0]), float(args[1]),
                  float(args[2]), op == "strokeText");
      doc.canvas_changed();
      return JS_UNDEFINED;
    }
    const auto a = argc > 2 ? numbers(context, argv[2]) : std::vector<double>{};
    constexpr std::pair<std::string_view, size_t> lengths[]{
        {"moveTo", 2},        {"lineTo", 2},    {"quadraticCurveTo", 4},
        {"bezierCurveTo", 6}, {"rect", 4},      {"fillRect", 4},
        {"strokeRect", 4},    {"clearRect", 4}, {"arc", 6},
        {"arcTo", 5},         {"ellipse", 8},   {"clip", 1},
        {"fill", 1},          {"stroke", 0},    {"save", 0},
        {"restore", 0},       {"beginPath", 0}, {"closePath", 0},
        {"setTransform", 6},  {"transform", 6}, {"resetTransform", 0},
        {"globalAlpha", 1},   {"lineWidth", 1}, {"miterLimit", 1},
        {"lineDashOffset", 1}};
    bool known = op == "setLineDash";
    for (const auto &[name, length] : lengths)
      if (op == name) {
        known = true;
        if (a.size() != length)
          throw DocumentFailure{XL_INVALID_ARGUMENT};
        break;
      }
    if (!known)
      throw DocumentFailure{XL_UNSUPPORTED};
    canvas.command(op, a);
    if (op == "fillRect" || op == "strokeRect" || op == "clearRect" ||
        op == "fill" || op == "stroke")
      doc.canvas_changed();
    return JS_UNDEFINED;
  } catch (const DocumentFailure &e) {
    if (e.status == XL_SCRIPT_ERROR)
      return JS_EXCEPTION;
    return JS_ThrowTypeError(context, "Canvas operation failed: %s",
                             xl_status_string(e.status));
  } catch (const std::bad_alloc &) {
    return JS_ThrowOutOfMemory(context);
  } catch (...) {
    return JS_ThrowInternalError(context, "Canvas operation failed");
  }
}
const char canvas_bootstrap[] =
    R"JS(globalThis.__xlCanvasBootstrap=function(n,ids,Node){
 'use strict';
 const contexts=new WeakMap(),contextData=new WeakMap(),gradientData=new WeakMap(),imageData=new WeakMap(),secret={};
 const defaults=()=>({fillStyle:'#000000',strokeStyle:'#000000',globalAlpha:1,globalCompositeOperation:'source-over',lineWidth:1,lineCap:'butt',lineJoin:'miter',miterLimit:10,lineDashOffset:0,font:'10px sans-serif',textAlign:'start',textBaseline:'alphabetic',direction:'inherit'});
 const call=(s,op,...a)=>n.canvas(s.id,op,...a);
 const state=context=>{const s=contextData.get(context);if(!s)throw new TypeError('Canvas context expected');const generation=call(s,'generation');if(s.generation!==generation){s.generation=generation;s.props=defaults();s.stack=[];s.dash=[];}return s;};
 const color=(s,text)=>call(s,'color',String(text));
 const colorText=value=>{const a=value>>>24,r=(value>>>16)&255,g=(value>>>8)&255,b=value&255;return a===255?'#'+[r,g,b].map(v=>v.toString(16).padStart(2,'0')).join(''):`rgba(${r}, ${g}, ${b}, ${Number((a/255).toFixed(3))})`;};
 class CanvasGradient {
  constructor(key,s,kind,points){if(key!==secret)throw new TypeError('Illegal constructor');gradientData.set(this,{s,kind,points,stops:[]});}
  addColorStop(offset,text){const data=gradientData.get(this);if(!data)throw new TypeError('CanvasGradient expected');offset=Number(offset);if(!Number.isFinite(offset)||offset<0||offset>1)throw new RangeError('IndexSizeError');const value=color(data.s,text);if(value===null)throw new SyntaxError('Invalid color stop');if(data.stops.length>=8192)throw new RangeError('Canvas gradient stop budget');data.stops.push(offset,value);}
 }
 const syncStyle=(s,stroke)=>{const value=s.props[stroke?'strokeStyle':'fillStyle'],gradient=gradientData.get(value);if(gradient)call(s,'gradient',stroke,gradient.kind,gradient.points,call(s,'matrix'),gradient.stops);else call(s,'style',stroke?'strokeStyle':'fillStyle',value);};
 class CanvasRenderingContext2D {
  constructor(key,canvas){if(key!==secret)throw new TypeError('Illegal constructor');contextData.set(this,{id:ids.get(canvas),canvas,generation:-1,props:defaults(),stack:[],dash:[]});}
  get canvas(){return state(this).canvas;}
  reset(){call(state(this),'reset');state(this);}
  save(){const s=state(this);call(s,'save',[]);s.stack.push({props:{...s.props},dash:s.dash.slice()});}
  restore(){const s=state(this);call(s,'restore',[]);if(s.stack.length){const old=s.stack.pop();s.props=old.props;s.dash=old.dash;}}
  resetTransform(){call(state(this),'resetTransform',[]);}
  transform(a,b,c,d,e,f){call(state(this),'transform',[a,b,c,d,e,f].map(Number));}
  setTransform(...a){if(!a.length)a=[1,0,0,1,0,0];else if(a.length===1){const m=a[0];a=[m.a??m.m11??1,m.b??m.m12??0,m.c??m.m21??0,m.d??m.m22??1,m.e??m.m41??0,m.f??m.m42??0];}if(a.length<6)throw new TypeError('Six transform values required');call(state(this),'setTransform',a.slice(0,6).map(Number));}
  getTransform(){const m=call(state(this),'matrix');return {a:m[0],b:m[1],c:m[2],d:m[3],e:m[4],f:m[5],m11:m[0],m12:m[1],m21:m[2],m22:m[3],m41:m[4],m42:m[5],is2D:true};}
  translate(x,y){this.transform(1,0,0,1,x,y);}
  scale(x,y){this.transform(x,0,0,y,0,0);}
  rotate(angle){angle=Number(angle);this.transform(Math.cos(angle),Math.sin(angle),-Math.sin(angle),Math.cos(angle),0,0);}
  setLineDash(values){const a=Array.from(values,Number);if(a.some(v=>!Number.isFinite(v)||v<0))return;const s=state(this);call(s,'setLineDash',a);s.dash=a.length%2?a.concat(a):a;}
  getLineDash(){return state(this).dash.slice();}
  fill(rule='nonzero'){if(rule!=='nonzero'&&rule!=='evenodd')throw new TypeError('Invalid fill rule');const s=state(this);syncStyle(s,false);call(s,'fill',[rule==='evenodd'?1:0]);}
  stroke(){const s=state(this);syncStyle(s,true);call(s,'stroke',[]);}
  clip(rule='nonzero'){if(rule!=='nonzero'&&rule!=='evenodd')throw new TypeError('Invalid fill rule');call(state(this),'clip',[rule==='evenodd'?1:0]);}
  fillText(text,x,y,maxWidth=Infinity){const s=state(this);syncStyle(s,false);call(s,'fillText',String(text),[Number(x),Number(y),Number(maxWidth)]);}
  strokeText(text,x,y,maxWidth=Infinity){const s=state(this);syncStyle(s,true);call(s,'strokeText',String(text),[Number(x),Number(y),Number(maxWidth)]);}
  measureText(text){return call(state(this),'measureText',String(text));}
  createImageData(width,height){if(imageData.has(width))return new ImageData(width.width,width.height);return new ImageData(width,height);}
  getImageData(x,y,width,height){width=Math.trunc(Number(width));height=Math.trunc(Number(height));const data=new Uint8ClampedArray(call(state(this),'getPixels',[x,y,width,height].map(Number)));return new ImageData(data,Math.abs(width),Math.abs(height));}
  putImageData(data,x,y,...dirty){const s=state(this);if(!imageData.has(data))throw new TypeError('ImageData expected');if(dirty.length&&dirty.length<4)throw new TypeError('Four dirty rectangle values required');const r=dirty.length?dirty.slice(0,4).map(Number):[0,0,data.width,data.height];call(s,'putPixels',data.data,[data.width,data.height,Number(x),Number(y),...r]);}
  drawImage(source,...args){if(!ids.has(source))throw new TypeError('Image source expected');call(state(this),'drawImage',ids.get(source),args.map(Number));}
  createLinearGradient(x0,y0,x1,y1){const s=state(this),p=[x0,y0,x1,y1].map(Number);if(p.some(v=>!Number.isFinite(v)))throw new TypeError('Non-finite gradient');return new CanvasGradient(secret,s,0,p);}
  createRadialGradient(x0,y0,r0,x1,y1,r1){const s=state(this),p=[x0,y0,r0,x1,y1,r1].map(Number);if(p.some(v=>!Number.isFinite(v)))throw new TypeError('Non-finite gradient');if(p[2]<0||p[5]<0)throw new RangeError('IndexSizeError');return new CanvasGradient(secret,s,1,p);}
  createConicGradient(start,x,y){const s=state(this),p=[start,x,y].map(Number);if(p.some(v=>!Number.isFinite(v)))throw new TypeError('Non-finite gradient');return new CanvasGradient(secret,s,2,p);}
 }
 for(const [method,count] of Object.entries({beginPath:0,closePath:0,moveTo:2,lineTo:2,quadraticCurveTo:4,bezierCurveTo:6,rect:4,arcTo:5,fillRect:4,strokeRect:4,clearRect:4}))Object.defineProperty(CanvasRenderingContext2D.prototype,method,{value:function(...args){if(args.length<count)throw new TypeError('Insufficient canvas arguments');const s=state(this);if(method==='fillRect'||method==='strokeRect')syncStyle(s,method==='strokeRect');return call(s,method,args.slice(0,count).map(Number));},configurable:true,writable:true});
 CanvasRenderingContext2D.prototype.arc=function(x,y,r,start,end,ccw=false){r=Number(r);if(r<0)throw new RangeError('IndexSizeError');return call(state(this),'arc',[Number(x),Number(y),r,Number(start),Number(end),ccw?1:0]);};
 CanvasRenderingContext2D.prototype.ellipse=function(x,y,rx,ry,rotation,start,end,ccw=false){rx=Number(rx);ry=Number(ry);if(rx<0||ry<0)throw new RangeError('IndexSizeError');return call(state(this),'ellipse',[Number(x),Number(y),rx,ry,Number(rotation),Number(start),Number(end),ccw?1:0]);};
 const enums={lineCap:['butt','round','square'],lineJoin:['round','bevel','miter'],textAlign:['start','end','left','right','center'],textBaseline:['top','hanging','middle','alphabetic','ideographic','bottom'],direction:['inherit','ltr','rtl'],globalCompositeOperation:['source-over','source-in','source-out','source-atop','destination-over','destination-in','destination-out','destination-atop','lighter','copy','xor','multiply','screen','overlay','darken','lighten','color-dodge','color-burn','hard-light','soft-light','difference','exclusion','hue','saturation','color','luminosity']};
 for(const name of Object.keys(defaults()))Object.defineProperty(CanvasRenderingContext2D.prototype,name,{get(){return state(this).props[name];},set(value){const s=state(this);if(name==='fillStyle'||name==='strokeStyle'){if(gradientData.has(value)){s.props[name]=value;return;}const parsed=color(s,value);if(parsed===null)return;value=colorText(parsed);}
 else if(['globalAlpha','lineWidth','miterLimit','lineDashOffset'].includes(name)){value=Number(value);if(!Number.isFinite(value)||(name==='globalAlpha'&&(value<0||value>1))||((name==='lineWidth'||name==='miterLimit')&&value<=0))return;call(s,name,[value]);}
 else {value=String(value);if(enums[name]&&!enums[name].includes(value))return;if(name==='font'&&!call(s,'font',value))return;call(s,'style',name,value);}s.props[name]=value;},configurable:true});
 class HTMLCanvasElement extends Node {
  get width(){return n.canvas(ids.get(this),'width');}set width(value){this.setAttribute('width',String(Number(value)>>>0));}
  get height(){return n.canvas(ids.get(this),'height');}set height(value){this.setAttribute('height',String(Number(value)>>>0));}
  getContext(type,options={}){if(String(type)!=='2d')return null;if(!contexts.has(this))contexts.set(this,new CanvasRenderingContext2D(secret,this));const result=contexts.get(this);state(result);return result;}
  toDataURL(type='image/png',quality){return n.canvas(ids.get(this),'dataURL');}
 }
 class ImageData {
  constructor(data,width,height){let bytes;
   if(data instanceof Uint8ClampedArray){bytes=data;width=Number(width)>>>0;if(!width||!bytes.length||bytes.length%4||bytes.length/4%width)throw new RangeError('IndexSizeError');const inferred=bytes.length/4/width;if(typeof height==='number'&&(Number(height)>>>0)!==inferred)throw new RangeError('IndexSizeError');height=inferred;}
   else {height=Number(width)>>>0;width=Number(data)>>>0;if(!width||!height||width*height>16777216)throw new RangeError('IndexSizeError');bytes=new Uint8ClampedArray(width*height*4);}
   if(width*height>16777216)throw new RangeError('ImageData budget');imageData.set(this,{bytes,width,height});
  }
  get width(){return imageData.get(this).width;}get height(){return imageData.get(this).height;}get data(){return imageData.get(this).bytes;}get colorSpace(){return 'srgb';}
 }
 Object.assign(globalThis,{HTMLCanvasElement,CanvasRenderingContext2D,CanvasGradient,ImageData});
 return HTMLCanvasElement;
};)JS";
} // namespace xl::detail
