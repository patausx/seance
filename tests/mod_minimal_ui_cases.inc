// Appended to a temporary copy of osc_input_dispatch_test.cpp (main renamed).
// Existing fixtures/raster are dependencies only; no inherited test group runs.
namespace mod_minimal_cases {
namespace {
namespace fixture = fader_lancet_cases;
namespace hid = osc_input_main;
using fixture::Scene;
struct Pair {
    std::vector<u32> top,bottom;
    std::vector<TextRecord> top_text,bottom_text;
    int upper=0,lower=0;
};
using fixture::catalogue;
using fixture::item;
static std::string name;
static uint64_t duals=0, backgrounds=0, detached=0, gestures=0, routes=0, ownership=0, veil_pixels=0;
static int peak=0;

void bounds(bool overlap=true) {
    if(outside_rects || outside_lines || (overlap && text_overlaps))
        std::fprintf(stderr,"minimal mod %s: w%d rect%d line%d text%d\n",name.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0 && objects<=8192 && !outside_rects && !outside_lines);
    if(overlap)CHECK(!text_overlaps);
    for(const auto& t:texts)CHECK(t.x>=0 && t.y>=0 && t.x+t.w<=width && t.y+t.h<=240);
}
void surface(bool upper) {fixture::surface(upper);}
void open(Scene& scene,bool held=true) {
    hid::nav_opened=false;hid::frame(0,0);
    if(held) {
        const auto result=hid::frame(KEY_L,KEY_L);
        CHECK(result.owned && result.skip && result.remaining==0);
        hid::frame(0,KEY_L);
    } else {
        hid::down(250,232);hid::up();hid::frame(0,0);
    }
    CHECK(param_lens_is_open() && g_drag==Drag::None && !scene.engine->param_temp_active());
    scene.capture();
}
void close() {param_lens_cancel();hid::frame(0,0);g_drag=Drag::None;}
void down(int x,int y,u32 modifiers=KEY_L) {
    hid::touch={x,y};hid::frame(KEY_TOUCH,modifiers|KEY_TOUCH);
}
void move(int x,int y,u32 modifiers=KEY_L) {
    hid::touch={x,y};hid::frame(0,modifiers|KEY_TOUCH);
}
void up(u32 modifiers=KEY_L) {hid::frame(0,modifiers,KEY_TOUCH);}
void tap(int x,int y,u32 modifiers=KEY_L) {down(x,y,modifiers);up(modifiers);}
int selected(const Scene& s) {
    if(s.kind==UnitKind::Fm)return fm_focus_selected();
    if(s.kind==UnitKind::Ghost)return ghost_focus_selected();
    if(s.kind==UnitKind::Reverb)return reverb_focus_selected();
    if(s.kind==UnitKind::Stranular)return stranular_focus_selected();
    return module_inspector_selected(s.kind);
}
void choose(Scene& s,int p) {
    if(s.kind==UnitKind::Reverb)reverb_focus_select(p);
    else if(s.kind==UnitKind::Ghost)ghost_focus_select(p);
    else if(s.kind==UnitKind::Stranular)stranular_focus_select(p);
    else s.select(p);
    CHECK(selected(s)==p);
}
std::array<Param,MAX_PARAMS> parameters(const Scene& s) {
    std::array<Param,MAX_PARAMS> result{};
    std::copy(std::begin(s.unit->params),std::end(s.unit->params),result.begin());return result;
}
void same_controls(const Scene& s,const std::array<Param,MAX_PARAMS>& before) {
    for(int p=0;p<s.unit->num_params;++p) {
        const auto& a=before[p];const auto& b=s.unit->params[p];
        CHECK(a.base==b.base && a.src==b.src && a.mod_depth==b.mod_depth && a.mod_enabled==b.mod_enabled);
    }
}
void overlay_text() {
    // Intentional translucent underlay is not a text collision. Inspect only
    // foreground labels from the first new standalone link, not raster history.
    auto first=std::find_if(texts.begin(),texts.end(),[](const TextRecord& t){return t.text=="link";});
    CHECK(first!=texts.end());
    bool base=false,depth=false;
    for(auto a=first;a!=texts.end();++a) {
        CHECK(a->text.find("modulation")==std::string::npos && a->text.find("destination")==std::string::npos);
        CHECK(a->text.find("route depth")==std::string::npos && a->text!="<" && a->text!=">");
        if(a->text=="base" || a->text=="base / busy"){CHECK(a->call_x==24 && a->call_y==80);base=true;}
        if(a->text=="depth"){CHECK(a->call_x==24 && a->call_y==125);depth=true;}
        for(auto b=first;b!=a;++b)
            CHECK(a->x>=b->x+b->w || a->x+a->w<=b->x || a->y>=b->y+b->h || a->y+a->h<=b->y);
    }
    CHECK(base && depth);
}
Pair draw(Scene& s,bool frozen=false) {
    Pair result;const int dirty=mutations;const auto clock=s.engine->transport_frame;
    const auto ring=ring_process_calls,strain=strain_process_calls,rev=rev_process_calls;
    surface(true);{fixture::AudioHidden hidden;top_draw();}bounds();
    result.top=pixels;result.top_text=texts;result.upper=objects;
    surface(false);{fixture::AudioHidden hidden;editor_draw();}bounds(!param_lens_is_open());
    if(param_lens_is_open())overlay_text();
    result.bottom=pixels;result.bottom_text=texts;result.lower=objects;
    CHECK(result.upper+result.lower<=8192);peak=std::max(peak,result.upper+result.lower);++duals;
    if(frozen) {
        const auto again=[&]() {
            surface(true);{ModuleInspectorNoEngine hidden;top_draw();}bounds();CHECK(pixels==result.top && objects==result.upper);
            surface(false);{ModuleInspectorNoEngine hidden;fixture::lower(*s.unit);param_lens_draw_bottom();}
            bounds(false);CHECK(pixels==result.bottom && objects==result.lower);
        };
        CHECK(param_lens_is_open());again();
        surface(true);{ModuleInspectorNoEngine hidden;CHECK(param_lens_draw_top());}
        bounds();CHECK(pixels==result.top && objects==result.upper);
        {fixture::PoisonAfterCapture poison(s);again();}++detached;
    }
    CHECK(mutations==dirty && s.engine->transport_frame==clock);
    CHECK(ring_process_calls==ring && strain_process_calls==strain && rev_process_calls==rev);
    return result;
}
std::vector<u32> body(Scene& s) {
    surface(false);{ModuleInspectorNoEngine hidden;fixture::lower(*s.unit);}bounds();return pixels;
}
void preserved_background(Scene& s) {
    CHECK(!param_lens_is_open());s.capture();const auto ordinary=draw(s);
    const auto before=parameters(s);const auto clock=s.engine->transport_frame;
    open(s);CHECK(s.engine->transport_frame==clock);same_controls(s,before);
    // Draw each actual module while opened is genuinely true. Never spoof it,
    // mutate app globals to a fake closed state, or repaint an old PNG.
    const auto underlay=body(s);
    if(underlay!=ordinary.bottom) {
        size_t count=0;for(size_t i=0;i<underlay.size();++i)if(underlay[i]!=ordinary.bottom[i])++count;
        std::fprintf(stderr,"minimal mod %s: ordinary body changed under lens (%zu pixels)\n",name.c_str(),count);
    }
    CHECK(underlay==ordinary.bottom);
    const auto lens=draw(s,true);
    if(lens.top!=ordinary.top)std::fprintf(stderr,"minimal mod %s: ordinary upper changed while lens open\n",name.c_str());
    CHECK(lens.top==ordinary.top && lens.upper==ordinary.upper);
    CHECK(lens.bottom!=ordinary.bottom && lens.lower>ordinary.lower);
    surface(false);menu_style::paper(320);const auto paper=pixels;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
        const auto in=[&](int x0,int y0,int x1,int y1){return x>=x0 && x<x1 && y>=y0 && y<y1;};
        if(in(260,46,306,74) || in(24,80,296,89) || in(20,95,300,110) ||
           in(24,125,296,134) || in(20,140,300,155) || in(8,177,318,201))continue;
        const int alpha=in(8,44,312,205)?234:170;const int at=y*320+x;
        u32 expected=0xff000000u;
        for(unsigned shift:{0u,8u,16u}) {
            const unsigned src=(paper[at]>>shift)&255u,old=(ordinary.bottom[at]>>shift)&255u;
            expected|=((src*alpha+old*(255-alpha)+127u)/255u)<<shift;
        }
        CHECK(lens.bottom[at]==expected);++veil_pixels;
    }
    size_t ordinary_ink=0,retained=0;
    // Away from every overlay control, module ink must survive translucency.
    for(int y=8;y<224;++y)for(int x=2;x<318;++x) {
        const bool passive=y<44 || (x<8 || x>=312) || (y>=206 && y<224);
        if(!passive)continue;
        const int at=y*320+x;
        if(ordinary.bottom[at]!=paper[at]){++ordinary_ink;if(lens.bottom[at]!=paper[at])++retained;}
    }
    CHECK(ordinary_ink>20 && retained>20);++backgrounds;
    // Recapture after actual new engine frames WHILE the panel stays open.
    // Compare that body/top to a subsequent real close at the exact same audio
    // frame; a cached entry bitmap or a capture gate would fail this comparison.
    const int p=selected(s);s.set(p,s.unit->params[p].bipolar?q15(-11000):q15(23000));
    s.render(128);s.capture();CHECK(param_lens_is_open());
    const auto live_body=body(s);const auto live_lens=draw(s);
    CHECK(live_body!=underlay || live_lens.top!=lens.top);
    close();s.capture();const auto live_ordinary=draw(s);
    CHECK(live_body==live_ordinary.bottom && live_lens.top==live_ordinary.top);++backgrounds;
}
void compositions() {
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink})for(const auto& entry:catalogue) {
        palette::set(theme);name=std::string(entry.name)+(theme==palette::Theme::Graphite?" graphite":" pink");
        Scene s(entry.kind);s.signal(24000);preserved_background(s);
        // Frozen extrema, including both bipolar endpoints and negative depth.
        for(int edge:{0,1}) {
            s.set(entry.param,q15(edge?32767:(s.unit->params[entry.param].bipolar?-32767:0)));
            s.unit->params[entry.param].mod_depth=q15(edge?32767:-32767);
            open(s);draw(s);close();
        }
        // New engine frames and recapture must keep the ordinary context live,
        // not reuse a bitmap captured once when L was first pressed.
        s.set(entry.param,s.unit->params[entry.param].bipolar?q15(7000):q15(12000));
        s.signal(-24000);preserved_background(s);
        // Every current parameter formatter, not only the catalogue's featured
        // rail: all signed/unsigned extrema on both themes, same native draw.
        for(int p=0;p<s.unit->num_params;++p) {
            if(entry.kind==UnitKind::Recorder && p==RecUnit::P_REC)continue;
            choose(s,p);
            for(int edge:{0,1}) {
                s.set(p,q15(edge?32767:(s.unit->params[p].bipolar?-32767:0)));
                s.capture();open(s);draw(s);close();
            }
        }
    }
}
int divisor(UnitKind kind,int p) {
    if(kind==UnitKind::Shift && p==ShiftUnit::P_SHIFT)return 32;
    if((kind==UnitKind::Osc && p==OscUnit::P_PITCH) ||
       (kind==UnitKind::Fm && (p==FmUnit::P_PITCH || p==FmUnit::P_RATIO)) ||
       (kind==UnitKind::Bird && p==BirdUnit::P_PITCH) ||
       (kind==UnitKind::Inst && p==InstUnit::P_NOTE) ||
       (kind==UnitKind::Ghost && p==GhostUnit::P_TONE) ||
       (kind==UnitKind::Filter && p==FilterUnit::P_CUTOFF) ||
       (kind==UnitKind::Ring && p==RingUnit::P_FREQ) ||
       (kind==UnitKind::Tilt && p==TiltUnit::P_FREQ) ||
       (kind==UnitKind::Motor && p==MotorUnit::P_RPM) ||
       (kind==UnitKind::SamplePlayer && p==PlayUnit::P_SPEED) ||
       (kind==UnitKind::Grain && p==GrainUnit::P_PITCH) ||
       (kind==UnitKind::Fract && p==FractUnit::P_SPEED))return 16;
    return 8;
}
void gesture_case(Scene& s,int p,bool depth) {
    choose(s,p);
    auto& q=s.unit->params[p];q.base=q.bipolar?0:12000;q.mod_depth=3000;
    q.src=ModSource::SigilC;q.mod_enabled=false;q.snap();s.capture();open(s);
    const auto before=parameters(s);const int y=depth?147:102;
    const int span=depth||q.bipolar?65534:32767;const int fine=depth?8:divisor(s.kind,p);
    const q15 start=depth?q.mod_depth:q.base;
    const auto value=[&](){return depth?q.mod_depth:q.base;};
    const int dirty=mutations;down(137,y);CHECK(value()==start && mutations==dirty);
    move(137,y-18);CHECK(value()==start && mutations==dirty); // vertical motion is inert
    move(144,y);CHECK(value()==start+7*span/271);
    const q15 coarse=value();hid::frame(KEY_R,KEY_L|KEY_R|KEY_TOUCH);
    move(144,y,KEY_L|KEY_R);CHECK(value()==coarse); // R edge must rebase, never jump
    move(152,y,KEY_L|KEY_R);CHECK(value()==coarse+8*span/(271*fine));
    const q15 fine_value=value();hid::frame(0,KEY_L|KEY_TOUCH,KEY_R);
    move(152,y);CHECK(value()==fine_value);
    move(147,y);CHECK(value()==fine_value-5*span/271);
    up();const q15 saved=value();move(210,y);CHECK(value()==saved);up();
    for(int other=0;other<s.unit->num_params;++other)if(other!=p) {
        CHECK(s.unit->params[other].base==before[other].base && s.unit->params[other].mod_depth==before[other].mod_depth);
    }
    CHECK(q.src==ModSource::SigilC && !q.mod_enabled); // depth changes never invent/unmute a cable
    CHECK(depth?q.base==before[p].base:q.mod_depth==before[p].mod_depth);
    CHECK(!s.engine->param_temp_active());close();++gestures;
}
void gestures_group() {
    for(const auto& entry:catalogue) {
        name=std::string(entry.name)+" relative edit";Scene s(entry.kind);
        gesture_case(s,selected(s),false);gesture_case(s,selected(s),true);
    }
    const std::pair<UnitKind,int> extra[]={
        {UnitKind::Osc,OscUnit::P_PITCH},{UnitKind::Fm,FmUnit::P_RATIO},
        {UnitKind::Filter,FilterUnit::P_CUTOFF},{UnitKind::Grain,GrainUnit::P_PITCH},
        {UnitKind::Tilt,TiltUnit::P_FREQ},{UnitKind::Fract,FractUnit::P_SPEED},
        {UnitKind::Inst,InstUnit::P_NOTE}};
    for(auto entry:extra){Scene s(entry.first);name=std::string(item(entry.first).name)+" alternate divisor";gesture_case(s,entry.second,false);}
    Scene s(UnitKind::Filter);name="half-open hit edges";open(s);auto& q=s.unit->params[selected(s)];
    for(auto point:{std::pair<int,int>{13,102},{306,102},{160,117},{13,147},{306,147},{160,166}}) {
        const auto before=parameters(s);down(point.first,point.second);move(240,point.second);up();same_controls(s,before);
    }
    for(int index:{0,9}) {
        q.src=ModSource::SigilD;tap(8+31*index,177);
        CHECK(q.src==(index?ModSource::EnvFollow:ModSource::None));
        tap(8+31*(index+1)-1,200);CHECK(q.src==(index?ModSource::EnvFollow:ModSource::None));
    }
    q.src=ModSource::SigilD;tap(318,188);CHECK(q.src==ModSource::SigilD);close();
}
void sources_group() {
    for(const auto& entry:catalogue) {
        name=std::string(entry.name)+" sources";Scene s(entry.kind);const int p=selected(s);auto& q=s.unit->params[p];
        q.base=q.bipolar?0:12000;q.mod_depth=-7890;q.snap();open(s);const q15 base=q.base;
        for(int i=0;i<10;++i) {
            const ModSource expected=i==0?ModSource::None:i==9?ModSource::EnvFollow:sigil_mod_source(i-1);
            q.mod_enabled=false;tap(8+i*31+15,188);
            CHECK(q.src==expected && q.mod_enabled && q.base==base && q.mod_depth==-7890);
            const int dirty=mutations;tap(8+i*31+15,188);CHECK(mutations==dirty);++routes;
        }
        tap(54,188);CHECK(q.src==ModSource::SigilA);tap(284,59);
        CHECK(!q.mod_enabled && q.src==ModSource::SigilA && q.mod_depth==-7890);
        tap(284,59);CHECK(q.mod_enabled && q.src==ModSource::SigilA);
        tap(23,188);const int dirty=mutations;tap(284,59);
        CHECK(q.src==ModSource::None && q.mod_depth==-7890 && mutations==dirty);
        s.capture();draw(s);close();
    }
    Scene s(UnitKind::Filter);name="env chain / response";open(s);tap(300,188);
    auto& q=s.unit->params[selected(s)];CHECK(q.src==ModSource::EnvFollow);
    for(int i=0;i<4;++i){tap(100+i*32,214);CHECK(s.engine->env_follow_source()==i);}
    for(int i=0;i<3;++i){const int old=int(s.engine->env_follow_response());tap(265,214);CHECK(int(s.engine->env_follow_response())==(old+1)%3);}
    s.capture();draw(s,true);tap(54,188);
    const int chain=s.engine->env_follow_source();const auto response=s.engine->env_follow_response();
    tap(100,214);tap(265,214);CHECK(s.engine->env_follow_source()==chain && s.engine->env_follow_response()==response);close();
}
void ownership_group() {
    for(bool same_edge:{false,true}) {
        Scene s(UnitKind::Osc);name="held L entry/release touch quarantine";const auto before=parameters(s);
        hid::touch={100,148};if(!same_edge)hid::frame(KEY_TOUCH,KEY_TOUCH);
        auto result=hid::frame(KEY_L|KEY_X|KEY_A|(same_edge?u32(KEY_TOUCH):0u),KEY_L|KEY_TOUCH);
        CHECK(result.owned && result.skip && !result.remaining && param_lens_is_open());
        move(295,102);same_controls(s,before);CHECK(g_drag==Drag::None);
        up();down(200,102);move(207,102);const q15 changed=s.unit->params[selected(s)].base;
        CHECK(changed!=before[selected(s)].base);
        result=hid::frame(KEY_X|KEY_A|KEY_R,KEY_TOUCH,KEY_L);
        CHECK(result.skip && !result.owned && !result.remaining && !param_lens_is_open());
        const auto saved=parameters(s);for(int i=0;i<3;++i){hid::touch={170,148};CHECK(hid::frame(i?0u:u32(KEY_TOUCH),KEY_TOUCH).skip);same_controls(s,saved);}
        hid::frame(0,0,KEY_TOUCH);CHECK(!s.engine->param_temp_active());++ownership;
    }
    for(u32 action:{u32(KEY_SELECT),u32(KEY_START),u32(KEY_Y),u32(KEY_B)}) {
        Scene s(UnitKind::Osc);name="global modal escape";open(s);
        const auto result=hid::frame(action|KEY_X|KEY_A|KEY_TOUCH,KEY_L|KEY_TOUCH);
        CHECK(result.skip && !param_lens_is_open());CHECK(result.remaining==(action==KEY_B?0:action));
        hid::frame(0,KEY_L);CHECK(!param_lens_is_open());hid::frame(0,0);++ownership;
    }
    for(int reason=0;reason<7;++reason) {
        Scene s(UnitKind::Osc);name="context cancellation";open(s);const auto before=parameters(s);
        if(reason==0)g_view=View::Performance;
        if(reason==1)g_view=View::Sigils;
        if(reason==2)g_view=View::Sequencer;
        if(reason==3)g_overlay=Overlay::None;
        if(reason==4)g_seq_lock_armed=true;
        if(reason==5)g_overlay=Overlay::ConfirmDiscard;
        if(reason==6)hid::nav_opened=true;
        const auto result=hid::frame(KEY_X|KEY_TOUCH,KEY_L|KEY_TOUCH);
        CHECK(result.skip && !param_lens_is_open());same_controls(s,before);
        hid::nav_opened=false;hid::frame(0,0);++ownership;
    }
    for(u32 direction:{u32(KEY_UP),u32(KEY_DOWN),u32(KEY_LEFT),u32(KEY_RIGHT)})for(bool simultaneous:{false,true}) {
        Scene s(UnitKind::Osc);name="old L+arrows ownership";
        const int chain=direction==KEY_UP?NUM_CHAINS-1:direction==KEY_DOWN?1:0;
        const int slot=direction==KEY_LEFT?UNITS_PER_CHAIN-1:direction==KEY_RIGHT?1:0;
        delete s.engine->replace_unit(chain,slot,new OscUnit);
        if(!simultaneous)open(s);
        const auto result=hid::frame(direction|(simultaneous?u32(KEY_L):0u),KEY_L|direction);
        CHECK(!result.owned && !param_lens_is_open() && g_edit_chain==chain && g_edit_slot==slot);
        hid::frame(0,KEY_L);CHECK(!param_lens_is_open());hid::frame(0,0);++ownership;
    }
    {
        Scene s(UnitKind::Osc);name="replacement while editing";open(s);down(150,102);
        Unit* retained=s.engine->replace_unit(0,s.slot,new FilterUnit);
        auto* replacement=s.engine->chains[0].units[s.slot];const q15 value=replacement->params[0].base;
        const auto result=hid::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
        CHECK(result.skip && !param_lens_is_open() && replacement->params[0].base==value);
        delete s.engine->replace_unit(0,s.slot,retained);hid::frame(0,0);++ownership;
    }
    {
        Scene s(UnitKind::Osc);name="latched footer / keyboard select / B return";open(s,false);
        const int p=selected(s);down(150,102,0);move(157,102,0);up(0);
        const q15 stored=s.unit->params[p].base;CHECK(param_lens_is_open());
        down(140,102,0);hid::frame(KEY_RIGHT,KEY_TOUCH);
        CHECK(param_lens_is_open() && selected(s)!=(p));const auto saved=parameters(s);
        move(220,102,0);same_controls(s,saved);up(0);tap(280,232,0);CHECK(param_lens_is_open());
        hid::frame(KEY_L,KEY_L);hid::frame(0,0,KEY_L);CHECK(param_lens_is_open());
        const auto result=hid::frame(KEY_B,0);CHECK(result.skip && result.remaining==0 && !param_lens_is_open());
        CHECK(g_overlay==Overlay::Editor && s.unit->params[p].base==stored);++ownership;
    }
    for(UnitKind kind:{UnitKind::Reverb,UnitKind::Ghost,UnitKind::Stranular}) {
        Scene s(kind);name=std::string(item(kind).name)+" latched selected top sync";open(s,false);
        const int old=selected(s);hid::frame(KEY_RIGHT,0);
        CHECK(selected(s)==(old+1)%s.unit->num_params);s.capture();const auto active=draw(s,true);
        hid::frame(KEY_B,0);CHECK(!param_lens_is_open());s.capture();const auto ordinary=draw(s);
        CHECK(active.top==ordinary.top);++ownership;
    }
    // Old arrows/route-caption/fine/reset/header targets are passive; never let
    // their hidden module controls steal input below the transparent paper.
    for(const auto& entry:catalogue) {
        Scene s(entry.kind);name=std::string(entry.name)+" hidden hit quarantine";open(s);
        const auto before=parameters(s);const int p=selected(s);const bool bypass=s.unit->bypass;
        for(auto point:{std::pair<int,int>{20,47},{156,47},{210,10},{258,10},{100,68},{225,70},{225,108},{286,108},{286,154},{3,150},{318,150},{250,232},{4,232}}) {
            tap(point.first,point.second);same_controls(s,before);CHECK(selected(s)==p && s.unit->bypass==bypass && param_lens_is_open());
        }
        // A hidden old tap-fine hit must not leave an invisible mode change.
        s.unit->params[p].base=12000;down(140,102);move(147,102);up();
        CHECK(s.unit->params[p].base==12000+7*(s.unit->params[p].bipolar?65534:32767)/271);
        close();++ownership;
    }
}
void secondary_group() {
    // The approved minimal surface retains R fine only: retired tap-fine,
    // reset and footer-return touch regions are intentionally inert (group 4).
    // REC's existing pending/active-recording base lock must remain in force.
    name="busy recorder base locked / depth retained";Scene s(UnitKind::Recorder);
    auto& rec=*static_cast<RecUnit*>(s.unit);s.set(RecUnit::P_LEN,12000);
    // Scene is a drawing fixture, not a prepared REC workspace. Use the actual
    // command's plan/stage/commit path to obtain private mono capacity first.
    s.engine->launch_quantize=LaunchQuantize::Beat;
    recorder_focus_command(rec,RecorderFocusAction::Record);
    CHECK(s.engine->recorder_record_pending(0,s.slot));open(s);const q15 length=rec.params[RecUnit::P_LEN].base;
    down(150,102);move(210,102);up();CHECK(rec.params[RecUnit::P_LEN].base==length);
    const q15 old_depth=rec.params[RecUnit::P_LEN].mod_depth;
    down(150,147);move(160,147);up();CHECK(rec.params[RecUnit::P_LEN].mod_depth==old_depth+10*65534/271);
    s.capture();draw(s,true);close();
    CHECK(s.engine->request_recorder_record(0,s.slot,false,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);
    CHECK(s.engine->request_recorder_record(0,s.slot,true,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);
    CHECK(rec.is_recording());open(s);down(150,102);move(210,102);up();CHECK(rec.params[RecUnit::P_LEN].base==length);
    s.capture();draw(s,true);close();
    CHECK(s.engine->request_recorder_record(0,s.slot,false,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);
}
int run() {
    fixture::sample_fixture();int total=0,failed=0;
    const auto group=[&](const char* title,auto test) {++total;try {test();std::printf("ok: minimal mod %s\n",title);}
        catch(const std::exception& e){++failed;close();std::fprintf(stderr,"FAIL: minimal mod %s [%s]: %s\n",title,name.c_str(),e.what());}};
    group("28 native module backgrounds / ordinary tops / themes / extrema / detached frozen draw",compositions);
    group("relative base-depth / no-jump / fine divisors / stationary R rebasing / no accidental unmute",gestures_group);
    group("all ten sources / same-source unmute / retained cable toggle / off / ENV",sources_group);
    group("actual main modal ownership / L quarantine / escapes / identity / latched / hidden hits",ownership_group);
    group("retired secondary touch affordances / busy REC base lock",secondary_group);
    theme_test::select();
    std::printf("minimal mod UI: %d/%d groups; %llu duals; %llu matched backgrounds; %llu detached pairs; %llu gestures; %llu source assignments; %llu ownership cases; %llu exact veil pixels; shared peak %d/8192\n",
        total-failed,total,(unsigned long long)duals,(unsigned long long)backgrounds,(unsigned long long)detached,
        (unsigned long long)gestures,(unsigned long long)routes,(unsigned long long)ownership,(unsigned long long)veil_pixels,peak);
    std::puts("focused actual C++ host UI only; zero inherited regression groups; no hardware/FPS/sound/deployment claims");
    return failed?1:0;
}
void save(const std::filesystem::path& out,std::ofstream& manifest,Scene& s,const std::string& frame) {
    name=frame;s.capture();const auto images=draw(s);
    width=400;pixels=images.top;texts=images.top_text;dump_surface(out,frame+"_top");
    width=320;pixels=images.bottom;texts=images.bottom_text;dump_surface(out,frame+"_bottom");
    const int p=selected(s);const auto& q=s.unit->params[p];
    manifest<<frame<<'\t'<<item(s.kind).name<<'\t'<<p<<'\t'<<q.base<<'\t'<<q.effective<<'\t'<<q.mod_depth<<'\t'<<int(q.src)<<'\t'<<q.mod_enabled<<'\t'
            <<s.engine->transport_frame<<'\t'<<images.upper<<'\t'<<images.lower<<'\t'<<images.upper+images.lower<<'\n';
}
int capture(const char* directory) {
    try {
        const std::filesystem::path out(directory);CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
        fixture::sample_fixture();host_time_ms=1000;
        std::ofstream manifest(out/"manifest.tsv");manifest<<"frame\tmodule\tparam\tbase\teffective\tdepth\tsource\tenabled\ttransport_frame\ttop\tbottom\tshared\n";
        unsigned count=0;
        for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink})for(UnitKind kind:{UnitKind::Filter,UnitKind::Reverb}) {
            palette::set(theme);Scene s(kind);
            int p=kind==UnitKind::Filter?int(FilterUnit::P_CUTOFF):int(ReverbUnit::P_MIX);
            if(kind==UnitKind::Filter){s.set(FilterUnit::P_CUTOFF,12000);s.set(FilterUnit::P_RES,6000);s.set(FilterUnit::P_DRIVE,5000);s.set(FilterUnit::P_MORPH,12000);}
            else{s.set(ReverbUnit::P_SIZE,18000);s.set(ReverbUnit::P_DECAY,20000);s.set(ReverbUnit::P_DAMP,9000);s.set(ReverbUnit::P_MIX,9000);}
            s.select(p);auto& q=s.unit->params[p];q.src=ModSource::SigilB;q.mod_depth=11000;q.mod_enabled=true;q.snap();
            for(int i=0;i<SIGIL_POINTS;++i)s.engine->sigils[1].set_point(i,q15(19000));
            auto cfg=s.engine->sigil_config[1];cfg.enabled=true;cfg.legacy=true;s.engine->set_sigil_config(1,cfg);s.engine->set_sigil_sync(1,false);s.engine->reset_sigil(1);
            s.render(4096);s.capture();const std::string prefix=std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+item(kind).name;
            save(out,manifest,s,prefix+"_ordinary");++count;open(s);save(out,manifest,s,prefix+"_held");++count;close();
        }
        palette::set(palette::Theme::Graphite);
        for(UnitKind kind:{UnitKind::Ghost,UnitKind::Stranular,UnitKind::Fm,UnitKind::Recorder,UnitKind::Inst,UnitKind::Shift}) {
            Scene s(kind);s.signal(24000);open(s);save(out,manifest,s,std::string("graphite_")+item(kind).name+"_held");++count;close();
        }
        {Scene s(UnitKind::Filter);open(s);tap(300,188);tap(164,214);save(out,manifest,s,"graphite_filter_env");++count;tap(284,59);save(out,manifest,s,"graphite_filter_muted_env");++count;tap(23,188);save(out,manifest,s,"graphite_filter_off");++count;close();}
        {Scene s(UnitKind::Osc);open(s,false);save(out,manifest,s,"graphite_osc_latched");++count;close();}
        manifest.close();std::ofstream receipt(out/"capture.json");
        receipt<<"{\n  \"pairs\": "<<count<<", \"regression_groups_run\": 0, \"shared_peak\": "<<peak<<", \"budget\": 8192,\n"
               <<"  \"provenance\": \"fresh actual current C++ editor/top/engine through native host raster; no authored UI or reused PNG pixels\",\n"
               <<"  \"hardware\": false, \"audio\": \"private engine scalars only; PCM discarded\"\n}\n";
        std::printf("minimal mod captures: %u fresh native pairs; 0 regression groups; shared peak %d/8192\n",count,peak);return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"minimal mod capture [%s]: %s\n",name.c_str(),e.what());return 1;}
}
} // namespace
} // namespace mod_minimal_cases
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--proof-only")return mod_minimal_cases::run();
    if(argc==3 && std::string(argv[1])=="--capture-only")return mod_minimal_cases::capture(argv[2]);
    std::fprintf(stderr,"usage: mod-minimal-ui --proof-only | --capture-only fresh-output-directory\n");return 2;
}
