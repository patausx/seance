#include "ui_start_menu.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "menu_style.h"
#include "start_menu_labels_data.h"
#include "cabinet_font_data.h"
#include "start_menu_motion.h"
#include "startup_reveal.h"
#include "startup_input_tail.h"
#include "wordmark_morph.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/font.h"
#include <cstdio>
#include <cstring>

namespace seance::platform::ui3ds {
namespace {
enum class Page { Home, Browser, Theme };
enum class Panel { None=-1, Library=0, Settings=1 };
enum HomeRow { New=0, Library=1, Settings=2, Recent0=3,
               All=Recent0+kStartRecentCount, Sound, Startup, Palette, HomeRows };
constexpr int kSettingsCount=3;
constexpr int kThemeRole=theme_config::kCount, kThemeSwatch=kThemeRole+3;
constexpr int kThemeCancel=kThemeSwatch+theme_config::kSwatchCount, kThemeSave=kThemeCancel+1;
constexpr int kBrowserRows=6, kBrowserBack=PROJECT_SLOTS, kBrowserPrev=PROJECT_SLOTS+1, kBrowserNext=PROJECT_SLOTS+2;
constexpr u32 kScreenshot=1u<<14, kButtons=~(u32(KEY_TOUCH)|kScreenshot);
struct Box { int x,y,w,h; };
Box theme_box(int row) {
    if(row<kThemeRole)return {22+(row%2)*142,54+(row/2)*21,140,21};
    if(row<kThemeSwatch)return {22+(row-kThemeRole)*92,122,92,24};
    if(row<kThemeCancel) { const int i=row-kThemeSwatch; return {22+(i%8)*34,148+(i/8)*17,34,17}; }
    return {22+(row-kThemeCancel)*148,201,126,24};
}
struct State {
    StartMenuData data;
    Page page=Page::Home;
    Panel panel=Panel::None;
    int selected=Library, browser_page=0, browser_count=0;
    int browser_slots[PROJECT_SLOTS]{};
    cabinet_motion::Reveal reveal;
    cabinet_ink::Clock ink_clock;
    int focus_age=cabinet_ink::kFocusMs, focus_from[3]{}, focus_to=-1;
    int modal=0, modal_from=0, modal_age=200;
    bool modal_open=false;
    theme_config::Settings theme_draft;
    int theme_role=0;
    u32 theme_inverse[3]{};
    bool theme_warning=false;
    u32 reveal_ms=0, logo_ms=0;
    StartupInputTail input_tail;
    bool quarantine=true, touch_held=false, buttons_held=false, touch_cancelled=false;
    int touch_row=-1;
    char error[48]{};
};
State g;
int ease(int t,int d) { return cabinet_motion::ease(t,d); }
u32 ink() { return col_text(); }
u32 spot() { return col_on(); }
u32 ghost() { return palette::menu_ghost(); }
u32 opacity(u32 c,int a) { return (c&0xffffffu)|(u32(a<0?0:a>255?255:a)<<24); }
int owner() { return g.panel==Panel::Settings?Settings:Library; }
bool right_ready() { return g.panel!=Panel::None&&g.reveal.ready(); }
bool valid(int s) { return project_slot_valid(s)&&g.data.projects[s].exists&&g.data.projects[s].valid; }
void clean(char* p,int n) {
    p[n-1]=0;
    for(int i=0;p[i];++i)if(static_cast<unsigned char>(p[i])<32||static_cast<unsigned char>(p[i])>126)p[i]='?';
}
void fitted(char* out,const char* in,int max) {
    const int len=int(std::strlen(in)), n=len<max?len:max;
    std::memcpy(out,in,n);
    if(len>n&&n>=3)out[n-1]=out[n-2]=out[n-3]='.';
    out[n]=0;
}
void cancel_touch() { g.touch_cancelled=true;g.touch_row=-1; }
void meaningful() { g.error[0]=0; }
void transition_input() { cancel_touch();g.quarantine=true; }
int focus(int row) {
    const int e=ease(g.focus_age,cabinet_ink::kFocusMs);
    return (g.focus_from[row]*(255-e)+(g.focus_to==row?255:0)*e+127)/255;
}
void focus_to(int row) {
    if(row==g.focus_to)return;
    for(int i=0;i<3;++i)g.focus_from[i]=focus(i);
    g.focus_to=row;g.focus_age=0;
}
int browser_pages() { return g.browser_count?(g.browser_count+kBrowserRows-1)/kBrowserRows:1; }
int browser_index(int slot) {
    for(int i=0;i<g.browser_count;++i)if(g.browser_slots[i]==slot)return i;
    return -1;
}
void select(int row) {
    g.selected=row;
    if(g.page==Page::Home)focus_to(row<3?row:owner());
    if(g.page==Page::Browser) {
        const int i=browser_index(row);if(i>=0)g.browser_page=i/kBrowserRows;
    }
}
void panel_to(Panel next) {
    if(next!=Panel::None)g.ink_clock.pulse(next==Panel::Library?Library:Settings);
    g.panel=next;g.reveal.retarget(int(next));
    select(next==Panel::Settings?Settings:Library);
    transition_input();
}
void home_from_browser() {
    g.page=Page::Home;g.panel=Panel::Library;g.reveal.settle(0);
    select(All);transition_input();
}
int home_rows(int* out,bool left) {
    if(left) { out[0]=New;out[1]=Library;out[2]=Settings;return 3; }
    int n=0;
    if(g.panel==Panel::Library) {
        for(int i=0;i<g.data.recent_count;++i)out[n++]=Recent0+i;
        out[n++]=All;
    } else if(g.panel==Panel::Settings) {
        out[n++]=Sound;out[n++]=Startup;out[n++]=Palette;
    }
    return n;
}
void normalize_selection() {
    if(g.page==Page::Home) {
        int list[HomeRows];const int n=home_rows(list,g.selected<3);
        for(int i=0;i<n;++i)if(list[i]==g.selected)return;
        select(owner());
    } else if(g.page==Page::Browser) {
        if(g.browser_page>=browser_pages())g.browser_page=browser_pages()-1;
        if(browser_index(g.selected)>=0)select(g.selected);
        else if(g.selected!=kBrowserBack)select(g.browser_count?g.browser_slots[0]:kBrowserBack);
    }
}
void move(int direction) {
    int list[PROJECT_SLOTS+1],n=0;
    if(g.page==Page::Home)n=home_rows(list,g.selected<3);
    else { for(int i=0;i<g.browser_count;++i)list[n++]=g.browser_slots[i];list[n++]=kBrowserBack; }
    if(!n)return;
    for(int i=0;i<n;++i)if(list[i]==g.selected) { select(list[(i+n+direction)%n]);meaningful();return; }
    select(list[0]);meaningful();
}
void page_browser(int direction) {
    const int next=g.browser_page+direction;
    if(next<0||next>=browser_pages())return;
    g.browser_page=next;
    g.selected=next*kBrowserRows<g.browser_count?g.browser_slots[next*kBrowserRows]:kBrowserBack;
    meaningful();cancel_touch();
}
void move_theme(u32 down) {
    int next=g.selected;
    if(down&(KEY_LEFT|KEY_RIGHT))next=(next+kThemeSave+1+((down&KEY_LEFT)?-1:1))%(kThemeSave+1);
    else if(down&KEY_UP) {
        if(next<2)next=next==0?kThemeCancel:kThemeSave;
        else if(next<4)next-=2;
        else if(next==4)next=2;
        else if(next<kThemeSwatch)next=next==kThemeSwatch-1?3:4;
        else if(next<kThemeSwatch+8)next=kThemeRole+(next-kThemeSwatch)*3/8;
        else if(next<kThemeCancel)next-=8;
        else next=kThemeSwatch+16+(next==kThemeCancel?0:7);
    } else {
        if(next<2)next+=2;
        else if(next<4)next=next==2?4:kThemeSwatch-1;
        else if(next==4)next=kThemeRole;
        else if(next<kThemeSwatch)next=kThemeSwatch+(next-kThemeRole)*3;
        else if(next<kThemeSwatch+16)next+=8;
        else if(next<kThemeCancel)next=(next-kThemeSwatch)%8<4?kThemeCancel:kThemeSave;
        else next=next==kThemeCancel?0:1;
    }
    select(next);meaningful();
}
bool in(int x,int y,Box b) { return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h; }
int row_at(int x,int y) {
    if(g.page==Page::Theme) {
        if(g.modal<255)return -1;
        for(int i=0;i<=kThemeSave;++i)if(in(x,y,theme_box(i)))return i;
    } else if(g.page==Page::Home) {
        for(int i=0;i<3;++i)if(in(x,y,{12,26+i*64,120,62}))return i;
        if(!right_ready())return -1; // outgoing text must not operate incoming controls
        if(g.panel==Panel::Library) {
            for(int i=0;i<g.data.recent_count;++i)if(in(x,y,{148,30+i*36,162,36}))return Recent0+i;
            if(in(x,y,{148,214,162,26}))return All;
        } else if(g.panel==Panel::Settings) {
            for(int i=0;i<kSettingsCount;++i)if(in(x,y,{148,26+i*44,162,44}))return Sound+i;
        }
    } else {
        if(in(x,y,{208,38,28,26}))return kBrowserPrev;
        if(in(x,y,{280,38,28,26}))return kBrowserNext;
        for(int i=0;i<kBrowserRows;++i) {
            const int idx=g.browser_page*kBrowserRows+i;
            if(idx<g.browser_count&&in(x,y,{12,66+i*22,296,22}))return g.browser_slots[idx];
        }
        if(in(x,y,{12,204,296,24}))return kBrowserBack;
    }
    return -1;
}
void theme_preview_update() {
    const auto c=theme_config::colors(g.theme_draft);
    const u32 roles[]={c.paper,c.ink,c.accent};
    for(int i=0;i<3;++i)g.theme_inverse[i]=theme_config::inverse(roles[i]);
    g.theme_warning=theme_config::low_contrast(c);
}
void theme_leave() {
    g.page=Page::Home;g.panel=Panel::Settings;g.reveal.settle(1);
    g.modal_from=g.modal;g.modal_age=0;g.modal_open=false;
    select(Palette);transition_input();
}
StartMenuResult activate_theme(int row) {
    if(row<kThemeRole)g.theme_draft.id=theme_config::Id(row);
    else if(row<kThemeSwatch)g.theme_role=row-kThemeRole;
    else if(row<kThemeCancel) {
        if(g.theme_draft.id!=theme_config::Id::Custom)g.theme_draft.custom=theme_config::colors(g.theme_draft);
        g.theme_draft.id=theme_config::Id::Custom;
        const u32 c=theme_config::swatches[row-kThemeSwatch];
        if(g.theme_role==0)g.theme_draft.custom.paper=c;
        else if(g.theme_role==1)g.theme_draft.custom.ink=c;
        else g.theme_draft.custom.accent=c;
    } else {
        const bool changed=row==kThemeSave&&g.theme_draft!=g.data.theme;
        if(changed)g.data.theme=g.theme_draft;
        theme_leave();
        if(changed)return {StartMenuAction::PreferencesChanged,-1};
    }
    if(g.page==Page::Theme)theme_preview_update();
    return {};
}
StartMenuResult activate(int row) {
    meaningful();
    if(g.page==Page::Theme)return activate_theme(row);
    if(g.page==Page::Browser) {
        if(row==kBrowserBack) { home_from_browser();return {}; }
        if(row==kBrowserPrev||row==kBrowserNext) { page_browser(row==kBrowserPrev?-1:1);return {}; }
        if(valid(row)) { select(row);return {StartMenuAction::LoadProject,row}; }
        start_menu_error("check file - cannot load");return {};
    }
    if(row>=Recent0&&!right_ready())return {};
    select(row);
    if(row==New)return {StartMenuAction::NewProject,-1};
    if(row==Library) { panel_to(g.panel==Panel::Library?Panel::None:Panel::Library);return {}; }
    if(row==Settings) {
        const bool close=g.panel==Panel::Settings;
        panel_to(close?Panel::None:Panel::Settings);if(close)select(Settings);return {};
    }
    if(g.panel==Panel::Library) {
        if(row>=Recent0&&row<Recent0+g.data.recent_count) {
            const int slot=g.data.recent_slots[row-Recent0];
            if(valid(slot))return {StartMenuAction::LoadProject,slot};
        } else if(row==All) {
            g.page=Page::Browser;
            select(browser_index(g.data.active_slot)>=0?g.data.active_slot:g.browser_count?g.browser_slots[0]:kBrowserBack);
            transition_input();
        }
    } else if(g.panel==Panel::Settings) {
        if(row==Palette) {
            g.theme_draft=g.data.theme;g.theme_role=0;theme_preview_update();
            g.page=Page::Theme;g.selected=int(g.theme_draft.id);
            g.modal_from=g.modal;g.modal_age=0;g.modal_open=true;
            g.reveal.settle(1);transition_input();return {};
        }
        if(row==Sound)g.data.menu_sound=!g.data.menu_sound;
        else if(row==Startup)g.data.show_menu=!g.data.show_menu;
        else return {};
        return {StartMenuAction::PreferencesChanged,-1};
    }
    return {};
}
void heading(int index,int x,int y,u32 c) {
    const auto& label=start_menu_labels::labels[index];const int stride=(label.width+7)/8;
    const auto* b=start_menu_labels::bitmap+label.offset;
    for(int row=0;row<label.height;) {
        int end=row+1;while(end<label.height&&!std::memcmp(b+row*stride,b+end*stride,stride))++end;
        int run=-1;
        for(int col=0;col<=label.width;++col) {
            const bool on=col<label.width&&(b[row*stride+col/8]&(1u<<(7-col%8)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){ui::rect(x+run,y+row,col-run,end-row,c);run=-1;}
        }
        row=end;
    }
}
// Exact vertical coalescing of equal-color native spans. Both row buffers are
// bounded by a single immutable glyph/heading, not the framebuffer. This changes
// primitive count only: every covered pixel and its single blend remain exact.
struct GlyphRows {
    struct Run { u32 color; uint8_t x,y,w,h; };
    Run previous[128],pending[128];
    int x,y,previous_count=0,count=0;
    GlyphRows(int xx,int yy):x(xx),y(yy) {}
    void add(int xx,int w,u32 color) {
        if(color>>24)pending[count++]={color,uint8_t(xx),0,uint8_t(w),1};
    }
    void emit(const Run& r) const { ui::rect(x+r.x,y+r.y,r.w,r.h,r.color); }
    void finish(int row) {
        bool used[128]{};int p=0;
        for(int i=0;i<count;++i) {
            auto& r=pending[i];r.y=uint8_t(row);
            while(p<previous_count&&previous[p].x<r.x)++p;
            if(p<previous_count&&previous[p].x==r.x&&previous[p].w==r.w&&previous[p].color==r.color) {
                r.y=previous[p].y;r.h=uint8_t(previous[p].h+1);used[p]=true;
            }
        }
        for(int i=0;i<previous_count;++i)if(!used[i])emit(previous[i]);
        std::memcpy(previous,pending,sizeof(Run)*count);previous_count=count;count=0;
    }
    void flush() const { for(int i=0;i<previous_count;++i)emit(previous[i]); }
};
constexpr bool bounded_ink_glyphs() {
    for(const auto& l:start_menu_labels::labels)if(l.width>128||l.height>254)return false;
    for(const auto& l:cabinet_font::glyphs)if(l.width>128||l.height>254)return false;
    return true;
}
static_assert(bounded_ink_glyphs(),"native masks fit bounded span buffers");
// Shade ONLY existing bitmap pixels; no glow, trail, displacement or new mask.
void ink_heading(int index,int x,int y,int f) {
    const auto& label=start_menu_labels::labels[index];const int stride=(label.width+7)/8;
    const auto* bits=start_menu_labels::bitmap+label.offset;
    const auto on=[&](int xx,int yy) {
        return xx>=0&&xx<label.width&&yy>=0&&yy<label.height&&
               (bits[yy*stride+xx/8]&(1u<<(7-xx%8)));
    };
    GlyphRows rows(x,y);
    for(int row=0;row<label.height;++row) {
        int run=-1;u32 previous=0;
        for(int col=0;col<=label.width;++col) {
            u32 color=0;
            if(on(col,row)) {
                const bool edge=!(on(col-1,row)&&on(col+1,row)&&on(col,row-1)&&on(col,row+1));
                const auto shade=cabinet_ink::heading(g.ink_clock,index,col,row,edge,f);
                color=opacity(theme_config::mix(ink(),spot(),shade.tint),shade.alpha);
            }
            if(run>=0&&color!=previous) { rows.add(run,col-run,previous);run=-1; }
            if(color&&run<0)run=col;
            previous=color;
        }
        rows.finish(row);
    }
    rows.flush();
}
u32 run_color(int p,int x,int y,int w,int h,u32 c,bool caption=false) {
    const int xx=x+w/2,yy=y+h/2;
    int a=g.reveal.sample(p,xx,yy,caption)*int(c>>24)/255;
    if(p>=0) {
        if(!caption)a=a*cabinet_ink::body_alpha(g.ink_clock,p,xx,yy)/255;
        // Stable 64-level coverage keeps gradual ink but merges neighboring
        // equal spans. At most2/255 alpha error; exact transparent/opaque ends.
        a=(a+2)/4*4;if(a>255)a=255;
    }
    return opacity(c,a);
}
void run_rect(int p,int x,int y,int w,int h,u32 c,bool caption=false) {
    c=run_color(p,x,y,w,h,c,caption);
    if(c>>24)ui::rect(x,y,w,h,c);
}
const cabinet_font::Glyph& letter(char c) {
    const unsigned ch=static_cast<unsigned char>(c);
    return cabinet_font::glyphs[(ch>=32&&ch<=126?ch:unsigned('?'))-32];
}
int title_width(const char* text) {
    int pos=0,right=0;
    for(int n=0;text[n]&&n<PROJECT_NAME_BYTES+3;++n) {
        const auto& a=letter(text[n]);
        const int end=(pos+32)/64+a.x+a.width;
        if(end>right)right=end;
        pos+=a.advance;
    }
    return right;
}
void title(int x,int baseline,const char* source,u32 c,int p=-1,int width=136) {
    char text[PROJECT_NAME_BYTES+4];fitted(text,source,PROJECT_NAME_BYTES-1);
    if(title_width(text)>width) {
        int n=int(std::strlen(text));
        if(n>PROJECT_NAME_BYTES-3)n=PROJECT_NAME_BYTES-3;
        do { --n;std::memcpy(text,source,n);std::memcpy(text+n,"...",4); } while(n>0&&title_width(text)>width);
    }
    int pos=0;
    for(int i=0;text[i];++i) {
        const auto& a=letter(text[i]);const int stride=(a.width+7)/8;
        const int xx=x+(pos+32)/64+a.x, yy=baseline+a.y;
        GlyphRows rows(xx,yy);
        for(int row=0;row<a.height;++row) {
            int run=-1;
            for(int col=0;col<=a.width;++col) {
                const bool on=col<a.width&&(cabinet_font::bits[a.offset+row*stride+col/8]&(1u<<(7-col%8)));
                if(on&&run<0)run=col;
                if(!on&&run>=0) {
                    if(p>=0)rows.add(run,col-run,run_color(p,xx+run,yy+row,col-run,1,c));
                    else run_rect(p,xx+run,yy+row,col-run,1,c);
                    run=-1;
                }
            }
            if(p>=0)rows.finish(row);
        }
        if(p>=0)rows.flush();
        pos+=a.advance;
    }
}
void small(int x,int y,const char* s,u32 c,int p=-1) {
    if(p<0||g.reveal.settled()) {
        if(p<0||g.reveal.sample(p,x,y,true))ui::text(x,y,s,c);
        return;
    }
    for(int i=0;s[i];++i) {
        GlyphRows rows(x+i*6,y);
        for(int row=0;row<8;++row) {
            const unsigned bits=trackr::ui::glyph_row(s[i],row);int run=-1;
            for(int col=0;col<=6;++col) {
                const bool on=col<6&&(bits&(1u<<(5-col)));
                if(on&&run<0)run=col;
                if(!on&&run>=0) {
                    rows.add(run,col-run,run_color(p,x+i*6+run,y+row,col-run,1,c,true));run=-1;
                }
            }
            rows.finish(row);
        }
        rows.flush();
    }
}
void diamond(int x,int y,bool selected,u32 c,int p=-1) {
    for(int dy=-3;dy<=3;++dy) {
        const int r=(3-(dy<0?-dy:dy))*2/3;
        if(selected||!r)run_rect(p,x-r,y+dy,2*r+1,1,c);
        else {run_rect(p,x-r,y+dy,1,1,c);run_rect(p,x+r,y+dy,1,1,c);}
    }
}
void draw_library() {
    for(int i=0;i<g.data.recent_count;++i) {
        const int slot=g.data.recent_slots[i], baseline=49+i*36;
        const bool sel=g.page==Page::Home&&g.selected==Recent0+i;
        const auto& item=g.data.projects[slot];const u32 c=sel?spot():ink();
        diamond(156,baseline-7,sel,sel?spot():ghost(),0);
        title(168,baseline,item.name[0]?item.name:"untitled",c,0);
        small(168,baseline+9,item.age[0]?item.age:"--",ghost(),0);
    }
    if(!g.data.recent_count)small(158,45,"no recent patches",ghost(),0);
    small(168,223,"all patches >",g.page==Page::Home&&g.selected==All?spot():ink(),0);
}
void draw_settings() {
    const char* names[]={"menu sound","startup","palette editor"};
    const char* values[]={g.data.menu_sound?"on":"off",g.data.show_menu?"menu":"last project",
                         theme_config::name(g.data.theme.id)};
    for(int i=0;i<kSettingsCount;++i) {
        const int baseline=49+i*44;
        const bool sel=(g.page==Page::Home&&g.selected==Sound+i)||(g.page==Page::Theme&&i==kSettingsCount-1);
        title(158,baseline,names[i],sel?spot():ink(),1,146);
        small(158,baseline+9,values[i],ghost(),1);
        if(sel)run_rect(1,158,baseline+5,title_width(names[i]),1,spot());
    }
}
void draw_home() {
    const int ys[]={35,99,166}, underline[]={63,127,191};
    for(int i=0;i<3;++i) {
        const int f=focus(i);ink_heading(i,20,ys[i],f);
        if(f)ui::rect(20,underline[i],start_menu_labels::labels[i].width,1,opacity(spot(),f));
    }
    draw_library();draw_settings();
}
void draw_browser() {
    heading(1,20,16,ink());small(20,50,"all patches",ghost());
    char page[16];std::snprintf(page,sizeof page,"%02d/%02d",g.browser_page+1,browser_pages());
    small(218,48,"<",g.browser_page?ink():ghost());small(240,48,page,ghost());
    small(290,48,">",g.browser_page+1<browser_pages()?ink():ghost());
    for(int row=0;row<kBrowserRows;++row) {
        const int idx=g.browser_page*kBrowserRows+row;if(idx>=g.browser_count)break;
        const int slot=g.browser_slots[idx],y=73+row*22;const auto& item=g.data.projects[slot];
        const bool sel=g.selected==slot;const u32 c=sel?spot():valid(slot)?ink():ghost();
        diamond(23,y+3,sel,c);char name[PROJECT_NAME_BYTES];
        fitted(name,item.name[0]?item.name:"untitled",23);small(36,y,name,c);
        const char* age=valid(slot)?(item.age[0]?item.age:"--"):"check file";
        small(304-ui::text_w(age),y,age,ghost());
    }
    if(!g.browser_count)small(20,92,"no saved patches",ghost());
    small(20,212,"< back",g.selected==kBrowserBack?spot():ghost());
    small(192,228,"l/r page  b back",ghost());
}
u32 modal_color(u32 c) { return opacity(c,g.modal); }
void theme_chip(int x,int y,int w,int h,u32 c) {
    ui::rect(x,y,w,h,modal_color(c));
    ui::frame(x,y,w,h,modal_color(theme_config::mix(c,ink(),48)));
}
void draw_theme() {
    // The same live menu underneath, not a screenshot; no input passes through.
    ui::rect(0,0,320,240,opacity(palette::rgb(0,0,0),g.modal*158/255));
    menu_style::paper_rect(12,12,296,216,g.modal);
    ui::frame(12,12,296,216,modal_color(theme_config::mix(palette::bg(),ink(),48)));
    title(26,43,"palette",modal_color(ink()),-1,160);
    char saved[32];std::snprintf(saved,sizeof saved,"saved %s",theme_config::name(g.data.theme.id));
    small(294-ui::text_w(saved),31,saved,modal_color(ghost()));
    for(int row=0;row<kThemeRole;++row) {
        const auto b=theme_box(row);auto sample=g.theme_draft;sample.id=theme_config::Id(row);
        const auto colors=theme_config::colors(sample);const u32 paints[]={colors.paper,colors.ink,colors.accent};
        for(int i=0;i<3;++i)theme_chip(b.x+4+i*7,b.y+6,6,10,paints[i]);
        const bool active=g.theme_draft.id==sample.id;const char* name=theme_config::name(sample.id);
        small(b.x+32,b.y+7,name,modal_color(active?spot():ink()));
        if(active)ui::rect(b.x+32,b.y+18,ui::text_w(name)-2,1,modal_color(spot()));
        if(g.page==Page::Theme&&g.selected==row)diamond(b.x-5,b.y+10,true,modal_color(spot()));
    }
    const auto colors=theme_config::colors(g.theme_draft);
    const u32 roles[]={colors.paper,colors.ink,colors.accent};const char* names[]={"paper","ink","accent"};
    for(int i=0;i<3;++i) {
        const auto b=theme_box(kThemeRole+i);theme_chip(b.x+4,b.y+4,17,14,roles[i]);
        small(b.x+28,b.y+8,names[i],modal_color(g.theme_role==i?spot():ghost()));
        if(g.page==Page::Theme&&g.selected==kThemeRole+i)ui::frame(b.x+2,b.y+2,21,18,modal_color(spot()));
    }
    for(int i=0;i<theme_config::kSwatchCount;++i) {
        const auto b=theme_box(kThemeSwatch+i);theme_chip(b.x+4,b.y+3,28,12,theme_config::swatches[i]);
        if(theme_config::swatches[i]==roles[g.theme_role])diamond(b.x+18,b.y+8,true,modal_color(g.theme_inverse[g.theme_role]));
        if(g.page==Page::Theme&&g.selected==kThemeSwatch+i)ui::frame(b.x+2,b.y+1,32,16,modal_color(spot()));
    }
    small(26,209,"cancel",modal_color(g.page==Page::Theme&&g.selected==kThemeCancel?spot():ghost()));
    small(270,209,"save",modal_color(g.page==Page::Theme&&g.selected==kThemeSave?spot():ink()));
    if(g.theme_warning)small(126,209,"low contrast",modal_color(ghost()));
}
void draw_theme_preview() {
    const auto c=theme_config::colors(g.theme_draft);
    const u32 muted=theme_config::mix(c.paper,c.ink,163), rule=theme_config::mix(c.paper,c.ink,61);
    ui::rect(0,0,400,240,c.paper);
    small(40,16,"seance / theme",muted);small(318,16,"preview",muted);
    heading(4+int(g.theme_draft.id),40,39,c.ink);
    ui::frame(40,102,96,83,rule);ui::frame(152,102,96,83,rule);ui::frame(264,102,96,83,rule);
    small(54,114,"paper",muted);small(166,114,"ink",muted);small(278,114,"accent",muted);
    ui::rect(54,138,68,32,c.paper);ui::rect(166,138,68,32,c.ink);ui::rect(278,138,68,32,c.accent);
    small(40,202,"three paints / one instrument",muted);
    small(40,228,"draft",muted);
    const char* state=g.theme_draft==g.data.theme?"saved palette":"not applied";
    small(360-ui::text_w(state),228,state,muted);
}
} // namespace

void start_menu_set_data(const StartMenuData& data) {
    const StartMenuData copy=data;g.data=copy;
    if(!theme_config::valid(g.data.theme))g.data.theme={};
    if(g.page==Page::Theme)theme_leave();
    if(!project_slot_valid(g.data.active_slot))g.data.active_slot=-1;
    g.browser_count=0;
    for(int i=0;i<PROJECT_SLOTS;++i) {
        auto& p=g.data.projects[i];clean(p.name,sizeof p.name);clean(p.age,sizeof p.age);
        if(!p.exists)p.valid=false;else g.browser_slots[g.browser_count++]=i;
    }
    int recent[kStartRecentCount];for(int& slot:recent)slot=-1;
    int n=0;
    for(int i=0;i<copy.recent_count&&i<kStartRecentCount;++i) {
        const int slot=copy.recent_slots[i];if(!valid(slot))continue;
        bool duplicate=false;for(int j=0;j<n;++j)if(recent[j]==slot)duplicate=true;
        if(!duplicate)recent[n++]=slot;
    }
    g.data.recent_count=n;for(int i=0;i<kStartRecentCount;++i)g.data.recent_slots[i]=recent[i];
    normalize_selection();transition_input();
}
void start_menu_open(const StartMenuData& data) {
    const StartMenuData copy=data;g=State{};start_menu_set_data(copy);
}
void start_menu_error(const char* text) {
    int n=0;if(text)while(n<46&&text[n]){g.error[n]=text[n];++n;}
    g.error[n]=0;if(n==46&&text[n])g.error[43]=g.error[44]=g.error[45]='.';
    clean(g.error,sizeof g.error);g.reveal_ms=startup_reveal::kDurationMs;transition_input();
}
const StartMenuData& start_menu_data() { return g.data; }
void start_menu_tick(u32 elapsed_ms) {
    const u32 remaining=startup_reveal::kDurationMs-g.reveal_ms;
    g.reveal_ms+=elapsed_ms<remaining?elapsed_ms:remaining;
    g.logo_ms=(g.logo_ms+elapsed_ms%wordmark_morph::kLoopMs)%wordmark_morph::kLoopMs;
    const int dt=int(elapsed_ms>400?400:elapsed_ms);
    // Modal and browser visits freeze only lower-menu ink, with no catch-up;
    // the original upper wordmark clock above deliberately keeps running.
    if(g.page==Page::Home&&!g.modal) {
        g.focus_age+=dt;if(g.focus_age>cabinet_ink::kFocusMs)g.focus_age=cabinet_ink::kFocusMs;
        g.ink_clock.tick(elapsed_ms);
        g.reveal.tick(elapsed_ms);
    }
    g.modal_age+=dt;if(g.modal_age>200)g.modal_age=200;
    const int e=ease(g.modal_age,g.modal_open?200:160);
    g.modal=(g.modal_from*(255-e)+(g.modal_open?255:0)*e+127)/255;
}
StartMenuResult start_menu_input(u32 down,u32 held,int tx,int ty) {
    // Closing palette remains modal until its last visible frame, including B.
    if(!g.modal_open&&g.modal) { transition_input();return {}; }
    if(g.quarantine) {
        // Consume the first sample once, then own each captured key until its
        // release. A stuck/ignored key must not gate unrelated fresh input.
        g.input_tail.capture(down,held);g.quarantine=false;
        cancel_touch();g.touch_held=false;g.buttons_held=false;
        return {};
    }
    u32 up=0;g.input_tail.filter(down,held,up);
    const bool touching=(held&KEY_TOUCH)!=0,was_touching=g.touch_held;
    const bool released=g.buttons_held&&!(held&kButtons);
    g.buttons_held=(held&kButtons)!=0;g.touch_held=touching;
    if(down&~kScreenshot)g.reveal_ms=startup_reveal::kDurationMs;
    const bool button_owned=((down|held)&kButtons)||released;
    if(button_owned) {
        cancel_touch();
        if(down&KEY_B) {
            meaningful();
            if(g.page==Page::Theme)theme_leave();
            else if(g.page==Page::Browser)home_from_browser();
            else if(g.panel!=Panel::None) {
                const int previous=owner();panel_to(Panel::None);select(previous);
            } else if(g.reveal.settled()) { transition_input();return {StartMenuAction::Exit,-1}; }
            return {};
        }
        if(g.page==Page::Theme&&(down&(KEY_A|KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))) {
            g.modal=g.modal_from=255;g.modal_age=200;
        }
        if(g.page==Page::Theme&&(down&(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))) {move_theme(down);return {};}
        if(g.page==Page::Browser&&(down&(KEY_LEFT|KEY_RIGHT|KEY_L|KEY_R))) {page_browser(down&(KEY_LEFT|KEY_L)?-1:1);return {};}
        if(down&KEY_UP){move(-1);return {};}
        if(down&KEY_DOWN){move(1);return {};}
        if(g.page==Page::Home&&(down&(KEY_LEFT|KEY_RIGHT))) {
            if(g.selected>=Sound&&g.selected<=Startup) {
                if(down&KEY_RIGHT)return activate(g.selected);
                select(Settings);return {};
            }
            if(down&KEY_LEFT) {if(g.selected>=3)select(owner());return {};}
            if(g.selected<3) {
                if(g.selected==New)return {};
                const Panel desired=g.selected==Settings?Panel::Settings:Panel::Library;
                if(g.panel!=desired)panel_to(desired);
                // Explicit keyboard entry finishes the decoration immediately;
                // no queued input or activation of a not-yet-visible label.
                g.reveal.settle(int(desired));
                int list[HomeRows];const int n=home_rows(list,false);if(n)select(list[0]);
            }
            return {};
        }
        if(down&KEY_A) {
            const auto result=activate(g.selected);
            if(result.action==StartMenuAction::LoadProject||result.action==StartMenuAction::NewProject)transition_input();
            return result;
        }
        return {};
    }
    if(touching) {
        if(!was_touching) {
            g.touch_row=(down&KEY_TOUCH)?row_at(tx,ty):-1;g.touch_cancelled=g.touch_row<0;
            if(!g.touch_cancelled&&!(g.page==Page::Browser&&g.touch_row>kBrowserBack))select(g.touch_row);
        } else if(row_at(tx,ty)!=g.touch_row)cancel_touch();
        return {};
    }
    if(was_touching) {
        const int row=g.touch_row;const bool commit=!g.touch_cancelled&&row>=0;cancel_touch();
        if(commit) {
            const auto result=activate(row);
            if(result.action==StartMenuAction::LoadProject||result.action==StartMenuAction::NewProject)transition_input();
            return result;
        }
    }
    return {};
}
void start_menu_capture(uint64_t now_ms) {
    // Both right panels belong to the same home sheet; switching never restarts
    // or fades its field. A palette visit freezes that sheet rather than catching up.
    menu_style::startup_capture(g.page!=Page::Browser,now_ms,g.page==Page::Theme||g.modal!=0);
}
void start_menu_draw_top() {
    if(g.page==Page::Theme) { draw_theme_preview();return; }
    menu_style::paper(400);
    const u32 c=ink()&0xffffffu;
    wordmark_morph::render(g.logo_ms,[c](int x,int y,int w,int h,u8 a){ui::rect(x,y,w,h,c|(u32(a)<<24));});
}
void start_menu_draw_bottom() {
    menu_style::paper(320);
    if(g.page==Page::Browser)draw_browser();else draw_home();
    if(g.modal)draw_theme();
    if(g.error[0]) {
        menu_style::paper_rect(12,0,296,12);
        small(16,2,g.error,spot());
    }
}
void start_menu_draw_reveal(int width) {
    if(width!=320&&width!=400)return;
    const u32 a=startup_reveal::veil_alpha(g.reveal_ms);if(a)ui::rect(0,0,width,240,a<<24);
}
} // namespace seance::platform::ui3ds
