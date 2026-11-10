#include "document/dom_internal.h"
#include "style/media_query.h"
#include "canvas_bindings.h"
#include <cstring>

namespace xl::detail {
namespace {
struct Value {
    JSContext* context;
    JSValue value;
    ~Value() { JS_FreeValue(context, value); }
};
std::string string(JSContext* context, JSValueConst value) {
    size_t length;
    const char* bytes = JS_ToCStringLen(context, &length, value);
    if (!bytes) throw DocumentFailure{XL_SCRIPT_ERROR};
    struct Cleanup { JSContext* context; const char* bytes; ~Cleanup() { JS_FreeCString(context, bytes); } } cleanup{context, bytes};
    return {bytes, length};
}
uint64_t node_id(JSContext* context, JSValueConst value) {
    uint64_t id = 0;
    if (JS_ToBigUint64(context, &id, value) < 0) throw DocumentFailure{XL_SCRIPT_ERROR};
    return id;
}
enum Operation { query, byid, set_attribute, attribute, set_text, text, create, append, remove,
                 parent, body, root, nodetype, tag, connected, focus, active, activate, style, uri, base_uri, resolve_uri, read_content, query_all,
                 blur, remove_attribute, checked, set_checked, value, set_value, edit_state, edit_selection,
                 schedule_task, cancel_task, time_now, children, fragment, set_inner_html, clone_node, environment, pseudo_style,
                 match_media,media_text,watch_media,unwatch_media,observe_resize,insert_before };
JSValue native(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int operation) {
    try {
        auto& doc = *static_cast<Document::Impl*>(JS_GetContextOpaque(context));
        auto arg = [&](int index) -> JSValueConst { if (index >= argc) throw DocumentFailure{XL_INVALID_ARGUMENT}; return argv[index]; };
        auto id = [&] { return node_id(context, arg(0)); };
        auto output = [&](std::string value) { return JS_NewStringLen(context, value.data(), value.size()); };
        switch (operation) {
        case observe_resize:doc.resize_observed=JS_ToBool(context,arg(0))!=0;if(!doc.resize_observed)doc.resize_pending=false;break;
        case match_media:return JS_NewBool(context,MediaQuery(string(context,arg(0))).matches(doc.environment));
        case media_text:return output(MediaQuery(string(context,arg(0))).serialized());
        case watch_media:return JS_NewUint32(context,doc.watch_media(std::make_shared<MediaQuery>(string(context,arg(0))),arg(1)));
        case unwatch_media:{uint32_t watch;if(JS_ToUint32(context,&watch,arg(0))<0)return JS_EXCEPTION;doc.unwatch_media(watch);break;}
        case pseudo_style: return output(doc.pseudo_style(id(), string(context, arg(1)), string(context, arg(2))));
        case environment: {
            const auto value = JS_NewObject(context);
            if (JS_IsException(value)) return JS_EXCEPTION;
            const auto& env = doc.environment;
            JS_SetPropertyStr(context, value, "width", JS_NewInt32(context, env.width));
            JS_SetPropertyStr(context, value, "height", JS_NewInt32(context, env.height));
            JS_SetPropertyStr(context, value, "screenWidth", JS_NewInt32(context, env.screen_width));
            JS_SetPropertyStr(context, value, "screenHeight", JS_NewInt32(context, env.screen_height));
            JS_SetPropertyStr(context, value, "colorDepth", JS_NewInt32(context, env.color_depth));
            JS_SetPropertyStr(context, value, "pixelRatio", JS_NewFloat64(context, env.pixel_ratio));
            JS_SetPropertyStr(context, value, "hidden", JS_NewBool(context, env.hidden));
            return value;
        }
        case children: {
            const auto array = JS_NewArray(context);
            if (JS_IsException(array)) return JS_EXCEPTION;
            Value cleanup{context, array};
            uint32_t index = 0;
            const bool elements = JS_ToBool(context, arg(1)) != 0;
            for (auto* child = doc.resolve(id())->first_child; child; child = child->next)
                if (!elements || child->type == LXB_DOM_NODE_TYPE_ELEMENT)
                    if (JS_SetPropertyUint32(context, array, index++, JS_NewBigUint64(context, doc.identify(child))) < 0) return JS_EXCEPTION;
            return JS_DupValue(context, array);
        }
        case fragment: {
            auto* node = lxb_dom_interface_node(lxb_dom_document_create_document_fragment(&doc.dom->dom_document));
            if (!node) throw std::bad_alloc();
            doc.detached.insert(node);
            return JS_NewBigUint64(context, doc.identify(node));
        }
        case clone_node: {
            auto* node = lxb_dom_node_clone(doc.resolve(id()), JS_ToBool(context, arg(1)) != 0);
            if (!node) throw std::bad_alloc();
            doc.detached.insert(node);
            doc.details_inserted(node, true);
            return JS_NewBigUint64(context, doc.identify(node));
        }
        case set_inner_html: {
            auto* node = doc.resolve(id()); element(node);
            const auto html = string(context, arg(1));
            if (html.size() > 16 * 1024 * 1024) throw DocumentFailure{XL_INVALID_ARGUMENT};
            ++doc.revision;
            while (node->first_child) {
                auto* child = node->first_child;
                doc.detached.insert(child); lxb_dom_node_remove(child);
            }
            if (!lxb_html_element_inner_html_set(lxb_html_interface_element(node), chars(html), html.size())) throw std::bad_alloc();
            doc.details_inserted(node, true, false);
            break;
        }
        case schedule_task: {
            double delay; uint32_t kind;
            if (JS_ToFloat64(context, &delay, arg(1)) < 0 || JS_ToUint32(context, &kind, arg(2)) < 0) return JS_EXCEPTION;
            return JS_NewUint32(context, doc.schedule(arg(0), delay, kind));
        }
        case cancel_task: {
            uint32_t timer;
            if (JS_ToUint32(context, &timer, arg(0)) < 0) return JS_EXCEPTION;
            doc.cancel_timer(timer); break;
        }
        case time_now: return JS_NewFloat64(context, doc.time_now());
        case query_all: {
            const auto matches=doc.query_all(string(context,arg(1)),id()?doc.resolve(id()):nullptr);
            Value array{context,JS_NewArray(context)};
            if(JS_IsException(array.value))return JS_EXCEPTION;
            for(uint32_t i=0;i<matches.size();++i)
                if(JS_SetPropertyUint32(context,array.value,i,JS_NewBigUint64(context,matches[i]))<0)return JS_EXCEPTION;
            return JS_DupValue(context,array.value);
        }
        case uri:return output(doc.uri);
        case base_uri:return output(doc.base_uri());
        case resolve_uri:return output(resolve_ui_uri(doc.base_uri(),string(context,arg(0))));
        case read_content: {
            if(!doc.mounts)throw UriFailure{XL_NOT_FOUND};
            const auto content=doc.mounts->open(resolve_ui_uri(doc.base_uri(),string(context,arg(0))));
            if(content->bytes.size()>16*1024*1024)throw UriFailure{XL_INVALID_ARGUMENT};
            return JS_NewStringLen(context,content->bytes.empty()?"":reinterpret_cast<const char*>(content->bytes.data()),content->bytes.size());
        }
        case query: return JS_NewBigUint64(context, doc.query(string(context, arg(1)), id() ? doc.resolve(id()) : nullptr));
        case byid: return JS_NewBigUint64(context, doc.by_id(string(context, arg(0))));
        case set_attribute: doc.set_attribute(id(), string(context, arg(1)), string(context, arg(2))); break;
        case attribute: {
            try { return output(doc.attribute(id(), string(context, arg(1)))); }
            catch (const DocumentFailure& e) { if (e.status == XL_NOT_FOUND) return JS_NULL; throw; }
        }
        case set_text: doc.set_text(id(), string(context, arg(1))); break;
        case text: return output(doc.text(id()));
        case create: return JS_NewBigUint64(context, doc.create(string(context, arg(0))));
        case append: doc.append(id(), node_id(context, arg(1))); break;
        case insert_before:doc.insert_before(id(),node_id(context,arg(1)),node_id(context,arg(2)));break;
        case remove: doc.remove(id()); break;
        case parent: return JS_NewBigUint64(context, doc.identify(doc.resolve(id())->parent));
        case body: return JS_NewBigUint64(context, doc.identify(lxb_dom_interface_node(doc.dom->body)));
        case root: return JS_NewBigUint64(context, doc.identify(lxb_dom_interface_node(doc.dom)));
        case nodetype: return JS_NewInt32(context, doc.resolve(id())->type);
        case tag: {
            size_t length;
            auto* name = lxb_dom_node_name(doc.resolve(id()), &length);
            return JS_NewStringLen(context, reinterpret_cast<const char*>(name), length);
        }
        case connected: return JS_NewBool(context, doc.connected(doc.resolve(id())));
        case focus: doc.set_focus(id()); break;
        case blur: if (doc.focused == id()) doc.set_focus(0); break;
        case active: {
            if (doc.focused && !doc.focusable(doc.resolve(doc.focused))) doc.set_focus(0);
            return JS_NewBigUint64(context, doc.focused ? doc.focused : doc.identify(lxb_dom_interface_node(doc.dom->body)));
        }
        case remove_attribute: {
            const auto name = string(context, arg(1));
            doc.remove_attribute(id(), name); break;
        }
        case checked: return JS_NewBool(context, doc.checked(id()));
        case set_checked: doc.checked(id(), JS_ToBool(context, arg(1)) != 0); break;
        case value: {
            const auto node_id = id(); auto* node = doc.resolve(node_id);
            if (doc.text_field(node)) return output(doc.editor(node_id).value);
            if (node->local_name == LXB_TAG_SELECT) {
                const auto options = doc.query_all("option", node);
                xl_node_id chosen = options.empty() ? 0 : options.front();
                for (const auto option : options)
                    if (lxb_dom_element_has_attribute(element(doc.resolve(option)), chars("selected"), 8)) chosen = option;
                if (!chosen) return output("");
                auto* option = element(doc.resolve(chosen));
                return output(lxb_dom_element_has_attribute(option, chars("value"), 5) ? doc.attribute(chosen, "value", false) : doc.text(chosen));
            }
            if (node->local_name == LXB_TAG_INPUT || node->local_name == LXB_TAG_OPTION ||
                node->local_name == LXB_TAG_BUTTON || node->local_name == LXB_TAG_PROGRESS || node->local_name == LXB_TAG_METER) {
                if (doc.checkable(node) && !lxb_dom_element_has_attribute(element(node), chars("value"), 5)) return output("on");
                return output(doc.attribute(node_id, "value", false));
            }
            return output(doc.text(node_id));
        }
        case set_value: {
            const auto node_id = id(); auto* node = doc.resolve(node_id);
            const auto text = string(context, arg(1));
            if (doc.text_field(node)) doc.edit_value(node_id, text);
            else if (node->local_name == LXB_TAG_OUTPUT) doc.set_text(node_id, text);
            else if (node->local_name == LXB_TAG_SELECT) {
                bool selected = false;
                for (const auto option : doc.query_all("option", node)) {
                    const auto option_value = lxb_dom_element_has_attribute(element(doc.resolve(option)), chars("value"), 5) ?
                        doc.attribute(option, "value", false) : doc.text(option);
                    if (!selected && option_value == text) { doc.set_attribute(option, "selected", ""); selected = true; }
                    else { lxb_dom_element_remove_attribute(element(doc.resolve(option)), chars("selected"), 8); ++doc.revision; }
                }
            } else doc.set_attribute(node_id, "value", text);
            break;
        }
        case edit_selection: {
            uint32_t anchor, focus;
            if (JS_ToUint32(context, &anchor, arg(1)) < 0 || JS_ToUint32(context, &focus, arg(2)) < 0) return JS_EXCEPTION;
            doc.edit_selection(id(), anchor, focus); break;
        }
        case edit_state: {
            const auto state = doc.edit_state(id());
            auto result = JS_NewObject(context);
            if (JS_IsException(result)) return JS_EXCEPTION;
            JS_SetPropertyStr(context, result, "anchor", JS_NewUint32(context, state.anchor_utf16));
            JS_SetPropertyStr(context, result, "focus", JS_NewUint32(context, state.focus_utf16));
            return result;
        }
        case activate: doc.activate(id()); break;
        case style: {
            try { return output(doc.style(id(), string(context, arg(1)))); }
            catch (const DocumentFailure& e) { if (e.status == XL_NOT_FOUND) return JS_NewString(context, ""); throw; }
        }
        default: throw DocumentFailure{XL_UNSUPPORTED};
        }
        return JS_UNDEFINED;
    } catch (const std::bad_alloc&) { return JS_ThrowOutOfMemory(context); }
    catch (const DocumentFailure& e) {
        if (e.status == XL_SCRIPT_ERROR) return JS_EXCEPTION;
        return JS_ThrowTypeError(context, "DOM operation %d failed: %s", operation, xl_status_string(e.status));
    } catch (const UriFailure& e) {return JS_ThrowTypeError(context,"Internal URI failed: %s",xl_status_string(e.status));}
    catch (...) { return JS_ThrowInternalError(context, "DOM operation failed"); }
}

const char bootstrap[] = R"JS((function(n) {
    'use strict';
    const cache = new Map(), ids = new WeakMap(), listeners = new WeakMap(), styleObjects = new WeakMap();
    let CanvasNode;
    function wrap(id) {
        if (id === 0n) return null;
        if (!cache.has(id)) { const Class=n.nodeType(id)===1&&n.tag(id)==='CANVAS'?CanvasNode:Node;const node = new Class(id); cache.set(id, node); }
        return cache.get(id);
    }
    const eventData = new WeakMap();
    const slot = event => {
        const value = eventData.get(event);
        if (!value) throw new TypeError('Event receiver expected');
        return value;
    };
    class Event {
        constructor(type, init = {}) {
            if (arguments.length === 0) throw new TypeError('Event type is required');
            eventData.set(this, {type:String(type),bubbles:Boolean(init.bubbles),cancelable:Boolean(init.cancelable),
                composed:Boolean(init.composed),target:null,currentTarget:null,phase:0,prevented:false,
                stopped:false,immediate:false,passive:false,dispatching:false,path:[],time:n.timeNow(),detail:null});
        }
        get type(){return slot(this).type;}
        get bubbles(){return slot(this).bubbles;}
        get cancelable(){return slot(this).cancelable;}
        get composed(){return slot(this).composed;}
        get isTrusted(){return slot(this).trusted??false;}
        get target(){return slot(this).target;}
        get srcElement(){return this.target;}
        get currentTarget(){return slot(this).currentTarget;}
        get eventPhase(){return slot(this).phase;}
        get timeStamp(){return slot(this).time;}
        get defaultPrevented(){return slot(this).prevented;}
        get cancelBubble(){return slot(this).stopped;}
        set cancelBubble(value){if(value)slot(this).stopped=true;}
        get returnValue(){return !this.defaultPrevented;}
        set returnValue(value){if(!value)this.preventDefault();}
        preventDefault(){const s=slot(this);if(s.cancelable && !s.passive)s.prevented=true;}
        stopPropagation(){slot(this).stopped=true;}
        stopImmediatePropagation(){const s=slot(this);s.immediate=true;s.stopped=true;}
        composedPath(){const s=slot(this);return s.dispatching?[...s.path]:[];}
    }
    for (const [name,value] of [['NONE',0],['CAPTURING_PHASE',1],['AT_TARGET',2],['BUBBLING_PHASE',3]]) {
        Object.defineProperty(Event,name,{value});Object.defineProperty(Event.prototype,name,{value});
    }
    class CustomEvent extends Event {
        constructor(type,init={}) {super(type,init);slot(this).detail=init.detail??null;}
        get detail(){return slot(this).detail;}
    }
    class ToggleEvent extends Event {
        constructor(type,init={}) {super(type,init);Object.assign(slot(this),{oldState:String(init.oldState??''),newState:String(init.newState??''),source:init.source??null});}
        get oldState(){return slot(this).oldState;}
        get newState(){return slot(this).newState;}
        get source(){return slot(this).source;}
    }
    class EventTarget {
        constructor() {listeners.set(this,new Map());}
        addEventListener(type, callback, options = false) {
            if (callback === null || callback === undefined) return;
            if (typeof callback !== 'function' && typeof callback !== 'object') throw new TypeError('invalid callback');
            const capture=typeof options==='boolean'?options:Boolean(options?.capture);
            let map = listeners.get(this); if (!map) listeners.set(this, map = new Map());
            const list = map.get(String(type)) ?? [];
            if (!list.some(item=>!item.removed && item.callback===callback && item.capture===capture))
                list.push({callback,capture,once:Boolean(options?.once),passive:Boolean(options?.passive),removed:false});
            map.set(String(type), list);
        }
        removeEventListener(type, callback, options = false) {
            const capture=typeof options==='boolean'?options:Boolean(options?.capture);
            const map = listeners.get(this); if (!map) return;
            const list=map.get(String(type))??[];
            for(const item of list)if(item.callback===callback && item.capture===capture)item.removed=true;
            map.set(String(type),list.filter(item=>!item.removed));
        }
        dispatchEvent(event) { return dispatchTo(this,event,false); }
    }
    function dispatchTo(target,event,trusted) {
            const state=slot(event);
            if(state.dispatching || !state.type)throw new Error('InvalidStateError');
            const path=[target];
            if(target instanceof Node)for(let node=target.parentNode;node;node=node.parentNode)path.push(node);
            state.target=target;state.trusted=trusted;state.dispatching=true;state.path=path;state.stopped=false;state.immediate=false;
            const invoke=(target,capture,phase)=>{
                state.currentTarget=target;state.phase=phase;
                for(const item of [...(listeners.get(target)?.get(state.type)??[])]) {
                    if(item.removed || item.capture!==capture)continue;
                    if(item.once)target.removeEventListener(state.type,item.callback,item.capture);
                    state.passive=item.passive;
                    try {
                        if(typeof item.callback==='function')item.callback.call(target,event);
                        else if(typeof item.callback.handleEvent==='function')item.callback.handleEvent(event);
                    } finally {state.passive=false;}
                    if(state.immediate)break;
                }
                if(!capture && !state.immediate) {
                    const handler=target['on'+state.type];
                    if(typeof handler==='function' && handler.call(target,event)===false)event.preventDefault();
                }
            };
            try {
                for(let i=path.length-1;i>0 && !state.stopped;--i)invoke(path[i],true,1);
                if(!state.stopped){invoke(target,true,2);if(!state.immediate)invoke(target,false,2);}
                if(state.bubbles)for(let i=1;i<path.length && !state.stopped;++i)invoke(path[i],false,3);
                return !state.prevented;
            } finally {
                state.currentTarget=null;state.phase=0;state.path=[];state.dispatching=false;
                state.stopped=false;state.immediate=false;state.passive=false;
            }
    }
    Object.assign(globalThis,{Event,CustomEvent,ToggleEvent,EventTarget});
    const queryData=new WeakMap(),eventQueryData=new WeakMap(),queryKey={};
    const querySlot=value=>{const data=queryData.get(value);if(!data)throw new TypeError('MediaQueryList receiver expected');return data;};
    class MediaQueryListEvent extends Event {
        constructor(type,init={}){super(type,init);eventQueryData.set(this,{matches:Boolean(init.matches),media:String(init.media??'')});}
        get matches(){const data=eventQueryData.get(this);if(!data)throw new TypeError('MediaQueryListEvent receiver expected');return data.matches;}
        get media(){const data=eventQueryData.get(this);if(!data)throw new TypeError('MediaQueryListEvent receiver expected');return data.media;}
    }
    class MediaQueryList extends EventTarget {
        constructor(key,query){super();if(key!==queryKey)throw new TypeError('Illegal constructor');queryData.set(this,{media:n.mediaText(query),watch:0,handler:null});}
        get media(){return querySlot(this).media;}
        get matches(){return n.matchMedia(querySlot(this).media);}
        get onchange(){return querySlot(this).handler;}
        set onchange(value){querySlot(this).handler=typeof value==='function'?value:null;this._sync();}
        addEventListener(type,callback,options=false){querySlot(this);super.addEventListener(type,callback,options);this._sync();}
        removeEventListener(type,callback,options=false){querySlot(this);super.removeEventListener(type,callback,options);this._sync();}
        addListener(callback){this.addEventListener('change',callback);}
        removeListener(callback){this.removeEventListener('change',callback);}
        _sync(){const state=querySlot(this),live=state.handler||(listeners.get(this)?.get('change')??[]).some(item=>!item.removed);
            if(live&&!state.watch)state.watch=n.watchMedia(state.media,(matches,media)=>{try{dispatchTo(this,new MediaQueryListEvent('change',{matches,media}),true);}finally{this._sync();}});
            else if(!live&&state.watch){n.unwatchMedia(state.watch);state.watch=0;}}
    }
    Object.assign(globalThis,{MediaQueryList,MediaQueryListEvent});
    globalThis.matchMedia=query=>new MediaQueryList(queryKey,String(query));
    Object.defineProperty(globalThis,'performance',{value:Object.freeze({now:()=>n.timeNow()})});
    globalThis.setTimeout=(callback,delay=0,...args)=>{
        if(typeof callback!=='function')throw new TypeError('timer callback must be a function');
        return n.scheduleTask(()=>callback.apply(globalThis,args),Number(delay),0);
    };
    globalThis.setInterval=(callback,delay=0,...args)=>{
        if(typeof callback!=='function')throw new TypeError('timer callback must be a function');
        return n.scheduleTask(()=>callback.apply(globalThis,args),Number(delay),1);
    };
    globalThis.clearTimeout=globalThis.clearInterval=id=>n.cancelTask(Number(id));
    globalThis.requestAnimationFrame=callback=>{
        if(typeof callback!=='function')throw new TypeError('animation callback must be a function');
        return n.scheduleTask(callback,16,2);
    };
    globalThis.cancelAnimationFrame=id=>n.cancelTask(Number(id));
    globalThis.queueMicrotask=callback=>{
        if(typeof callback!=='function')throw new TypeError('microtask callback must be a function');
        Promise.resolve().then(callback);
    };
    class Node extends EventTarget {
        constructor(id) { super(); ids.set(this, id); }
        get nodeType() { return n.nodeType(ids.get(this)); }
        get nodeName() { return n.tag(ids.get(this)); }
        get tagName() { return this.nodeType === 1 ? this.nodeName : undefined; }
        get parentNode() { return wrap(n.parent(ids.get(this))); }
        get parentElement() { const parent=this.parentNode;return parent?.nodeType===1?parent:null; }
        get childNodes() { return n.children(ids.get(this),false).map(wrap); }
        get children() { return n.children(ids.get(this),true).map(wrap); }
        get firstChild() { return this.childNodes[0]??null; }
        get lastChild() { return this.childNodes.at(-1)??null; }
        get firstElementChild() { return this.children[0]??null; }
        get lastElementChild() { return this.children.at(-1)??null; }
        get isConnected() { return n.connected(ids.get(this)); }
        get baseURI() { return n.baseURI(); }
        get href() { const value=this.getAttribute('href');return value===null?'':n.resolveURI(value); }
        set href(value) { this.setAttribute('href',value); }
        get src() { const value=this.getAttribute('src');return value===null?'':n.resolveURI(value); }
        set src(value) { this.setAttribute('src',value); }
        get textContent() { return n.text(ids.get(this)); }
        set textContent(value) { n.setText(ids.get(this), String(value ?? '')); }
        get id() { return this.getAttribute('id') ?? ''; }
        set id(value) { this.setAttribute('id', value); }
        get className() { return this.getAttribute('class') ?? ''; }
        set className(value) { this.setAttribute('class', value); }
        get hidden() { return this.hasAttribute('hidden'); }
        set hidden(value) { if(value)this.setAttribute('hidden','');else this.removeAttribute('hidden'); }
        get open() { return this.hasAttribute('open'); }
        set open(value) { if(value)this.setAttribute('open','');else this.removeAttribute('open'); }
        get name() { return this.getAttribute('name')??''; }
        set name(value) { this.setAttribute('name',String(value)); }
        get dataset() {
            const node=this,attribute=key=>'data-'+String(key).replace(/[A-Z]/g,c=>'-'+c.toLowerCase());
            return new Proxy(Object.create(null),{
                get:(_,key)=>typeof key==='string'?(node.getAttribute(attribute(key))??undefined):undefined,
                set:(_,key,value)=>{node.setAttribute(attribute(key),String(value));return true;},
                deleteProperty:(_,key)=>{node.removeAttribute(attribute(key));return true;},
                has:(_,key)=>node.hasAttribute(attribute(key))
            });
        }
        get classList() {
            const node=this,tokens=()=>[...new Set(node.className.split(/[\t\n\f\r ]+/).filter(Boolean))];
            const validate=values=>{for(const token of values)if(!token || /[\t\n\f\r ]/.test(token))throw new Error('InvalidCharacterError');};
            const write=values=>node.className=values.join(' ');
            return {
                get length(){return tokens().length;},get value(){return node.className;},set value(value){node.className=String(value);},
                contains(value){return tokens().includes(String(value));},item(index){return tokens()[index]??null;},
                add(...values){values=values.map(String);validate(values);write([...new Set([...tokens(),...values])]);},
                remove(...values){values=values.map(String);validate(values);write(tokens().filter(x=>!values.includes(x)));},
                toggle(value,force){value=String(value);validate([value]);const list=tokens(),has=list.includes(value),want=arguments.length>1?Boolean(force):!has;if(want!==has)write(want?[...list,value]:list.filter(x=>x!==value));return want;},
                replace(old,value){old=String(old);value=String(value);validate([old,value]);const list=tokens();if(!list.includes(old))return false;write([...new Set(list.map(x=>x===old?value:x))]);return true;},
                toString(){return node.className;},[Symbol.iterator](){return tokens()[Symbol.iterator]();}
            };
        }
        getAttribute(name) { return n.attribute(ids.get(this), String(name)); }
        setAttribute(name, value) { n.setAttribute(ids.get(this), String(name), String(value)); }
        removeAttribute(name) { n.removeAttribute(ids.get(this), String(name)); }
        hasAttribute(name) { return this.getAttribute(name) !== null; }
        get checked() { return n.checked(ids.get(this)); }
        set checked(value) { n.setChecked(ids.get(this), Boolean(value)); }
        get value() { return n.value(ids.get(this)); }
        set value(value) { n.setValue(ids.get(this),String(value)); }
        get defaultValue() { return this.tagName==='TEXTAREA'?this.textContent:(this.getAttribute('value')??''); }
        set defaultValue(value) { if(this.tagName==='TEXTAREA')this.textContent=String(value);else this.setAttribute('value',value); }
        get readOnly() { return this.hasAttribute('readonly'); }
        set readOnly(value) { if(value)this.setAttribute('readonly','');else this.removeAttribute('readonly'); }
        get selectionStart() { const s=n.editState(ids.get(this));return Math.min(s.anchor,s.focus); }
        get selectionEnd() { const s=n.editState(ids.get(this));return Math.max(s.anchor,s.focus); }
        get selectionDirection() { const s=n.editState(ids.get(this));return s.anchor>s.focus?'backward':s.anchor===s.focus?'none':'forward'; }
        set selectionStart(value) { this.setSelectionRange(value,Math.max(Number(value),this.selectionEnd),this.selectionDirection); }
        set selectionEnd(value) { this.setSelectionRange(Math.min(this.selectionStart,Number(value)),value,this.selectionDirection); }
        setSelectionRange(start,end,direction='none') {
            start=Math.max(0,Number(start)||0);end=Math.max(0,Number(end)||0);start=Math.min(start,end);
            n.editSelection(ids.get(this),direction==='backward'?end:start,direction==='backward'?start:end);
        }
        select() { this.setSelectionRange(0,this.value.length); }
        get disabled() { return this.hasAttribute('disabled'); }
        set disabled(value) { if(value)this.setAttribute('disabled','');else this.removeAttribute('disabled'); }
        querySelector(selector) { return wrap(n.query(ids.get(this), String(selector))); }
        querySelectorAll(selector) { return n.queryAll(ids.get(this),String(selector)).map(wrap); }
        appendChild(child) { n.append(ids.get(this), ids.get(child)); return child; }
        insertBefore(child,reference) { n.insertBefore(ids.get(this),ids.get(child),reference==null?0n:ids.get(reference));return child; }
        removeChild(child) { if(child.parentNode!==this)throw new Error('NotFoundError');child.remove();return child; }
        set innerHTML(value) { n.setInnerHTML(ids.get(this),String(value)); }
        cloneNode(deep=false) { return wrap(n.cloneNode(ids.get(this),Boolean(deep))); }
        contains(other) { for(let node=other;node;node=node.parentNode)if(node===this)return true;return false; }
        matches(selector) {
            let root=this;while(root.parentNode)root=root.parentNode;
            return n.queryAll(ids.get(root),String(selector)).some(id=>id===ids.get(this));
        }
        closest(selector) { for(let node=this;node?.nodeType===1;node=node.parentElement)if(node.matches(selector))return node;return null; }
        remove() { n.remove(ids.get(this)); }
        focus() { n.focus(ids.get(this)); }
        blur() { n.blur(ids.get(this)); }
        click() { n.activate(ids.get(this)); }
        get style() {
            if(styleObjects.has(this))return styleObjects.get(this);
            const element = this;
            const declarations=()=>{
                const parts=[];let start=0,depth=0,quote='',escape=false;
                const text=element.getAttribute('style')??'';
                for(let i=0;i<=text.length;++i) {
                    const c=text[i];
                    if(escape){escape=false;continue;}
                    if(c==='\\'){escape=true;continue;}
                    if(quote){if(c===quote)quote='';continue;}
                    if(c==='"' || c==="'"){quote=c;continue;}
                    if(c==='(' || c==='[' || c==='{')++depth;
                    if(c===')' || c===']' || c==='}')--depth;
                    if(i===text.length || (c===';' && depth===0)) {
                        const raw=text.slice(start,i),colon=raw.indexOf(':');start=i+1;
                        if(colon>=0)parts.push([raw.slice(0,colon).trim(),raw.slice(colon+1).trim()]);
                    }
                }
                return parts;
            };
            const equal=(a,b)=>a.startsWith('--') || b.startsWith('--')?a===b:a.toLowerCase()===b.toLowerCase();
            const value=name=>{let result='',important=false;for(const [key,text] of declarations())if(equal(key,String(name))){const priority=/!important\s*$/i.test(text);if(!important || priority){result=text;important=priority;}}return result;};
            const declaration = {
                get cssText() { return element.getAttribute('style') ?? ''; },
                set cssText(value) { element.setAttribute('style', value); },
                getPropertyValue(name){return value(name).replace(/\s*!important\s*$/i,'');},
                getPropertyPriority(name){return /!important\s*$/i.test(value(name))?'important':'';},
                removeProperty(name){const old=this.getPropertyValue(name);element.setAttribute('style',declarations().filter(([key])=>!equal(key,String(name))).map(([key,value])=>key+':'+value).join(';'));return old;},
                setProperty(name, value, priority = '') {
                    name=String(name);value=String(value);priority=String(priority);
                    if(!value){this.removeProperty(name);return;}
                    if (priority && priority.toLowerCase() !== 'important') return;
                    element.setAttribute('style', (element.getAttribute('style') ?? '') + ';' + name + ':' + value + (priority ? ' !important' : ''));
                }
            };
            const cssName=name=>String(name).replace(/[A-Z]/g,c=>'-'+c.toLowerCase());
            const result=new Proxy(declaration,{
                get(object,name,receiver){return typeof name!=='string' || name in object?Reflect.get(object,name,receiver):object.getPropertyValue(cssName(name));},
                set(object,name,value,receiver){if(name==='cssText')return Reflect.set(object,name,value,receiver);if(typeof name==='string')object.setProperty(cssName(name),value);return true;}
            });
            styleObjects.set(this,result);return result;
        }

    }
    CanvasNode=globalThis.__xlCanvasBootstrap(n,ids,Node);delete globalThis.__xlCanvasBootstrap;
    const document = wrap(n.root());
    Object.defineProperties(document, {
        body: {get() { return wrap(n.body()); }},
        documentElement: {get() { return document.children.find(node=>node.tagName==='HTML')??null; }},
        readyState: {value:'complete'},
        URL: {get() { return n.uri(); }},
        documentURI: {get() { return n.uri(); }},
        hidden: {get() { return n.environment().hidden; }},
        visibilityState: {get() { return n.environment().hidden?'hidden':'visible'; }},
        activeElement: {get() { return wrap(n.active()); }},
        getElementById: {value(name) { return wrap(n.byId(String(name))); }},
        createElement: {value(name) { return wrap(n.create(String(name))); }},
        createDocumentFragment: {value() { return wrap(n.fragment()); }},
        getElementsByName: {value(name) { return document.querySelectorAll('[name]').filter(node=>node.getAttribute('name')===String(name)); }}
    });
    Object.defineProperty(globalThis, 'document', {value: document, configurable: true});
    globalThis.window = globalThis;
    for(const name of ['addEventListener','removeEventListener','dispatchEvent'])
        globalThis[name]=EventTarget.prototype[name].bind(globalThis);
    let resizeHandler=null;
    const syncResize=()=>n.observeResize(resizeHandler!==null||(listeners.get(globalThis)?.get('resize')??[]).some(item=>!item.removed));
    for(const method of ['addEventListener','removeEventListener'])globalThis[method]=function(...args){
        EventTarget.prototype[method].apply(globalThis,args);syncResize();
    };
    Object.defineProperty(globalThis,'onresize',{get:()=>resizeHandler,set:value=>{resizeHandler=typeof value==='function'?value:null;syncResize();},configurable:true});
    Object.defineProperty(globalThis,'location',{value:Object.freeze({
        get href(){return n.uri();},get hash(){const uri=n.uri(),index=uri.indexOf('#');return index<0?'':uri.slice(index);},
        get protocol(){return n.uri().split(':')[0]+':';},get origin(){return n.uri().split('/').slice(0,3).join('/');}
    })});
    Object.defineProperty(globalThis,'navigator',{value:Object.freeze({userAgent:'XLWebEngineUI/0.1.0',appName:'XLWebEngineUI'})});
    Object.defineProperties(globalThis,{
        innerWidth:{get:()=>n.environment().width},innerHeight:{get:()=>n.environment().height},
        devicePixelRatio:{get:()=>n.environment().pixelRatio},
        screen:{value:Object.freeze({get width(){return n.environment().screenWidth;},get height(){return n.environment().screenHeight;},get colorDepth(){return n.environment().colorDepth;}})}
    });
    Object.defineProperty(globalThis,'xl',{value:Object.freeze({
        resolveURI(value) { return n.resolveURI(String(value)); },
        readText(value) { return n.readContent(String(value)); }
    })});
    globalThis.getComputedStyle = function(element,pseudo=null) {
        return {getPropertyValue(name) { return pseudo ? n.pseudoStyle(ids.get(element),String(pseudo),String(name)) : n.style(ids.get(element), String(name)); }};
    };
    delete globalThis.__xlNative;
    return function(id, type, init = {}) {
        const target = init.window ? globalThis : wrap(id);
        let clipboardLive=Boolean(init.clipboard),clipboardText=init.clipboardText??null;
        const format=value=>{value=String(value).toLowerCase();return value==='text'?'text/plain':value;};
        const transfer=init.clipboard?Object.freeze({
            get types(){return Object.freeze(clipboardLive && clipboardText!==null?['text/plain']:[]);},
            getData(type){return clipboardLive && format(type)==='text/plain'?(clipboardText??''):'';},
            setData(type,value){
                if(!clipboardLive || init.clipboardPaste || format(type)!=='text/plain')return;
                value=String(value);if(value.length>4*1024*1024)throw new RangeError('clipboard text exceeds 4MiB');
                clipboardText=value;
            },
            clearData(type){if(clipboardLive && !init.clipboardPaste && (type===undefined || format(type)==='text/plain'))clipboardText=null;}
        }):null;
        const event = init.toggle ? new ToggleEvent(type,init) : new Event(type,{bubbles:init.bubbles??true,cancelable:init.cancelable??true});
        Object.assign(event,{
            relatedTarget:wrap(init.related??0n),key:init.key??'',code:init.code??'',location:init.location??0,
            repeat:Boolean(init.repeat),isComposing:Boolean(init.isComposing),data:init.data??null,inputType:init.inputType??'',
            shiftKey:Boolean(init.shiftKey),ctrlKey:Boolean(init.ctrlKey),altKey:Boolean(init.altKey),metaKey:Boolean(init.metaKey),
            getModifierState(name){return Boolean({Shift:this.shiftKey,Control:this.ctrlKey,Alt:this.altKey,Meta:this.metaKey,CapsLock:init.capsLock,NumLock:init.numLock}[name]);}
        });
        Object.defineProperty(event,'clipboardData',{value:transfer,enumerable:true});
        try {
            return {allowed:dispatchTo(target,event,init.trusted??true),clipboardText};
        } finally {
            clipboardLive=false;clipboardText=null;init.clipboardText=null;
        }
    };
})(globalThis.__xlNative))JS";
}

void Document::Impl::initialize_script() {
    if (context && JS_IsFunction(context, dispatcher)) return;
    close_script();
    js = JS_NewRuntime();
    if (!js) throw std::bad_alloc();
    JS_SetMemoryLimit(js, 32 * 1024 * 1024);
    JS_SetMaxStackSize(js, 512 * 1024);
    JS_SetInterruptHandler(js, [](JSContext*, void* opaque) {
        auto& doc = *static_cast<Impl*>(opaque);
        if (doc.executing && std::chrono::steady_clock::now() >= doc.deadline) { doc.timed_out = true; return 1; }
        return 0;
    }, this);
    context = JS_NewContext(js);
    if (!context) { close_script(); throw std::bad_alloc(); }
    JS_SetContextOpaque(context, this);
    Value global{context, JS_GetGlobalObject(context)};
    auto object = JS_NewObject(context);
    const char* methods[] = {"query", "byId", "setAttribute", "attribute", "setText", "text", "create", "append", "remove",
                              "parent", "body", "root", "nodeType", "tag", "connected", "focus", "active", "activate", "style", "uri", "baseURI", "resolveURI", "readContent", "queryAll", "blur", "removeAttribute", "checked", "setChecked", "value", "setValue", "editState", "editSelection", "scheduleTask", "cancelTask", "timeNow", "children", "fragment", "setInnerHTML", "cloneNode", "environment", "pseudoStyle", "matchMedia", "mediaText", "watchMedia", "unwatchMedia", "observeResize", "insertBefore"};
    for (int index = 0; index < static_cast<int>(std::size(methods)); ++index)
        JS_SetPropertyStr(context, object, methods[index], JS_NewCFunctionMagic(context, native, methods[index], 3, JS_CFUNC_generic_magic, index));
    JS_SetPropertyStr(context, global.value, "__xlNative", object);
    JS_SetPropertyStr(context, object, "canvas", JS_NewCFunction(context,canvas_native,"canvas",3));
    Value canvas_init{context,JS_Eval(context,canvas_bootstrap,std::strlen(canvas_bootstrap),"xl-canvas-bootstrap.js",JS_EVAL_TYPE_GLOBAL)};
    if(JS_IsException(canvas_init.value))throw DocumentFailure{XL_INTERNAL_ERROR};
    dispatcher = JS_Eval(context, bootstrap, std::strlen(bootstrap), "xl-dom-bootstrap.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(dispatcher)) throw DocumentFailure{XL_INTERNAL_ERROR};
}
void Document::Impl::close_script() {
    for(const auto& [id,watch]:media_watches){(void)id;JS_FreeValue(context,watch.callback);}media_watches.clear();resize_pending=resize_observed=visibility_pending=false;
    for (const auto& [id, timer] : timers) { (void)id; JS_FreeValue(context, timer.callback); }
    timers.clear();
    if (context) { JS_FreeValue(context, dispatcher); dispatcher = JS_UNDEFINED; JS_FreeContext(context); context = nullptr; }
    if (js) { JS_FreeRuntime(js); js = nullptr; }
}
xl_status Document::Impl::evaluate(std::string_view code, uint32_t budget) {
    initialize_script();
    executing = true; timed_out = false;
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget ? budget : 100);
    struct End { bool& flag; ~End() { flag = false; } } end{executing};
    // QuickJS requires a trailing NUL even with an explicit input length.
    const std::string source(code);
    Value result{context, JS_Eval(context, source.c_str(), source.size(), "application.js", JS_EVAL_TYPE_GLOBAL)};
    if (!JS_IsException(result.value)) {
        JSContext* job_context;
        int pending;
        do {
            pending = JS_ExecutePendingJob(js, &job_context);
            if (pending < 0) {
                Value exception{job_context, JS_GetException(job_context)};
                script_result = string(job_context, exception.value);
                return timed_out ? XL_TIMEOUT : XL_SCRIPT_ERROR;
            }
            if (std::chrono::steady_clock::now() >= deadline) { timed_out = true; break; }
        } while (pending > 0);
    }
    if (JS_IsException(result.value)) {
        Value exception{context, JS_GetException(context)};
        try { script_result = string(context, exception.value); }
        catch (const DocumentFailure&) {
            Value ignored{context, JS_GetException(context)};
            script_result = timed_out ? "script budget exhausted" : "unprintable script exception";
        }
        return timed_out ? XL_TIMEOUT : XL_SCRIPT_ERROR;
    }
    if (timed_out) { script_result = "script budget exhausted"; return XL_TIMEOUT; }
    try { script_result = string(context, result.value); }
    catch (const DocumentFailure&) {
        Value exception{context, JS_GetException(context)};
        script_result = timed_out ? "script budget exhausted" : "script result conversion failed";
        return timed_out ? XL_TIMEOUT : XL_SCRIPT_ERROR;
    }
    return XL_OK;
}
bool Document::Impl::dispatch(xl_node_id id, const char* type, const xl_key_event* key,
                              xl_node_id related, bool bubbles, bool cancelable, bool trusted, const TextEvent* text,
                              ClipboardEvent* clipboard, const DetailsToggle* toggle) {
    if (!context) return true; // No script/listeners: input does not eagerly create a VM.
    const bool outer = !executing;
    if (outer) { deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100); timed_out = false; executing = true; }
    struct End { bool& flag; bool outer; ~End() { if (outer) flag = false; } } end{executing, outer};
    const auto fail = [&](JSContext* failed) {
        if (outer) {
            Value error{failed, JS_GetException(failed)};
            try { script_result = string(failed, error.value); }
            catch (const DocumentFailure&) {
                Value ignored{failed, JS_GetException(failed)};
                script_result = timed_out ? "script budget exhausted" : "unprintable event exception";
            }
        }
        throw DocumentFailure{timed_out ? XL_TIMEOUT : XL_SCRIPT_ERROR};
    };
    Value first{context, JS_NewBigUint64(context, id)}, second{context, JS_NewString(context, type)};
    Value init{context, JS_NewObject(context)};
    const auto property = [&](const char* name, JSValue value) {
        if (JS_SetPropertyStr(context, init.value, name, value) < 0) fail(context);
    };
    if (JS_IsException(first.value) || JS_IsException(second.value) || JS_IsException(init.value)) fail(context);
    property("related", JS_NewBigUint64(context, related));
    property("bubbles", JS_NewBool(context, bubbles)); property("cancelable", JS_NewBool(context, cancelable));
    property("trusted", JS_NewBool(context, trusted));
    if (toggle) {
        property("toggle", JS_TRUE);
        property("oldState", JS_NewString(context, toggle->old_open ? "open" : "closed"));
        property("newState", JS_NewString(context, toggle->new_open ? "open" : "closed"));
    }
    if (clipboard) {
        property("clipboard", JS_TRUE); property("clipboardPaste", JS_NewBool(context, clipboard->paste));
        property("clipboardText", clipboard->text ? JS_NewStringLen(context, clipboard->text->data(), clipboard->text->size()) : JS_NULL);
    }
    bool composing = text && text->composing;
    if (const auto found = editors.find(id); found != editors.end() && found->second.composition) composing = true;
    property("isComposing", JS_NewBool(context, composing));
    if (text) {
        property("data", text->data ? JS_NewStringLen(context, text->data->data(), text->data->size()) : JS_NULL);
        property("inputType", JS_NewStringLen(context, text->input_type.empty() ? "" : text->input_type.data(), text->input_type.size()));
    }
    if (key) {
        property("key", JS_NewString(context, key->key_utf8)); property("code", JS_NewString(context, key->code_utf8));
        property("location", JS_NewUint32(context, key->location)); property("repeat", JS_NewBool(context, key->repeat != 0));
        for (const auto& [name, mask] : {std::pair{"shiftKey", XL_MOD_SHIFT}, {"ctrlKey", XL_MOD_CONTROL},
            {"altKey", XL_MOD_ALT}, {"metaKey", XL_MOD_META}, {"capsLock", XL_MOD_CAPS_LOCK}, {"numLock", XL_MOD_NUM_LOCK}})
            property(name, JS_NewBool(context, (key->modifiers & mask) != 0));
    }
    JSValue args[]{first.value, second.value, init.value};
    Value result{context, JS_Call(context, dispatcher, JS_UNDEFINED, 3, args)};
    if (JS_IsException(result.value)) fail(context);
    Value allowed_value{context, JS_GetPropertyStr(context, result.value, "allowed")};
    if (JS_IsException(allowed_value.value)) fail(context);
    const auto allowed = JS_ToBool(context, allowed_value.value) != 0;
    if (clipboard && !clipboard->paste) {
        Value data{context, JS_GetPropertyStr(context, result.value, "clipboardText")};
        if (JS_IsException(data.value)) fail(context);
        if (JS_IsNull(data.value)) clipboard->text.reset();
        else clipboard->text = string(context, data.value);
    }
    if (outer) {
        JSContext* pending_context = nullptr;
        int pending;
        do {
            pending = JS_ExecutePendingJob(js, &pending_context);
            if (pending < 0) fail(pending_context);
            if (std::chrono::steady_clock::now() >= deadline) {
                timed_out = true; script_result = "script budget exhausted"; throw DocumentFailure{XL_TIMEOUT};
            }
        } while (pending > 0);
    }
    return allowed;
}
}
