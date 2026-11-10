#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main() {
    xl::Runtime runtime; xl::Document doc(runtime);
    doc.html("<div id=a><span id=b></span></div><input id=c type=checkbox><select id=s><option value=one>One</option><option value=two>Two</option></select>");
    const auto script=[&](const char* code,const char* expected) {
        const auto status=doc.evaluate(code);
        if(status!=XL_OK || doc.result()!=expected){std::cerr<<code<<"\n"<<xl_status_string(status)<<": "<<doc.result()<<" expected "<<expected<<'\n';std::exit(1);}
    };
    script("let bus=new EventTarget(),calls=[],once=e=>{calls.push(e.detail);e.preventDefault()};bus.addEventListener('p',once,{once:true});bus.addEventListener('p',once);let e=new CustomEvent('p',{detail:42,cancelable:true});[bus.dispatchEvent(e),e.defaultPrevented,e.target===bus,e.currentTarget===null,e.eventPhase,bus.dispatchEvent(new Event('p')),calls.join(',')].join(':')", "false:true:true:true:0:true:42");
    script("let passive=new Event('p',{cancelable:true});bus.addEventListener('p',e=>e.preventDefault(),{passive:true});bus.dispatchEvent(passive)+':'+passive.defaultPrevented", "true:false");
    script("let a=document.getElementById('a'),b=document.getElementById('b'),order=[];a.addEventListener('x',e=>order.push('capture:'+e.eventPhase),true);b.addEventListener('x',e=>order.push('target:'+e.detail));a.addEventListener('x',e=>order.push('bubble:'+e.eventPhase));b.dispatchEvent(new CustomEvent('x',{bubbles:true,detail:7}));order.join(',')", "capture:1,target:7,bubble:3");
    script("let recursive=false;bus.addEventListener('r',e=>{try{bus.dispatchEvent(e)}catch(error){recursive=true}});bus.dispatchEvent(new Event('r'));recursive", "true");
    script("let c=document.getElementById('c'),eventType=false;c.onclick=e=>eventType=e instanceof Event;c.click();eventType+':'+c.checked", "true:true");
    script("let errors=new EventTarget(),bad=new Event('bad');errors.addEventListener('bad',()=>{throw Error('listener')},{once:true});try{errors.dispatchEvent(bad)}catch(error){};errors.dispatchEvent(bad)+':'+bad.eventPhase", "true:0");
    script("let frag=document.createDocumentFragment(),child=document.createElement('div');child.innerHTML='<span class=inside>ok</span>';frag.appendChild(child);a.appendChild(frag);child.dataset.testKey='data';child.classList.add('one','two');child.classList.toggle('one');[frag.childNodes.length,child.parentNode===a,child.firstElementChild.textContent,child.getAttribute('data-test-key'),child.classList.contains('two')].join(':')", "0:true:ok:data:true");
    script("let old=child.firstChild;child.innerHTML='<b>new</b>';old.textContent+':'+old.isConnected+':'+child.firstChild.tagName", "ok:false:B");
    script("child.style.cssText='--key:fn(1;2);color:red !important;color:blue';child.style.getPropertyValue('--key')+':'+child.style.getPropertyValue('color')+':'+child.style.getPropertyPriority('color')+':'+child.style.removeProperty('--key')", "fn(1;2):red:important:fn(1;2)");
    script("child.style.backgroundColor='blue';(child.style===child.style)+':'+child.style.getPropertyValue('background-color')", "true:blue");
    script("let delivered=null;c.onclick=e=>{delivered=e};c.click();let targetSeen=false;a.addEventListener('click',e=>{targetSeen=e.target===a && e.currentTarget===a});a.dispatchEvent(delivered);targetSeen+':'+delivered.isTrusted+':'+delivered.eventPhase", "true:false:0");
    script("let s=document.getElementById('s');s.value='two';s.value+':'+c.value", "two:on");
    script("document.hidden+':'+document.visibilityState", "false:visible");
    script("let target=document.createElement('div'),tail=document.createElement('span'),first=document.createElement('b');target.appendChild(tail);target.insertBefore(first,tail);let inserted=document.createDocumentFragment();inserted.appendChild(document.createElement('i'));target.insertBefore(inserted,tail);[target.firstChild===first,target.children.map(x=>x.tagName).join(','),inserted.childNodes.length,target.insertBefore(first,first)===first].join(':')", "true:B,I,SPAN:0:true");
    xl_view_options options; CHECK(xl_view_options_init(&options,sizeof(options))==XL_OK);
    options.kind=XL_VIEW_OFFSCREEN; options.width=32;options.height=32;
    xl::View view(runtime,&options);view.document(&doc);
    int64_t delay = 0; CHECK(xl_view_next_wakeup_ms(view.get(), &delay) == XL_OK && delay == -1);
    script("globalThis.tasks=[];let cancelled=setTimeout(()=>tasks.push('cancelled'),0);clearTimeout(cancelled);setTimeout(value=>{tasks.push(value);queueMicrotask(()=>tasks.push('micro'))},0,'timer');setTimeout(()=>{tasks.push('later');setTimeout(()=>tasks.push('next'),0)},0);let interval=setInterval(()=>{tasks.push('interval');clearInterval(interval)},1);requestAnimationFrame(time=>tasks.push(typeof time==='number'?'frame':'bad'));tasks.length", "0");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(view.stats().needs_frame);
    CHECK(xl_view_next_wakeup_ms(view.get(), &delay) == XL_OK && delay == 0);
    // The content contains text: removing it before paint keeps this scheduler
    // test independent of the asynchronous font/ICU preparation pipeline.
    script("a.remove();document.getElementById('s').remove();c.setAttribute('style','display:none');'ready'", "ready");
    uint32_t flags;CHECK(xl_view_render(view.get(),&flags)==XL_OK);
    script("tasks.join(',')", "timer,micro,later,interval,frame");
    CHECK(xl_view_render(view.get(),&flags)==XL_OK);
    script("tasks.join(',')", "timer,micro,later,interval,frame,next");
    script("let count=0;for(let i=0;i<70;i++)setTimeout(()=>count++,0);'ready'", "ready");
    CHECK(xl_view_render(view.get(),&flags)!=XL_SCRIPT_ERROR);script("String(count)","64");
    CHECK(xl_view_render(view.get(),&flags)!=XL_SCRIPT_ERROR);script("String(count)","70");
    script("setTimeout(()=>{throw Error('timer failure')},0);'scheduled'", "scheduled");
    CHECK(xl_view_render(view.get(),&flags)==XL_SCRIPT_ERROR);
    script("setTimeout(()=>{while(true){}},0);'scheduled'", "scheduled");
    CHECK(xl_view_render(view.get(),&flags)==XL_TIMEOUT);
    script("setTimeout(()=>globalThis.leaked=true,0);'scheduled'", "scheduled");
    doc.html("<div></div>");CHECK(xl_view_render(view.get(),&flags)!=XL_SCRIPT_ERROR);
    script("typeof leaked", "undefined");
    CHECK(xl_view_next_wakeup_ms(view.get(), &delay) == XL_OK && delay == -1);
}
