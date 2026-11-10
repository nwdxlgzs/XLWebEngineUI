#include "document/dom_internal.h"
#include "style/media_query.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace xl::detail {
uint32_t Document::Impl::watch_media(std::shared_ptr<MediaQuery> query,JSValueConst callback) {
    if(!JS_IsFunction(context,callback))throw DocumentFailure{XL_INVALID_ARGUMENT};
    if(media_watches.size()>=1024)throw DocumentFailure{XL_BUSY};
    uint32_t id;do{id=next_media_watch++;}while(!id||media_watches.contains(id));
    const auto function=JS_DupValue(context,callback);const auto matches=query->matches(environment);
    try {media_watches.emplace(id,MediaWatch{std::move(query),function,matches,false,0});}
    catch(...){JS_FreeValue(context,function);throw;}
    return id;
}
void Document::Impl::unwatch_media(uint32_t id) {
    const auto found=media_watches.find(id);if(found!=media_watches.end()){JS_FreeValue(context,found->second.callback);media_watches.erase(found);}
}
double Document::Impl::time_now() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - time_origin).count();
}
uint32_t Document::Impl::schedule(JSValueConst callback, double delay, unsigned kind) {
    if (!JS_IsFunction(context, callback) || kind > 2) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (timers.size() >= 1024) throw DocumentFailure{XL_BUSY};
    if (!std::isfinite(delay)) delay = 0;
    const auto milliseconds = static_cast<uint32_t>(std::clamp(delay, kind == 1 ? 1.0 : 0.0, 2147483647.0));
    uint32_t id;
    do { id = next_timer++; } while (!id || timers.contains(id));
    const auto function = JS_DupValue(context, callback);
    try { timers.emplace(id, ScriptTimer{function, std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds),
                                        milliseconds, kind == 1, kind == 2, next_task_order++}); }
    catch (...) { JS_FreeValue(context, function); throw; }
    return id;
}
void Document::Impl::cancel_timer(uint32_t id) {
    const auto found = timers.find(id);
    if (found != timers.end()) { JS_FreeValue(context, found->second.callback); timers.erase(found); }
}
bool Document::Impl::tasks_ready() const {
    if (!details_toggles.empty()) return true;
    if(resize_pending||visibility_pending||std::any_of(media_watches.begin(),media_watches.end(),[](const auto& watch){return watch.second.pending;}))return true;
    const auto now = std::chrono::steady_clock::now();
    return std::any_of(timers.begin(), timers.end(), [&](const auto& entry) { return !(entry.second.animation&&environment.hidden)&&entry.second.due <= now; });
}
void Document::Impl::run_tasks() {
    if (timers.empty() && details_toggles.empty() && !resize_pending && !visibility_pending && !std::any_of(media_watches.begin(),media_watches.end(),[](const auto& watch){return watch.second.pending;})) return;
    if (executing) throw DocumentFailure{XL_BUSY};
    const auto now = std::chrono::steady_clock::now();
    struct Task { std::chrono::steady_clock::time_point due; uint64_t order, id; unsigned kind; };
    std::vector<Task> ready;
    for (const auto& [id, timer] : timers) if (timer.due <= now&&!(timer.animation&&environment.hidden)) ready.push_back({timer.due, timer.order, id, false});
    for (const auto& [id, toggle] : details_toggles) ready.push_back({toggle.due, toggle.order, id, true});
    for(const auto& [id,watch]:media_watches)if(watch.pending)ready.push_back({now,watch.order,id,2});
    if(resize_pending)ready.push_back({now,resize_order,0,3});
    if(visibility_pending)ready.push_back({now,visibility_order,0,4});
    std::sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) { return a.due == b.due ? a.order < b.order : a.due < b.due; });
    if (ready.size() > 64) ready.resize(64);
    executing = true; timed_out = false; deadline = now + std::chrono::milliseconds(100);
    struct End { bool& value; ~End() { value = false; } } end{executing};
    const auto failure = [&](JSContext* failed) {
        const auto error = JS_GetException(failed);
        const auto* text = JS_ToCString(failed, error);
        if (text) { script_result = text; JS_FreeCString(failed, text); }
        else { const auto ignored = JS_GetException(failed); JS_FreeValue(failed, ignored); script_result = "script task failed"; }
        JS_FreeValue(failed, error);
        throw DocumentFailure{timed_out ? XL_TIMEOUT : XL_SCRIPT_ERROR};
    };
    for (const auto& task : ready) {
        if(task.kind==4) {
            if(!visibility_pending||visibility_order!=task.order)continue;visibility_pending=false;
            dispatch(identify(lxb_dom_interface_node(dom)),"visibilitychange",nullptr,0,false,false,true);
        } else if(task.kind==2) {
            const auto found=media_watches.find(static_cast<uint32_t>(task.id));
            if(found==media_watches.end()||!found->second.pending||found->second.order!=task.order)continue;
            auto& watch=found->second;watch.pending=false;const auto matches=watch.query->matches(environment);
            if(matches==watch.last)continue;
            watch.last=matches;const auto callback=JS_DupValue(context,watch.callback);const auto media=watch.query->serialized();
            struct Release {JSContext* context;JSValue value;~Release(){JS_FreeValue(context,value);}} release{context,callback};
            JSValue args[]{JS_NewBool(context,matches),JS_NewStringLen(context,media.data(),media.size())};
            const auto result=JS_Call(context,callback,JS_UNDEFINED,2,args);JS_FreeValue(context,args[0]);JS_FreeValue(context,args[1]);
            if(JS_IsException(result))failure(context);JS_FreeValue(context,result);
        } else if(task.kind==3) {
            if(!resize_pending||resize_order!=task.order)continue;resize_pending=false;
            auto init=JS_NewObject(context);JS_SetPropertyStr(context,init,"window",JS_TRUE);
            JSValue args[]{JS_NewBigUint64(context,0),JS_NewString(context,"resize"),init};
            const auto result=JS_Call(context,dispatcher,JS_UNDEFINED,3,args);for(auto& arg:args)JS_FreeValue(context,arg);
            if(JS_IsException(result))failure(context);JS_FreeValue(context,result);
        } else if (task.kind==1) {
            const auto found = details_toggles.find(task.id);
            if (found == details_toggles.end() || found->second.order != task.order) continue;
            const auto toggle = found->second;
            details_toggles.erase(found); // Changes in the listener queue a new task for the next turn.
            try { dispatch(task.id, "toggle", nullptr, 0, false, false, true, nullptr, nullptr, &toggle); }
            catch (const DocumentFailure& error) {
                if (context && (error.status == XL_SCRIPT_ERROR || error.status == XL_TIMEOUT)) failure(context);
                throw;
            }
        } else {
        const auto id = static_cast<uint32_t>(task.id);
        auto found = timers.find(id);
        if (found == timers.end()) continue;
        const auto callback = JS_DupValue(context, found->second.callback);
        struct Release { JSContext* context; JSValue value; ~Release() { JS_FreeValue(context, value); } } release{context, callback};
        const bool animation = found->second.animation;
        if (found->second.repeat) found->second.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(found->second.delay);
        else cancel_timer(id);
        JSValue timestamp = JS_NewFloat64(context, time_now());
        const auto result = JS_Call(context, callback, JS_UNDEFINED, animation ? 1 : 0, animation ? &timestamp : nullptr);
        JS_FreeValue(context, timestamp);
        if (JS_IsException(result)) failure(context);
        JS_FreeValue(context, result);
        }
        JSContext* job = nullptr; int pending;
        pending = 0;
        while (js && (pending = JS_ExecutePendingJob(js, &job)) > 0)
            if (std::chrono::steady_clock::now() >= deadline) { timed_out = true; break; }
        if (pending < 0) failure(job);
        if (timed_out || std::chrono::steady_clock::now() >= deadline) throw DocumentFailure{XL_TIMEOUT};
    }
}
}
