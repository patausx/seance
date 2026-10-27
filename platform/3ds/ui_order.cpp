#include "ui_order.h"
#include "ui_transition.h"
#include "performance_tabs.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_nav.h"
#include "ui_palette.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "order_motion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::order {
namespace {
namespace m = menu_style;
bool g_open=false,g_blocked=false,g_choose=false;
int g_selected=0,g_page=0;
// Main ticks before the next audio DTO is published. A just-added last card
// must survive that one stale-length frame (especially when it opens a page).
u32 g_selection_after=0;
const char* g_notice=nullptr;
u32 g_notice_until=0;
enum class Action { None,Select,Bank,Bars,Grip,Hold,Add,Loop,Run,Next,PrevPage,NextPage,Choose,Remove,Cancel };
struct Press {
    Action action=Action::None;
    Engine* owner=nullptr;
    int card=-1,x=0,y=0,target=-1,preview=-1,bars=1,bank=0,length=0;
    int grab_x=0,grab_y=0;
    PatternChainStep steps[PATTERN_CHAIN_STEPS]{};
    bool moved=false,pause=false;
    u32 page_after=0;
} g_press;
order_motion::Point g_motion[PATTERN_CHAIN_STEPS];
Engine* g_motion_owner=nullptr;
int g_motion_length=-1,g_motion_page=-1,g_front=-1;
// Audio publication may trail an accepted local reorder by one display frame.
// Bridge only an exact pre-edit DTO, never an unrelated/newer project frame.
struct PendingOrder {
    PatternChainStep before[PATTERN_CHAIN_STEPS]{},after[PATTERN_CHAIN_STEPS]{};
    int remap[PATTERN_CHAIN_STEPS]{};
    int length=0,position=-1,next_position=-1,old_position=-1,old_next_position=-1;
    u32 until=0;
} g_pending;
static_assert(sizeof(g_motion)<=768,"order card motion is fixed UI-only storage");
// No clocks, engine reads or mutable capture in either draw path. One published
// engine DTO drives both displays. UI-only state changes only in input/tick.
bool allowed() {return g_open&&!g_blocked&&g_view==View::Performance&&g_overlay==Overlay::None&&!nav_is_open();}
int clamp(int n,int lo,int hi){return std::max(lo,std::min(hi,n));}
int length(){return clamp(g_ui_snapshot.pattern_chain_length,0,PATTERN_CHAIN_STEPS);}
int cx(int slot){return 8+(slot%2)*156;}
int cy(int slot){return 51+(slot/2)*78;}
bool stale_order(){
    if(!g_pending.length||length()!=g_pending.length||int32_t(g_ui_frame-g_pending.until)>=0)return false;
    bool old=true,next=true;
    for(int i=0;i<length();++i){
        const auto step=g_ui_snapshot.pattern_chain_steps[i];
        old&=step.pattern==g_pending.before[i].pattern&&step.bars==g_pending.before[i].bars;
        next&=step.pattern==g_pending.after[i].pattern&&step.bars==g_pending.after[i].bars;
    }
    const int next_position=g_ui_snapshot.pattern_chain_next_pending?g_ui_snapshot.pattern_chain_next_position:-1;
    const bool old_cursor=g_ui_snapshot.pattern_chain_position==g_pending.old_position&&next_position==g_pending.old_next_position;
    const bool next_cursor=g_ui_snapshot.pattern_chain_position==g_pending.position&&next_position==g_pending.next_position;
    return old&&(!next||(old_cursor&&!next_cursor));
}
PatternChainStep step_at(int i){return stale_order()?g_pending.after[i]:g_ui_snapshot.pattern_chain_steps[i];}
int display_position(int i){return stale_order()&&i>=0&&i<length()?g_pending.remap[i]:i;}
void remember_move(int from,int to,const PatternChainStep* before){
    if(!stale_order()){
        for(int i=0;i<length();++i){g_pending.before[i]=before[i];g_pending.remap[i]=i;}
        g_pending.old_position=g_ui_snapshot.pattern_chain_position;
        g_pending.old_next_position=g_ui_snapshot.pattern_chain_next_pending?g_ui_snapshot.pattern_chain_next_position:-1;
    }
    for(int i=0;i<length();++i){
        g_pending.after[i]=g_eng->pattern_chain[i];
        int& position=g_pending.remap[i];
        if(position==from)position=to;
        else if(from<to&&position>from&&position<=to)--position;
        else if(from>to&&position>=to&&position<from)++position;
    }
    g_pending.position=g_eng->pattern_chain_position;
    const int queued=g_ui_snapshot.pattern_chain_next_position;
    g_pending.next_position=g_ui_snapshot.pattern_chain_next_pending?(queued<length()?g_pending.remap[queued]:queued):-1;
    g_pending.length=length();g_pending.until=g_ui_frame+2;
}
bool gripping(){return g_press.action==Action::Grip&&g_press.card>=0;}
bool dragging(){return gripping()&&g_press.moved;}
int preview_position(int index){
    if(!dragging()||g_press.preview<0)return index;
    const int from=g_press.card,to=g_press.preview;
    if(index==from)return to;
    if(from<to&&index>from&&index<=to)return index-1;
    if(from>to&&index>=to&&index<from)return index+1;
    return index;
}
void reset_motion(){
    for(auto& point:g_motion)point={};
    g_motion_owner=nullptr;g_motion_length=g_motion_page=g_front=-1;
}
void arrange(uint64_t now){
    if(g_motion_owner!=g_eng)g_pending={};
    const bool fresh=g_motion_owner!=g_eng||g_motion_length!=length()||g_motion_page!=g_page;
    if(fresh){
        g_motion_owner=g_eng;g_motion_length=length();g_motion_page=g_page;
        // Page changes reveal their own geometry, never travel across a second
        // page's controls. A held card keeps its own absolute stylus position.
        for(int i=0;i<length();++i){
            if(gripping()&&i==g_press.card&&g_motion[i].ready)continue;
            const int slot=preview_position(i)%4;g_motion[i].snap(cx(slot),cy(slot));
        }
        if(!gripping())g_front=-1;
    }
    for(int i=0;i<length();++i){
        if(gripping()&&i==g_press.card){g_motion[i].sample(now);continue;}
        const int slot=preview_position(i)%4;
        g_motion[i].aim(cx(slot),cy(slot),now,i==g_front?180:150);
    }
    if(g_front>=0&&!gripping()&&(g_front>=length()||!g_motion[g_front].active))g_front=-1;
}
int shown_x(int i){return g_motion_length==length()&&g_motion_page==g_page&&g_motion[i].ready?int(std::lround(g_motion[i].x)):cx(i%4);}
int shown_y(int i){return g_motion_length==length()&&g_motion_page==g_page&&g_motion[i].ready?int(std::lround(g_motion[i].y)):cy(i%4);}
int shown_card_at(int x,int y){
    if(g_front>=0&&g_front<length()&&g_front/4==g_page&&hit(x,y,shown_x(g_front),shown_y(g_front),148,72))return g_front;
    for(int i=std::min(length(),g_page*4+4)-1;i>=g_page*4;--i)
        if(hit(x,y,shown_x(i),shown_y(i),148,72))return i;
    return -1;
}
void move_motion(int from,int to){
    const auto moving=g_motion[from];
    if(from<to)for(int i=from;i<to;++i)g_motion[i]=g_motion[i+1];
    else for(int i=from;i>to;--i)g_motion[i]=g_motion[i-1];
    g_motion[to]=moving;g_front=to;
}
void center(int x,int y,const char* text,u32 ink){ui::text(x-ui::text_w(text)/2,y,text,ink);}
void notice(const char* text){g_notice=text;g_notice_until=g_ui_frame+150;}
void select(int index){g_selected=std::max(0,index);g_page=g_selected/4;}
int card_at(int x,int y){
    for(int slot=0;slot<4;++slot)if(hit(x,y,cx(slot),cy(slot),148,72))return g_page*4+slot<length()?g_page*4+slot:-1;
    return -1;
}
int next_index(){
    const auto& f=g_ui_snapshot;
    if(f.pattern_chain_next_pending)return f.pattern_chain_next_position<length()?display_position(f.pattern_chain_next_position):-1;
    const int pos=display_position(f.pattern_chain_position);
    if(pos+1<length())return pos+1;
    return length()&&f.pattern_chain_loop?0:-1;
}
void digit(int x,int top,int bank,u32 color){
    char text[2]={char('1'+clamp(bank,0,7)),0};
    const auto* glyph=inspector_gothic::digit(text[0]);
    if(glyph)inspector_gothic::draw_numeric(x,top-glyph->top,text,color,color);
}
void ruler(int x,int y,int width,int bars,int done,u32 color,u32 rail=0){
    bars=clamp(bars,1,16);done=clamp(done,0,bars);
    for(int i=0;i<bars;++i){const int a=x+i*width/bars,b=x+(i+1)*width/bars-3;ui::rect(a,y,std::max(1,b-a),i<done?2:1,i<done?color:rail?rail:m::branch());}
}
void cancel(){
    if(dragging())g_front=g_press.card;
    g_press={};if(g_drag==Drag::PerformanceMorph)g_drag=Drag::None;
    if(g_open)arrange(osGetTime());
}
void start_press(Action a,int card,int x,int y){
    g_press={};g_press.action=a;g_press.owner=g_eng;g_press.card=card;g_press.target=card;g_press.preview=card;g_press.x=x;g_press.y=y;g_press.length=length();
    g_press.page_after=g_ui_frame;g_press.pause=g_ui_snapshot.pattern_chain_playing;
    if(card>=0&&card<length()) {
        for(int i=0;i<length();++i)g_press.steps[i]=step_at(i);
        const auto step=step_at(card);g_press.bars=step.bars;g_press.bank=step.pattern;
        g_press.grab_x=x-shown_x(card);g_press.grab_y=y-shown_y(card);
        if(a==Action::Grip){
            // Catch an in-flight return at the actual touched position. Until
            // the 6px threshold this is a press, never a moving drop preview.
            g_motion[card].snap(shown_x(card),shown_y(card));g_front=card;
        }
    }
    g_drag=Drag::PerformanceMorph;
}
bool same_card(){
    // External edits/project replacement cannot turn a held press into a
    // command for a different row. Reorder is committed once, on true lift.
    if(g_press.owner!=g_eng||!g_eng||g_eng->pattern_chain_length!=g_press.length)return false;
    const int i=g_press.card;
    if(i<0)return true;
    for(int at=0;at<g_press.length;++at)
        if(g_eng->pattern_chain[at].pattern!=g_press.steps[at].pattern||g_eng->pattern_chain[at].bars!=g_press.steps[at].bars)return false;
    return i<g_eng->pattern_chain_length;
}
void execute(){
    if(!g_eng||!g_audio)return;
    platform::AudioOut::Guard guard(*g_audio);
    if(!same_card()){notice("order changed / touch again");return;}
    const int i=g_press.card;
    bool changed=false;
    switch(g_press.action){
        case Action::Add:
            if(g_eng->insert_pattern_chain_step(g_eng->pattern_chain_length,g_eng->active_pattern,4)){select(g_eng->pattern_chain_length-1);g_selection_after=g_ui_frame+2;changed=true;}
            else notice("order full / 16 cards");
            break;
        case Action::Loop:g_eng->pattern_chain_loop=!g_eng->pattern_chain_loop;changed=true;break;
        case Action::Run:
            if(!g_eng->pattern_chain_length)notice("add a card first");
            // Intent belongs to down, not the audio state at lift: a pause
            // held across the last bar must never restart a finished order.
            else if(g_press.pause) {if(g_eng->pattern_chain_playing)g_eng->take_over_pattern_chain();}
            else if(!g_eng->pattern_chain_playing&&!g_eng->resume_pattern_chain(g_selected))notice("keep bank in banks / then resume");
            break;
        case Action::Next:
            if(!g_eng->next_pattern_chain())notice("start order first");
            break;
        case Action::Hold:
            if(i==g_eng->pattern_chain_position&&g_eng->pattern_chain_playing)g_eng->set_pattern_chain_hold(!g_eng->pattern_chain_held);
            else select(i);
            break;
        case Action::Select:select(i);break;
        case Action::Bank:select(i);g_choose=true;break;
        case Action::Grip:
            if(g_press.moved){
                // Invalid lift is a return, not a selection or a move to the
                // last hovered card. The preview never mutates the engine.
                if(g_press.target>=0&&g_press.target!=i){
                    PatternChainStep before[PATTERN_CHAIN_STEPS];
                    std::copy_n(g_eng->pattern_chain,length(),before);
                    changed=g_eng->move_pattern_chain_step(i,g_press.target);
                    if(changed){remember_move(i,g_press.target,before);move_motion(i,g_press.target);select(g_press.target);}
                }
            }else select(i);
            break;
        case Action::Bars:{
            const int bars=clamp(g_press.bars+(g_press.target-i),1,16);
            if(bars!=g_press.bars){changed=g_eng->set_pattern_chain_step(i,g_press.bank,bars);if(changed&&g_eng->pattern_chain_playing&&i==g_eng->pattern_chain_position)notice("bars saved / next visit");}
            select(i);break;
        }
        case Action::Choose:
            if(g_press.target!=g_press.bank){changed=g_eng->set_pattern_chain_step(i,g_press.target,g_press.bars);if(changed&&g_eng->pattern_chain_playing&&i==g_eng->pattern_chain_position)notice("bank saved / next visit");}
            g_choose=false;break;
        case Action::Remove:
            changed=g_eng->remove_pattern_chain_step(i);
            if(changed){select(std::min(i,std::max(0,int(g_eng->pattern_chain_length)-1)));g_choose=false;}
            else notice("current or queued card / cannot remove");
            break;
        case Action::Cancel:g_choose=false;break;
        case Action::PrevPage:g_page=std::max(0,g_page-1);break;
        case Action::NextPage:g_page=std::min((std::max(1,length())-1)/4,g_page+1);break;
        default:break;
    }
    if(changed)mark_project_mutated();
}
u32 rule(){return palette::current()==palette::Theme::Pink?m::branch():palette::subdued();}
void card(int index,int x,int y,bool moving=false){
    const auto& f=g_ui_snapshot;
    const auto entry=step_at(index);
    const bool current=(f.pattern_chain_playing||f.pattern_chain_paused)&&index==display_position(f.pattern_chain_position);
    const bool playing=current&&f.pattern_chain_playing;
    const bool selected=moving||(g_front<0&&index==g_selected);
    const u32 paper=palette::bg(),ink=selected?paper:m::ink();
    const u32 quiet=selected?theme_config::mix(m::ink(),paper,190):m::ghost();
    const u32 rail=selected?theme_config::mix(m::ink(),paper,72):rule();
    // Approved flat tiles: selection is ink/paper inversion, not an outline or
    // a second arbitrary accent palette. All surfaces follow the active theme.
    ui::rect(x,y,148,72,selected?m::ink():palette::cell());
    char text[32];std::snprintf(text,sizeof text,"%02d",index+1);ui::text(x+10,y+8,text,quiet);
    const char* status=playing?(f.pattern_chain_start_pending?"armed":!f.transport_playing?"ready":f.pattern_chain_held?"held":"play"):current?"pause":index==next_index()&&f.pattern_chain_playing?"next":"";
    ui::text(x+119-ui::text_w(status),y+8,status,selected?ink:current?m::spot():m::ghost());
    for(int k=0;k<3;++k)ui::rect(x+130+k*3,y+9,1,3,quiet);
    int bank=entry.pattern,bars=entry.bars;
    if(g_press.action==Action::Bars&&g_press.card==index)bars=clamp(g_press.bars+g_press.target-index,1,16);
    const bool queued=!playing&&f.pattern_chain_playing&&!f.pattern_chain_start_pending&&f.pattern_chain_next_pending&&index==display_position(f.pattern_chain_next_position);
    const bool latched=playing||queued;
    const int live_bank=queued?f.pattern_chain_next_pattern:f.pattern_chain_start_pending?f.pattern_chain_armed_pattern:f.pattern_chain_current_pattern;
    const int live_bars=queued?f.pattern_chain_next_bars:f.pattern_chain_start_pending?f.pattern_chain_armed_bars:f.pattern_chain_current_bars;
    const int shown_bank=latched?live_bank:bank;
    const auto* glyph=inspector_gothic::digit(char('1'+clamp(shown_bank,0,7)));
    digit(x+29-(glyph?glyph->width/2:0),y+23,shown_bank,ink);
    // Deferred edits retain the accepted glyph/ruler. The saved bank is a small
    // annotation in the free lower-left field, never squeezed under the glyph.
    if(latched&&bank!=live_bank){std::snprintf(text,sizeof text,"next%d",bank+1);ui::text(x+10,y+61,text,quiet);}
    if(latched&&bars!=live_bars)std::snprintf(text,sizeof text,"%d>%d bars",live_bars,bars);
    else std::snprintf(text,sizeof text,"%d bars",bars);
    ui::text(x+68,y+29,text,ink);
    const int done=playing&&!f.pattern_chain_start_pending?std::max(0,live_bars-int(f.pattern_chain_bars_remaining)+1):0;
    ruler(x+68,y+43,62,latched?live_bars:bars,done,ink,rail);
    const char* action=playing?(f.pattern_chain_held?"held":"hold"):selected?"selected":"";
    ui::text(x+130-ui::text_w(action),y+59,action,quiet);
}
void draw_chooser(){
    char text[48];
    std::snprintf(text,sizeof text,"card %02d / choose bank",g_selected+1);
    ui::text(10,33,g_notice?g_notice:text,m::ink());
    const int chosen=step_at(g_selected).pattern;
    if(!g_notice){std::snprintf(text,sizeof text,"%d selected",chosen+1);ui::text(310-ui::text_w(text),33,text,m::ghost());}
    for(int bank=0;bank<8;++bank){
        const int x=16+74*(bank%4),y=65+62*(bank/4);
        const bool selected=bank==chosen,pressed=g_press.action==Action::Choose&&g_press.target==bank;
        if(selected||pressed)ui::rect(x,y,66,52,selected?m::ink():palette::cell());
        const auto* glyph=inspector_gothic::digit(char('1'+bank));
        digit(x+33-(glyph?glyph->width/2:0),y+9,bank,selected?palette::bg():m::ink());
    }
    ui::line(8,209,311,209,rule());
    ui::text(10,221,"remove card",m::ghost());ui::text(310-ui::text_w("cancel"),221,"cancel",m::ink());
}
}
bool active(){return g_open;}
void open(){g_open=true;g_blocked=false;g_choose=false;cancel();tick();}
void close(){cancel();g_choose=false;g_open=false;reset_motion();g_pending={};}
void reset(){close();g_selected=g_page=0;g_selection_after=0;g_notice=nullptr;g_notice_until=0;g_blocked=false;}
void tick(){
    if(!g_open)return;
    if(g_selected<length()||int32_t(g_ui_frame-g_selection_after)>=0){
        g_selected=clamp(g_selected,0,std::max(0,length()-1));g_page=clamp(g_page,0,(std::max(1,length())-1)/4);
    }
    if(!length())g_choose=false;
    if(g_view!=View::Performance||g_overlay!=Overlay::None||nav_is_open()){cancel();g_choose=false;}
    if(g_notice&&int32_t(g_ui_frame-g_notice_until)>=0)g_notice=nullptr;
    // A different project/sequence length cannot inherit a held card's motion.
    if(g_press.action!=Action::None&&(g_press.owner!=g_eng||g_press.length!=length()))cancel();
    arrange(osGetTime());
}
void buttons(u32 down,u32 held,u32 up){
    (void)up;const u32 modifiers=KEY_L|KEY_R|KEY_SELECT|KEY_B|KEY_START;
    if((down|held)&modifiers){cancel();g_choose=false;}
    g_blocked=(held&modifiers)!=0;
    // No chain triggers, mute, bank launch, or auto/manual ownership via dpad.
}
void touch_down(int x,int y){
    cancel();if(!allowed())return;
    if(g_choose){
        for(int bank=0;bank<8;++bank)if(hit(x,y,16+74*(bank%4),65+62*(bank/4),66,52)){start_press(Action::Choose,g_selected,x,y);g_press.target=bank;return;}
        if(hit(x,y,0,210,160,30))start_press(Action::Remove,g_selected,x,y);
        else if(hit(x,y,160,210,160,30))start_press(Action::Cancel,-1,x,y);
        return;
    }
    const int tab=performance_tabs::target(x,y);
    if(tab>=0){if(tab==performance_tabs::Perform)close();else if(tab==performance_tabs::Banks){close();transition::open();}return;}
    if(hit(x,y,238,25,28,22)){start_press(Action::PrevPage,-1,x,y);return;}
    if(hit(x,y,292,25,28,22)){start_press(Action::NextPage,-1,x,y);return;}
    if(y>=210&&y<240){
        start_press(x<80?Action::Add:x<150?Action::Loop:x<245?Action::Run:Action::Next,-1,x,y);return;
    }
    const int i=shown_card_at(x,y);if(i<0)return;
    const int dx=x-shown_x(i),dy=y-shown_y(i);
    const Action a=dy<20?Action::Grip:dx<60?Action::Bank:dy<53?Action::Bars:Action::Hold;
    start_press(a,i,x,y);
}
void touch_drag(int x,int y){
    if(!allowed()){cancel();return;}
    auto& p=g_press;
    if(p.action==Action::Grip){
        if(std::abs(x-p.x)+std::abs(y-p.y)>6)p.moved=true;
        // The same lifted drag can cross all four pages: dwell on a paging
        // arrow, then place on a card. The arrow itself is never a drop target.
        if(p.moved&&y>=25&&y<47&&int32_t(g_ui_frame-p.page_after)>=0){
            const int direction=x>=292&&x<320?1:x>=238&&x<266?-1:0;
            if(direction){g_page=clamp(g_page+direction,0,(std::max(1,length())-1)/4);p.page_after=g_ui_frame+24;}
        }
        p.target=card_at(x,y);
        if(p.moved){
            if(p.target>=0)p.preview=p.target;
            g_front=p.card;
            g_motion[p.card].snap(clamp(x-p.grab_x,4,168),clamp(y-p.grab_y,49,133));
            arrange(osGetTime());
        }
        return;
    }
    if(p.action==Action::Bars){
        if(std::abs(y-p.y)>30){cancel();return;}
        const int delta=(x-p.x)/10;p.target=p.card+clamp(p.bars+delta,1,16)-p.bars;return;
    }
    if(p.action==Action::Choose){
        if(!hit(x,y,16+74*(p.target%4),65+62*(p.target/4),66,52))cancel();
        return;
    }
    if(std::abs(x-p.x)>8||std::abs(y-p.y)>8)cancel();
}
void touch_hold(int x,int y){touch_drag(x,y);}
void touch_release(bool commit){
    // Keep the just-lifted layer on top while it eases into its home. A valid
    // engine reorder remaps that same visual identity before publishing.
    if(commit&&allowed()&&g_press.action!=Action::None)execute();
    g_press={};if(g_drag==Drag::PerformanceMorph)g_drag=Drag::None;
    arrange(osGetTime());
}
void draw(){
    m::paper(320);performance_tabs::draw(performance_tabs::Order);
    if(g_choose){draw_chooser();return;}
    const auto& f=g_ui_snapshot;char text[64];
    const char* state=f.pattern_chain_playing?(f.pattern_chain_start_pending?"armed":!f.transport_playing?"ready":f.pattern_chain_held?"held":"playing"):f.pattern_chain_paused?"manual":"stopped";
    std::snprintf(text,sizeof text,"%02d / %02d %s",length()?display_position(f.pattern_chain_position)+1:0,length(),state);
    if(!g_notice)ui::text(10,33,text,m::ghost());
    if(g_notice)ui::text(10,33,g_notice,m::ink());
    else {ui::text(248,33,"<",m::ghost());std::snprintf(text,sizeof text,"%d/%d",g_page+1,(std::max(1,length())+3)/4);center(279,33,text,m::ghost());ui::text(305,33,">",m::ghost());}
    if(!length()){center(160,95,"no cards yet",m::ink());center(160,112,"+ card / choose a bank / set bars",m::ghost());}
    // The placeholder is bare paper. Reflow cards first, held/landing identity
    // last, so real geometry (not a target outline) communicates the gesture.
    for(int i=0;i<length();++i){
        if(i==g_front)continue;
        if(preview_position(i)/4==g_page)card(i,shown_x(i),shown_y(i));
    }
    if(g_front>=0&&g_front<length()&&(dragging()||g_front/4==g_page))card(g_front,shown_x(g_front),shown_y(g_front),true);
    ui::line(8,209,311,209,rule());
    if(dragging()){
        std::snprintf(text,sizeof text,g_press.target>=0?"move %02d to %02d":"move %02d / outside",g_press.card+1,g_press.target+1);
        ui::text(10,221,text,m::ghost());const char* action=g_press.target>=0?"release to place":"release to return";
        ui::text(310-ui::text_w(action),221,action,m::ink());
    }else{
        ui::text(10,221,"+ card",length()<16?m::ghost():rule());
        ui::text(98,221,"loop",f.pattern_chain_loop?m::spot():m::ghost());if(f.pattern_chain_loop)ui::rect(98,232,23,1,m::spot());
        center(196,221,f.pattern_chain_playing?"pause":f.pattern_chain_paused?"resume":"start",m::ink());
        ui::text(310-ui::text_w("next >"),221,"next >",m::ghost());
    }
}
void draw_top(){
    m::paper(400);m::heading(40,8,"perform",m::ink());const auto& f=g_ui_snapshot;char text[64];
    std::snprintf(text,sizeof text,"%d bpm / %s",f.bpm,f.transport_playing?"play":"stop");ui::text(359-ui::text_w(text),9,text,m::ink());
    std::snprintf(text,sizeof text,"order %02d / %02d",length()?display_position(f.pattern_chain_position)+1:0,length());ui::text(359-ui::text_w(text),24,text,m::ghost());
    const bool auto_run=f.pattern_chain_playing;
    int now=f.pattern_transition.active?f.pattern_transition.source:f.active_pattern;
    if(auto_run&&!f.pattern_chain_start_pending)now=f.pattern_chain_current_pattern;
    const int ni=auto_run?(f.pattern_chain_start_pending?display_position(f.pattern_chain_position):next_index()):g_selected;
    int next=ni>=0&&ni<length()?step_at(ni).pattern:-1;
    int next_bars=ni>=0&&ni<length()?step_at(ni).bars:0;
    if(auto_run&&f.pattern_chain_start_pending){next=f.pattern_chain_armed_pattern;next_bars=f.pattern_chain_armed_bars;}
    else if(auto_run&&f.pattern_chain_next_pending&&ni>=0){next=f.pattern_chain_next_pattern;next_bars=f.pattern_chain_next_bars;}
    const char* mode=f.pattern_transition.active?"manual blend":f.pattern_chain_paused?"manual / order paused":auto_run?(f.pattern_chain_start_pending?"order armed":f.pattern_chain_held?"order held":"auto order"):"order stopped";
    const int bars=clamp(f.pattern_chain_current_bars,1,16),remaining=clamp(f.pattern_chain_bars_remaining,0,bars);
    for(int side=0;side<2;++side){
        const int x=44+162*side;
        center(x+75,51,side?(auto_run?"next":"resume from"):"now",side?m::ghost():m::spot());
        const int bank=side?next:now;
        if(bank>=0){const auto* glyph=inspector_gothic::digit(char('1'+clamp(bank,0,7)));digit(x+75-(glyph?glyph->width/2:0),79,bank,m::ink());}
        else center(x+75,89,"end",m::ghost());
        center(x+75,125,"bank",m::ghost());
        if(side){if(next>=0)std::snprintf(text,sizeof text,"%d bars",next_bars);else std::snprintf(text,sizeof text,"end of order");}
        else if(auto_run&&!f.pattern_chain_start_pending)std::snprintf(text,sizeof text,"bar %d / %d",bars-remaining+1,bars);
        else std::snprintf(text,sizeof text,"%s",f.pattern_transition.active?"rhythm blend":f.pattern_chain_start_pending?"next bar":"live bank");
        center(x+75,147,text,m::ink());
        ruler(x+18,169,114,side?std::max(1,next_bars):bars,side?0:auto_run&&!f.pattern_chain_start_pending?bars-remaining+1:0,m::spot(),rule());
    }
    center(200,185,mode,m::ghost());
    ui::text(44,207,"order",m::ghost());ui::text(355-ui::text_w(f.pattern_chain_loop?"loop":"once"),207,f.pattern_chain_loop?"loop":"once",m::spot());
    // Four-card route window, never pretend duplicate bank numbers identify rows.
    const int base=clamp(auto_run?display_position(f.pattern_chain_position)/4:g_page,0,3)*4;
    for(int j=0;j<4&&base+j<length();++j){
        const int i=base+j,x=116+68*j;std::snprintf(text,sizeof text,"%d",step_at(i).pattern+1);
        center(x,226,text,i==display_position(f.pattern_chain_position)&&auto_run?m::spot():m::ghost());if(j<3&&i+1<length())center(x+34,226,">",rule());
    }
}
}
