#pragma once
// Mechanical approved R2 transfer, not controller/Engine/DSP. tests/rot_ui_transfer.py.
// Scalar frame and separate Artwork are const frozen copies; see docs/rot-visual-runtime.md.
#include "rot_material.h"
#include "editor_style.h"
#include "module_heading.h"
#include "module_fader.h"
#include "module_footer_layout.h"
namespace seance::platform::ui3ds::rot_visual {
namespace m=editor_style;
using seance::ui::rect;using seance::ui::line;
inline void word(int x, int y, const char* s, bool active) {
    text(x, y, s, active ? palette::spot() : palette::ink());
    if (active) rect(float(x), float(y + 12), float(text_w(s) - 1), 1.f, palette::spot());
}

inline constexpr const char* names[]={"pitch","level","rot","spread","smear","holes","crackle","wow","bits","heal"};
inline void caption_text(char* out,size_t capacity,const char* text,int width){
    const int limit=std::min(int(capacity)-1,std::min(62,std::max(0,width/6)));int n=0;
    if(text)while(n<limit&&text[n]){out[n]=text[n];++n;}
    if(text&&text[n])for(int i=n-std::min(3,n);i<n;++i)out[i]='.';
    out[n]=0;
}
struct NativePen {
    void rect(int x,int y,int w,int h,uint32_t c)const{ui::rect(x,y,w,h,c);}
    void line(int x,int y,int xx,int yy,uint32_t c)const{ui::line(x,y,xx,yy,c);}
    void text(int x,int y,const char* s,uint32_t c)const{ui::text(x,y,s,c);}
    void right(int x,int y,const char* s,uint32_t c)const{right_text(x,y,s,c);}
};
// Submission seam only: the exact shared lancet detail::draw owns the stencil.
// Native calls retain horizontal; isolated fixtures need no second header copy.
template<class Pen> struct LancetPen {
    const Pen& pen;int x,y;
    void block(int u,int vv,int w,int h,uint32_t c)const{pen.rect(x+u,y+vv,w,h,c);}
};
template<class Pen> inline void knob_slider(const Pen& pen,int x0,int x1,int y,int edit,int live,module_fader::State state,int neutral){
    module_fader::detail::draw(LancetPen<Pen>{pen,x0,y},x1-x0,edit-x0,live-x0,state,neutral<0?-1:neutral-x0,false);
}
inline void knob_slider(const NativePen&,int x0,int x1,int y,int edit,int live,module_fader::State state,int neutral){
    module_fader::horizontal(x0,x1,y,edit,live,state,neutral);
}
template<class Pen> inline void knob_grid(const Pen& pen,const RotFrame& f){
    for(int i=0;i<10;++i){const auto& k=f.knobs[i];
        const int x0=(i&1)?174:14,x1=(i&1)?305:143,y=102+(i/2)*24;
        const int r0=x0+4,r1=x1-2;
        if(!f.lens)pen.text(x0,y,names[i],i==f.selected?palette::spot():palette::ink());
        char value[16];std::memcpy(value,k.value,16);value[15]=0;
        const int width=(x1-x0-text_w(names[i])-8)/6*6;
        char bounded[16];caption_text(bounded,sizeof bounded,value,width);
        if(!f.lens)pen.text(x1-std::min(width,text_w(value)),y,bounded,palette::ghost());
        knob_slider(pen,r0,r1,y+14,r0+(r1-r0)*std::clamp(k.edit_q8,0,256)/256,r0+(r1-r0)*std::clamp(k.live_q8,0,256)/256,
            {k.linked,k.disabled,k.quiet||f.status!=RotFrame::Ready},i==0?(r0+r1)/2:-1);
    }
}
inline void knob_grid(const RotFrame& f){knob_grid(NativePen{},f);}
inline void rot_draw_top(const RotFrame& f,const Artwork& a,const Resource& resource) {
    m::paper(400);
    if (!f.valid) return;
    (void)a;
    module_heading::header(UnitKind::Rot, m::spot());
    char buf[40];
    if (f.law && f.pass + 1 > f.passes) std::snprintf(buf, sizeof buf, "pass %02d", f.pass + 1);
    else std::snprintf(buf, sizeof buf, "pass %02d / %02d", std::min(f.pass + 1, f.passes), f.passes);
    m::header_text(76, 386 - text_w(buf) - 8, f.source, palette::ink());
    right_text(m::right_edge(400), m::kHeaderTextY, buf, palette::ink());
    std::snprintf(buf, sizeof buf, "rot %d%%", f.rot_percent);
    text(14, 38, buf, palette::spot());
    static const char* const status[] = {"rotting", "healing", "holding", "tail"};
    right_text(386, 38, f.status==RotFrame::Ready?status[std::clamp(int(f.mode),0,3)]:status_text(f), palette::ink());

    const bool art=resource.draw_top_art(f);
    if(shows_art(f)&&!art) text(14,60,"art unavailable",palette::ghost());
    else if(!shows_art(f)&&f.detail[0]) m::caption(14,60,372,f.detail,palette::ghost());
    if(art&&moving(f)&&f.head>=0&&f.head<372)line(14+f.head,136,14+f.head,204,palette::playhead());

    line(14, 210, 385, 210, palette::subdued());
    for (int x : {14, 200, 385}) line(x, 207, x, 210, palette::subdued());
    char end[16]; std::snprintf(end, sizeof end, "%d.%03d s", f.loop_ms / 1000, f.loop_ms % 1000);
    m::footer(400, "0", end);
}

inline void rot_draw_bottom(const RotFrame& f,const Artwork& a,const Resource& resource) {
    m::paper(320);
    if (!f.valid) return;
    (void)a;
    module_heading::header(UnitKind::Rot, m::spot());
    m::header_text(76, 250, f.source, palette::ink());
    right_text(305, m::kHeaderTextY, "setup", palette::ink());
    word(14, 40, "rot", f.status==RotFrame::Ready && f.mode == RotFrame::Rotting);
    word(146, 40, "heal", f.status==RotFrame::Ready && f.mode == RotFrame::Healing);
    word(305 - text_w("hold"), 40, "hold", f.status==RotFrame::Ready && f.mode == RotFrame::Holding);

    const bool art=resource.draw_bottom_art(f);
    if(shows_art(f)&&!art) text(14,60,"art unavailable",palette::ghost());
    else if(!shows_art(f)) m::caption(14,60,292,f.detail[0]?f.detail:status_text(f),palette::ghost());
    const int cy=76,half=13;
    const int loop0=std::clamp(f.loop_x0,0,292),loop1=std::clamp(f.loop_x1,loop0,292);
    if(art&&loop1>loop0)
    for (int gx : {14 + loop0, 14 + loop1 - 1}) {   // loop grips (not sliders)
        line(gx, cy - half - 3, gx, cy + half + 3, palette::ink());
        const int foot = gx == 14 + loop0 ? 1 : -1;
        line(gx, cy - half - 3, gx + 2 * foot, cy - half - 3, palette::ink());
        line(gx, cy + half + 3, gx + 2 * foot, cy + half + 3, palette::ink());
    }
    if (art&&moving(f)&&f.strip_head>=0&&f.strip_head<292) line(14 + f.strip_head, cy - half - 1, 14 + f.strip_head, cy + half + 1, palette::playhead());

    knob_grid(f);
    m::footer(320, f.window_pending?"window pending":"l mod", "b back");
}

template<class Pen> inline void cache_word(const Pen& pen,int x,int y,const char* s,bool active){
    pen.text(x,y,s,active?palette::spot():palette::ink());if(active)pen.rect(x,y+12,text_w(s)-1,1,palette::spot());
}
template<class Pen> inline void bottom_content(const Pen& pen,const RotFrame& f,const Artwork& a,bool art_ready=true){
    char source[40];caption_text(source,sizeof source,f.source,174);pen.text(76,m::kHeaderTextY,source,palette::ink());
    pen.right(305, m::kHeaderTextY, "setup", palette::ink());
    cache_word(pen,14, 40, "rot", f.status==RotFrame::Ready && f.mode == RotFrame::Rotting);
    cache_word(pen,146, 40, "heal", f.status==RotFrame::Ready && f.mode == RotFrame::Healing);
    cache_word(pen,305 - text_w("hold"), 40, "hold", f.status==RotFrame::Ready && f.mode == RotFrame::Holding);

    const bool art=shows_art(f)&&art_ready;
    if(art)material_source(f,a,pen);
    else if(shows_art(f)&&!art_ready)pen.text(14,60,"art unavailable",palette::ghost());
    else {char detail[52];caption_text(detail,sizeof detail,f.detail[0]?f.detail:status_text(f),292);pen.text(14,60,detail,palette::ghost());}
    const int cy=76,half=13;
    const int loop0=std::clamp(f.loop_x0,0,292),loop1=std::clamp(f.loop_x1,loop0,292);
    if(art&&loop1>loop0)
    for (int gx : {14 + loop0, 14 + loop1 - 1}) {   // loop grips (not sliders)
        pen.line(gx, cy - half - 3, gx, cy + half + 3, palette::ink());
        const int foot = gx == 14 + loop0 ? 1 : -1;
        pen.line(gx, cy - half - 3, gx + 2 * foot, cy - half - 3, palette::ink());
        pen.line(gx, cy + half + 3, gx + 2 * foot, cy + half + 3, palette::ink());
    }
    if (art&&moving(f)&&f.strip_head>=0&&f.strip_head<292) pen.line(14 + f.strip_head, cy - half - 1, 14 + f.strip_head, cy + half + 1, palette::playhead());

    knob_grid(pen,f);
    pen.text(module_frame_layout::kLeft,module_footer_layout::kTextY,f.window_pending?"window pending":"l mod",palette::ghost());pen.right(m::right_edge(320),module_footer_layout::kTextY,"b back",palette::ghost());
}
}
