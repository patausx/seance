#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_param_lens.h"
#include "ui_theme.h"
#include "editor_style.h"
#include "module_heading.h"
#include "filter_response_model.h"
#include "filter_interaction.h"
#include "filter_type_data.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace model=filter_response_model;
namespace interaction=filter_interaction;
namespace type=filter_type;
using C=FilterUnit;
using T=trackr::dsp::FilterType;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,bypass=false,mute=false,lock=false,lock_full=false,lock_invalid=false,lens=false;
    bool different=false,live_changed=false,limited=false,below_axis=false;
    int8_t chain=0,slot=0,selected=0;
    T type=T::LPF;
    q15 edit[4]={},live[4]={};
    uint8_t edit_y[model::POINTS]={},live_y[model::POINTS]={};
    uint8_t edit_top_y[model::POINTS]={},live_top_y[model::POINTS]={};
    int16_t edit_x[2]={},live_x[2]={};
    uint8_t edit_clip=0,live_clip=0;
    char cutoff[12]={},live_cutoff[12]={},number[12]={};
} frame;
static_assert(sizeof(Frame)<=640 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free filter frame; never reads audio in draw");
int selected=C::P_CUTOFF,drag_param=-1,origin_x=0,origin_y=0,last_axis=0;
int origin_chain=-1,origin_slot=-1,origin_step=-1,origin_lock_chain=-1,origin_bank=-1;
bool origin_locked=false;
View origin_view=View::Chains;
uintptr_t origin_unit=0;
Engine* origin_engine=nullptr;
q15 origin_value=0;
constexpr Drag DRAGS[4]={Drag::FilterCutoff,Drag::FilterResonance,Drag::FilterDrive,Drag::FilterMorph};
constexpr const char* NAMES[4]={"cutoff","resonance","drive","morph"};
constexpr const char* MODES[5]={"low","high","band","notch","off"};
constexpr const char* MODE_LABELS[5]={"low pass","high pass","band pass","notch","off"};
int clamp(int v) { return std::max(0,std::min(32767,v)); }
int percent(q15 v) {return (clamp(v)*100+16383)/32767;}
bool capture_context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
// The module stays visible beneath permanent MOD; only its controls are blocked.
bool context() { return !param_lens_is_open() && capture_context(); }
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
bool owns(const C& u) {
    return origin_engine==g_eng && origin_unit==uintptr_t(&u) &&
        origin_chain==g_edit_chain && origin_slot==g_edit_slot && origin_view==g_view &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u &&
        origin_bank==g_eng->active_pattern && origin_locked==g_seq_lock_armed &&
        (!origin_locked || (lock_context() && origin_step==g_seq_lock_step && origin_lock_chain==g_seq_lock_chain));
}
q15 edit_value(const C& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
u32 color(int p) { return frame.selected==p?m::spot():m::ghost(); }
bool dry() {return frame.bypass || frame.type==T::Off;}
u32 curve_color() {return frame.mute || dry()?m::ghost():m::spot();}
void right(int x,int y,const char* text,u32 c) {ui::text(x-ui::text_w(text),y,text,c);}
void center(int x,int y,const char* text,u32 c) {ui::text(x-ui::text_w(text)/2,y,text,c);}
void marker(int x,int y,u32 c,bool hollow=false) {
    if(hollow) {
        ui::line(x,y-3,x+2,y,c);ui::line(x+2,y,x,y+3,c);
        ui::line(x,y+3,x-2,y,c);ui::line(x-2,y,x,y-3,c);
    } else {
        ui::rect(x,y-3,1,7,c);ui::rect(x-1,y-1,3,3,c);ui::rect(x-2,y,5,1,c);
    }
}
void mask(int x,int baseline,unsigned offset,int w,int h,int top,u32 c) {
    const int stride=(w+7)/8;
    for(int y=0;y<h;++y) {
        int run=-1;
        for(int col=0;col<=w;++col) {
            const bool on=col<w && (type::bitmap[offset+y*stride+col/8]&(0x80u>>(col&7)));
            if(on && run<0)run=col;
            if(!on && run>=0){ui::rect(x+run,baseline+top+y,col-run,1,c);run=-1;}
        }
    }
}
void word(int x,int baseline,const char* text,int size,u32 c) {
    for(const auto& label:type::labels)if(label.size==size && !std::strcmp(label.text,text)) {
        mask(x,baseline,label.offset,label.width,label.height,label.top,c);return;
    }
}
const type::Glyph* glyph(char c) {
    for(const auto& g:type::glyphs)if(g.character==c)return &g;
    return nullptr;
}
int number(int x,int baseline,const char* text,u32 c) {
    const auto* first=glyph(text[0]);
    if(!first)return x;
    int cursor=x-first->left,end=x;
    for(const char* p=text;*p;++p) {
        const auto* g=glyph(*p);if(!g)continue;
        mask(cursor+g->left,baseline,g->offset,g->width,g->height,g->top,c);
        end=cursor+g->left+g->width;cursor+=g->advance;
    }
    return end;
}
void icon(T type,int cx,u32 c) {
    // Original five vertices, exactly twice their geometry, a thin native pen.
    static const int8_t shapes[5][5]={{1,1,2,8,9},{9,8,2,1,1},{9,5,1,5,9},{1,2,9,2,1},{5,5,5,5,5}};
    for(int i=1;i<5;++i)ui::line(cx-12+(i-1)*6,31+2*shapes[int(type)][i-1],
                               cx-12+i*6,31+2*shapes[int(type)][i],c);
}
void rail(int p,int x,int width,int y,bool vertical=false) {
    const int base=(clamp(frame.edit[p])*width+16383)/32767;
    const int live=(clamp(frame.live[p])*width+16383)/32767;
    const bool linked=(frame.mod_links&(1u<<p)) || frame.edit[p]!=frame.live[p];
    const module_fader::State state{linked,false,frame.mute || dry()};
    if(vertical)module_fader::vertical(x,y-width,y,y-base,y-live,state);
    else module_fader::horizontal(x,x+width,y,x+base,x+live,state);
}
uint8_t response_y(float db,bool top) {
    const int y=top?interaction::upper_y:interaction::lower_y;
    const int h=top?interaction::upper_height:interaction::lower_height;
    return uint8_t(y+int((12.0f-std::max(-36.0f,std::min(12.0f,db)))*h/48.0f+0.5f));
}
int frequency_x(float hz,bool top) {
    hz=std::max(model::MIN_HZ,std::min(model::MAX_HZ,hz));
    const int width=top?interaction::upper_width:interaction::lower_width;
    return interaction::lower_x+int(std::log(hz/model::MIN_HZ)/std::log(model::MAX_HZ/model::MIN_HZ)*width+0.5f);
}
void hz_text(char* out,size_t size,q15 raw) {
    const float hz=model::nominal_hz(raw);
    if(hz<10.f)std::snprintf(out,size,"%.1f",double(hz));
    else std::snprintf(out,size,"%u",unsigned(hz+0.5f));
}
void capture_curves() {
    // Same 117-frequency transfer model and two-entry scalar cache as before.
    // Both screen projections derive from dB, not a scaled low-resolution raster.
    // All trig/model/projection/formatting happens AFTER the short audio guard.
    struct Cache {
        bool valid=false,bypass=false;
        T type=T::LPF;
        q15 values[2][3]={};
        uint8_t y[2][2][model::POINTS]={},clip[2]={};
    };
    static Cache cache;
    static model::Frequency frequencies[model::POINTS];
    static bool initialized=false;
    if(!initialized) {
        for(int i=0;i<model::POINTS;++i)frequencies[i]=model::at_hz(
            model::MIN_HZ*std::pow(model::MAX_HZ/model::MIN_HZ,float(i)/float(model::POINTS-1)));
        initialized=true;
    }
    const bool same_type=cache.valid && cache.type==frame.type && cache.bypass==frame.bypass;
    for(int curve=0;curve<2;++curve) {
        const q15* src=curve?frame.live:frame.edit;
        const q15 key[3]={q15(model::coefficient(src[0])),q15(std::min(31743,clamp(src[1]))),src[3]};
        if(!(same_type && std::equal(key,key+3,cache.values[curve]))) {
            std::copy(key,key+3,cache.values[curve]);cache.clip[curve]=0;
            for(int i=0;i<model::POINTS;++i) {
                const float db=model::magnitude_db(src[0],src[1],src[3],frame.bypass?T::Off:frame.type,frequencies[i]);
                if(db>12.0f)cache.clip[curve]|=1;
                if(db<-36.0f)cache.clip[curve]|=2;
                cache.y[curve][0][i]=response_y(db,false);
                cache.y[curve][1][i]=response_y(db,true);
            }
        }
        std::copy(cache.y[curve][0],cache.y[curve][0]+model::POINTS,curve?frame.live_y:frame.edit_y);
        std::copy(cache.y[curve][1],cache.y[curve][1]+model::POINTS,curve?frame.live_top_y:frame.edit_top_y);
        (curve?frame.live_clip:frame.edit_clip)=cache.clip[curve];
    }
    cache.valid=true;cache.type=frame.type;cache.bypass=frame.bypass;
    frame.different=!std::equal(frame.edit_y,frame.edit_y+model::POINTS,frame.live_y) ||
        !std::equal(frame.edit_top_y,frame.edit_top_y+model::POINTS,frame.live_top_y);
    frame.live_changed=!std::equal(frame.edit,frame.edit+4,frame.live);
    frame.limited=frame.edit[0]>model::F_MAX || frame.live[0]>model::F_MAX;
    frame.below_axis=model::nominal_hz(frame.edit[0])<model::MIN_HZ;
    for(int top=0;top<2;++top) {
        frame.edit_x[top]=frequency_x(model::nominal_hz(frame.edit[0]),top);
        frame.live_x[top]=frequency_x(model::nominal_hz(frame.live[0]),top);
    }
    hz_text(frame.cutoff,sizeof frame.cutoff,frame.edit[0]);
    hz_text(frame.live_cutoff,sizeof frame.live_cutoff,frame.live[0]);
    if(frame.selected==0)std::snprintf(frame.number,sizeof frame.number,"%s",frame.cutoff);
    else std::snprintf(frame.number,sizeof frame.number,"%d",percent(frame.edit[frame.selected]));
}
void response(const uint8_t* points,int x,int width,u32 c,bool dashed) {
    for(int i=1;i<model::POINTS;++i) {
        if(dashed && (i&3)>1)continue;
        ui::line(x+((i-1)*width+58)/116,points[i-1],x+(i*width+58)/116,points[i],c);
    }
}
void plot(bool top) {
    const int x=interaction::lower_x,y=top?interaction::upper_y:interaction::lower_y;
    const int w=top?interaction::upper_width:interaction::lower_width;
    const int h=top?interaction::upper_height:interaction::lower_height;
    const int zero=y+(h+2)/4;
    const auto* edit=top?frame.edit_top_y:frame.edit_y;
    const auto* live=top?frame.live_top_y:frame.live_y;
    if(top && !frame.mute && !dry()) {
        // The translucent wash follows the same full-screen material underneath;
        // no flat old-paper rectangle, new response math, or geometry changes.
        const u32 shade=(m::spot() & 0x00ffffffu) | (16u << 24);
        // Integer interpolation only: bounded quiet fill, no draw-time response math.
        for(int col=0;col<=w;++col) {
            const int scaled=col*116,index=scaled/w,remainder=scaled%w;
            const int yy=index==116?live[index]:live[index]+(int(live[index+1])-live[index])*remainder/w;
            ui::rect(x+col,yy,1,y+h-yy+1,shade);
        }
    }
    ui::line(x,zero,x+w,zero,palette::shadow());
    ui::line(x,y+h,x+w,y+h,palette::shadow());
    const int grid[2][4]={{32,87,165,260},{32,116,236,380}};
    const char* labels[4]={"20","100","1k","16k"};
    for(int i=0;i<4;++i) {
        const int xx=grid[top?1:0][i];
        for(int yy=y;yy<y+h;yy+=7)ui::rect(xx,yy,1,1,palette::shadow());
        if(i==3)right(xx,y+h+9,labels[i],m::ghost());
        else center(xx,y+h+9,labels[i],m::ghost());
    }
    right(x-6,y-3,"+12",m::ghost());right(x-6,zero-3,"0",m::ghost());right(x-6,y+h-5,"-36",m::ghost());
    if(frame.different)response(edit,x,w,m::ghost(),true);
    response(live,x,w,curve_color(),false);
    if(!dry()) {
        const int ex=frame.edit_x[top?1:0],lx=frame.live_x[top?1:0];
        const bool show_live=(frame.mod_links&(1u<<C::P_CUTOFF)) || frame.edit[0]!=frame.live[0];
        const u32 ec=frame.mute?m::ghost():m::spot();
        for(int yy=y-3;yy<=y+h;yy+=5)ui::line(ex,yy,ex,std::min(y+h,yy+1),show_live?m::ghost():ec);
        marker(ex,y-7,ec,true);
        if(show_live) {
            if(lx!=ex)marker(lx,y-7,curve_color());
            else ui::rect(lx,y-8,1,3,curve_color());
        }
    }
    if((frame.edit_clip|frame.live_clip)&1)ui::text(x+w-8,y+1,"^",m::ghost());
    if((frame.edit_clip|frame.live_clip)&2)ui::text(x+w-8,y+h-16,"v",m::ghost());
}
} // namespace

int filter_focus_selected() { return selected; }
void filter_focus_select(int p) {
    if(p<0 || p>=4)return;
    if(p!=selected)filter_focus_release();
    selected=p;
}
void filter_focus_release() { drag_param=-1;origin_unit=0;origin_engine=nullptr; }
bool filter_focus_touch_header(C&,int,int) { return false; }
void filter_focus_draw_header(const C&) {}

bool filter_focus_touch_body(C& u,int tx,int ty) {
    filter_focus_release();
    if(!context())return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(ty<24) {
        if(!g_seq_lock_armed && interaction::bypass.contains(tx,ty)) {u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    for(int i=0;i<5;++i) {
        if(!interaction::mode(i).contains(tx,ty))continue;
        if(!g_seq_lock_armed && u.type!=T(i)) {u.type=T(i);u.reset();mark_project_mutated();}
        return true;
    }
    int p=-1;bool name=false;
    if(interaction::cutoff.contains(tx,ty)){p=C::P_CUTOFF;name=interaction::cutoff_name.contains(tx,ty);}
    else if(interaction::resonance.contains(tx,ty)){p=C::P_RES;name=interaction::resonance_name.contains(tx,ty);}
    else if(interaction::drive.contains(tx,ty)){p=C::P_DRIVE;name=interaction::drive_name.contains(tx,ty);}
    else if(interaction::morph.contains(tx,ty)){p=C::P_MORPH;name=interaction::morph_name.contains(tx,ty);}
    if(p>=0) {
        selected=p;
        if(g_seq_lock_armed && !lock_context())return true;
        if(lock_context() && name) {
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
            return true;
        }
        drag_param=p;origin_x=tx;origin_y=ty;last_axis=p==C::P_RES?ty:tx;origin_value=edit_value(u,p);
        origin_unit=uintptr_t(&u);origin_engine=g_eng;origin_chain=g_edit_chain;origin_slot=g_edit_slot;
        origin_view=g_view;origin_bank=g_eng->active_pattern;
        origin_locked=g_seq_lock_armed;origin_step=g_seq_lock_step;origin_lock_chain=g_seq_lock_chain;
        g_drag=DRAGS[p];
    }
    return true; // inert gaps never leak into hidden legacy rows
}

void filter_focus_drag(C& u,int tx,int ty) {
    if(drag_param<0)return;
    if(!context() || g_drag!=DRAGS[drag_param]){filter_focus_release();return;}
    const int axis=drag_param==C::P_RES?ty:tx;
    const int delta=drag_param==C::P_RES?origin_y-ty:tx-origin_x;
    // UI log/exp work is outside the guard; capture remains unchanged until a
    // different axis report. R stays exclusively the existing shared MOD fine key.
    const q15 next=axis==last_axis?origin_value:drag_param==C::P_CUTOFF?
        interaction::cutoff_drag(origin_value,delta):
        q15(clamp(origin_value+delta*32767/(drag_param==C::P_RES?70:126)));
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){filter_focus_release();return;}
    if(axis==last_axis)return; // external edits survive identical-axis HID reports
    last_axis=axis;
    if(next==edit_value(u,drag_param))return;
    if(g_seq_lock_armed)seq_record_lock(g_edit_slot,drag_param,next);
    else {u.params[drag_param].base=next;mark_project_mutated();}
}

void filter_focus_capture() {
    frame={};
    if(!capture_context()){filter_focus_release();return;}
    if(drag_param>=0 && (!context() || g_drag!=DRAGS[drag_param]))filter_focus_release();
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];
        const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Filter){filter_focus_release();return;}
        const auto& u=*static_cast<const C*>(unit);
        if(drag_param>=0 && !owns(u))filter_focus_release();
        frame.valid=true;frame.type=unsigned(u.type)<=unsigned(T::Off)?u.type:T::LPF;
        frame.bypass=u.bypass;frame.mute=chain.mute;frame.lens=param_lens_is_open();
        frame.lock=lock_context();frame.lock_full=frame.lock && g_seq_lock_flash>0;
        frame.lock_invalid=g_seq_lock_armed && !frame.lock;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.mod_links=focus_mod_links(u);
        for(int p=0;p<4;++p) {frame.edit[p]=edit_value(u,p);frame.live[p]=u.params[p].effective;}
        frame.live[0]=q15(u.visual_cutoff());frame.live[1]=q15(u.visual_resonance());
    }
    capture_curves();
}

void filter_focus_draw(const C&) {
    m::paper(320);
    if(!frame.valid)return;
    module_heading::header(UnitKind::Filter,m::spot());
    ui::text(250,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());
    for(int i=0;i<5;++i) {
        const int cx=36+61*i;const u32 c=int(frame.type)==i?m::spot():m::ghost();
        icon(T(i),cx,c);if(int(frame.type)==i)marker(cx,55,c);
    }
    char text[32];
    ui::text(10,65,"cutoff",color(0));
    std::snprintf(text,sizeof text,"%shz",frame.cutoff);ui::text(105,65,text,m::ink());
    center(292,65,"res",color(1));
    std::snprintf(text,sizeof text,"%d%%",percent(frame.edit[1]));center(292,78,text,m::ghost());
    plot(false);rail(1,295,63,157,true);
    for(int p=2;p<4;++p) {
        const int x=p==2?14:172;
        ui::text(x,188,NAMES[p],color(p));
        std::snprintf(text,sizeof text,"%d%%",percent(frame.edit[p]));right(x+125,188,text,m::ink());
        rail(p,x+4,119,211);
    }
    if(frame.lock || frame.lock_invalid) {
        ui::text(m::kLeft,m::kTextY,frame.lock_invalid?"lock unavailable":frame.lock_full?"lock full":"p-lock / name: remove",m::ghost());
        right(m::right_edge(320),m::kTextY,"b back",m::ghost());
    } else {ui::text(m::kLeft,m::kTextY,"b back",m::ghost());right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());}
}

bool filter_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);
    module_heading::header(UnitKind::Filter,m::spot());
    word(116,32,MODE_LABELS[int(frame.type)],22,m::ghost());
    char text[64];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(20,46,NAMES[frame.selected],m::ghost());
    const int end=number(20,92,frame.number,m::ink());
    ui::text(end+7,82,frame.selected==0?"hz":"%",m::ghost());
    const char* meaning=frame.selected==0?"nominal frequency":frame.selected==1?"resonance":
        frame.selected==2?"pre-clip drive":"pass / reject morph";
    right(382,48,meaning,m::ghost());
    std::snprintf(text,sizeof text,"res %d%% / drive %d%%",percent(frame.edit[1]),percent(frame.edit[2]));right(382,64,text,m::ghost());
    const bool linked=frame.mod_links || frame.live_changed;
    if(frame.lock_invalid)std::snprintf(text,sizeof text,"lock unavailable");
    else if(frame.lock_full)std::snprintf(text,sizeof text,"lock full / base unchanged");
    else if(frame.bypass)std::snprintf(text,sizeof text,"bypassed / dry");
    else if(frame.mute)std::snprintf(text,sizeof text,"muted / model only");
    else if(frame.type==T::Off)std::snprintf(text,sizeof text,"off / exact dry");
    else if(frame.lens)std::snprintf(text,sizeof text,"mod / stored edit + live");
    else if(frame.lock)std::snprintf(text,sizeof text,"p-lock edit / live separate");
    else if(linked && frame.selected!=0)std::snprintf(text,sizeof text,"live %s %d%%",NAMES[frame.selected],percent(frame.live[frame.selected]));
    else std::snprintf(text,sizeof text,"morph %d%%",percent(frame.edit[3]));
    right(382,80,text,linked && !dry() && !frame.mute?m::spot():m::ghost());
    plot(true);
    if(frame.below_axis || frame.limited)
        std::snprintf(text,sizeof text,"%s %shz / raw%d",frame.lock?"lock":"edit",frame.cutoff,int(frame.edit[0]));
    else std::snprintf(text,sizeof text,"%s %shz",frame.lock?"lock":"edit",frame.cutoff);
    ui::text(20,214,text,m::ghost());
    if(linked && frame.limited)std::snprintf(text,sizeof text,"live %shz / 85%% ceiling",frame.live_cutoff);
    else if(linked && frame.below_axis)std::snprintf(text,sizeof text,"live %shz / <20hz edge",frame.live_cutoff);
    else if(frame.limited)std::snprintf(text,sizeof text,"85%% coefficient ceiling");
    else if(frame.below_axis)std::snprintf(text,sizeof text,"<20hz at edge");
    else if(linked)std::snprintf(text,sizeof text,"live %shz",frame.live_cutoff);
    else std::snprintf(text,sizeof text,"cutoff / %s",MODES[int(frame.type)]);
    right(382,214,text,linked && !dry() && !frame.mute?m::spot():m::ghost());
    const char* caption=frame.bypass?"bypassed / exact dry":frame.type==T::Off?"off / dry; drive + morph inactive":
        frame.selected==2?"drive before clip / linear curve unchanged":
        frame.selected==3?"middle = half post-drive input / not spectrum":
        "post-drive response / not spectrum";
    ui::text(m::kLeft,m::kTextY,caption,m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
