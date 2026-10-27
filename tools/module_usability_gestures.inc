// Appended to the private actual-main/UI driver; no inherited suite is invoked.
namespace usability_gestures {
namespace input=osc_input_main;
using Scene=fader_lancet_cases::Scene;
uint64_t gestures=0,target_cases=0;
namespace {
using D=DelayUnit;
using C=ChorusUnit;
using G=GhostUnit;
using Values=std::array<q15,MAX_PARAMS>;
Values bases(const Scene& s){Values out{};for(int p=0;p<s.unit->num_params;++p)out[p]=s.unit->params[p].base;return out;}
void down(int x,int y){++gestures;input::touch={x,y};input::frame(KEY_TOUCH,KEY_TOUCH);}
void move(int x,int y){input::touch={x,y};input::frame(0,KEY_TOUCH);}
void up(){input::frame(0,0,KEY_TOUCH);}
void tap(int x,int y){down(x,y);up();}
void neutral(){
    delay_focus_release();chorus_focus_release();ghost_focus_release();param_lens_cancel();
    input::nav_opened=false;g_drag=Drag::None;input::frame(0,0);
}
void select(Scene& s,int p){
    if(s.kind==UnitKind::Delay)delay_focus_select(p);
    else if(s.kind==UnitKind::Chorus)chorus_focus_select(p);
    else ghost_focus_select(p);
}
void capture(Scene& s){s.capture();}
void base_context(Scene& s){
    g_eng=s.engine.get();g_audio=&s.audio;g_view=View::Chains;g_overlay=Overlay::Editor;
    g_edit_chain=g_sel_chain=0;g_edit_slot=g_sel_slot=s.slot;g_seq_lock_armed=false;
}
void cancellations(Scene& s,int p,int x,int y,int dx,int dy){
    // Each mismatch is observed while held, then restored. Restoring even the
    // old Drag token cannot resurrect a cancelled private gesture.
    for(int reason=0;reason<11;++reason){
        base_context(s);neutral();s.set(p,12000);capture(s);down(x,y);
        const Drag token=g_drag;const auto before=bases(s);CHECK(token!=Drag::None);
        std::unique_ptr<Engine> other;
        std::unique_ptr<Unit> replacement;
        Unit* parked=nullptr;
        if(reason==0)g_overlay=Overlay::None;
        if(reason==1)g_view=View::Sigils;
        if(reason==2)g_edit_chain=1;
        if(reason==3)g_edit_slot=(s.slot+1)%UNITS_PER_CHAIN;
        if(reason==4)g_drag=Drag::None;
        if(reason==5)s.engine->request_pattern(1,false);
        if(reason==6){replacement.reset(make_unit(s.kind));parked=s.engine->replace_unit(0,s.slot,replacement.release());}
        if(reason==7){
            other=std::make_unique<Engine>();
            std::swap(other->chains[0].units[s.slot],s.engine->chains[0].units[s.slot]);g_eng=other.get();
        }
        if(reason==8)select(s,(p+1)%s.unit->num_params);
        if(reason==9){input::nav_opened=true;input::frame(0,KEY_TOUCH);}
        if(reason==10){input::frame(KEY_L,KEY_L|KEY_TOUCH);CHECK(param_lens_is_open());}
        capture(s);
        if(reason==6)delete s.engine->replace_unit(0,s.slot,parked);
        if(reason==7){g_eng=s.engine.get();std::swap(other->chains[0].units[s.slot],s.engine->chains[0].units[s.slot]);}
        if(reason==5)s.engine->request_pattern(0,false);
        input::nav_opened=false;param_lens_cancel();base_context(s);
        g_drag=token;move(x+dx,y+dy);up();CHECK(bases(s)==before);
    }
    // Physical modal/navigation keys cancel before the release path too.
    for(u32 key:{u32(KEY_SELECT),u32(KEY_START),u32(KEY_Y),u32(KEY_B)}){
        base_context(s);neutral();s.set(p,12000);capture(s);down(x,y);const auto before=bases(s);
        input::frame(key,KEY_TOUCH);move(x+dx,y+dy);up();CHECK(bases(s)==before);
    }
    base_context(s);neutral();capture(s);
}
void lock_cancellation(Scene& s,int p,int x,int y,int dx,int dy){
    base_context(s);neutral();s.engine->clear_track(0);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const auto before=bases(s);
    for(int reason=0;reason<5;++reason){
        CHECK(s.engine->set_step_lock(0,7,s.slot,p,12000));capture(s);down(x,y);const Drag token=g_drag;
        CHECK(token!=Drag::None);
        if(reason==0)g_seq_lock_step=8;
        if(reason==1)g_seq_lock_chain=1;
        if(reason==2)g_seq_lock_armed=false;
        if(reason==3)s.engine->request_pattern(1,false);
        if(reason==4)g_view=View::Chains;
        capture(s);
        if(reason==3)s.engine->request_pattern(0,false);
        g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
        g_drag=token;move(x+dx,y+dy);up();
        CHECK(seq_display_value(s.slot,p,0)==12000 && bases(s)==before);
    }
    base_context(s);neutral();capture(s);
}
void stationary(Scene& s,int p,int x,int y,int dx,int dy,q15 delta){
    neutral();s.set(p,14000);capture(s);const auto before=bases(s);
    down(x,y);capture(s);move(x,y);CHECK(bases(s)==before);
    move(x+dx,y+dy);CHECK(s.unit->params[p].base==14000+delta);
    move(x,y);CHECK(bases(s)==before);up();
    down(x,y);s.unit->params[p].base=23001;move(x,y);capture(s);up();
    CHECK(s.unit->params[p].base==23001);
    down(x,y);move(x+dx,y+dy);s.unit->params[p].base=24002;
    move(x+dx,y+dy);up();CHECK(s.unit->params[p].base==24002);
}
void tap_box(Scene& s,int p,int x,int y,int w,int h,q15 origin,q15 result){
    for(int xx:{x,x+w/2,x+w-1})for(int yy:{y,y+h/2,y+h-1}){
        neutral();s.set(p,origin);capture(s);const auto before=bases(s);const int dirty=mutations;
        down(xx,yy);capture(s);CHECK(bases(s)==before && mutations==dirty);
        move(xx,yy);CHECK(bases(s)==before);up();
        auto expected=before;expected[p]=result;CHECK(bases(s)==expected);++target_cases;
    }
}
void delay_taps(){
    Scene s(UnitKind::Delay);auto& u=*static_cast<D*>(s.unit);CHECK(u.modern());neutral();
    tap_box(s,D::P_HOLD,184,0,56,25,0,32767);
    tap_box(s,D::P_HOLD,184,0,56,25,32767,0);
    tap_box(s,D::P_SYNC,8,204,46,24,32767,0);
    tap_box(s,D::P_SYNC,54,204,54,24,0,32767);
    tap_box(s,D::P_MODE,148,204,52,24,32767,0);
    tap_box(s,D::P_MODE,200,204,50,24,0,32767);
    s.set(D::P_SYNC,32767);const int dirty=mutations;tap(72,214);CHECK(mutations==dirty);
    // Moved too far, out of target, out-and-back: all stay cancelled until a new down.
    for(int reason=0;reason<4;++reason){
        s.set(D::P_SYNC,0);capture(s);down(75,215);const auto before=bases(s);
        if(reason==0)move(84,215);
        if(reason==1)move(75,224);
        if(reason==2)move(75,228);
        if(reason==3)move(53,215);
        move(75,215);up();CHECK(bases(s)==before);
    }
    s.set(D::P_SYNC,0);down(75,215);move(78,218);up();CHECK(u.params[D::P_SYNC].base==32767);
    s.set(D::P_HOLD,0);down(210,10);u.params[D::P_HOLD].base=9000;up();CHECK(u.params[D::P_HOLD].base==9000);
    s.set(D::P_HOLD,0);down(210,10);u.params[D::P_HOLD].base=9000;capture(s);
    u.params[D::P_HOLD].base=0;move(210,10);up();CHECK(u.params[D::P_HOLD].base==0);
    // Main real-lift commit also works for a stationary injected legacy p-lock.
    s.engine->clear_track(0);g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(s.engine->set_step_lock(0,7,s.slot,D::P_HOLD,0));const auto before=bases(s);
    down(210,10);CHECK(seq_display_value(s.slot,D::P_HOLD,-1)==0);up();
    CHECK(seq_display_value(s.slot,D::P_HOLD,-1)==32767 && bases(s)==before);
    base_context(s);neutral();cancellations(s,D::P_HOLD,210,10,0,0);lock_cancellation(s,D::P_HOLD,210,10,0,0);
    // Legacy receives HOLD convenience only; inactive modern controls remain inert.
    CHECK(u.set_modern(false));capture(s);s.set(D::P_HOLD,0);down(210,10);CHECK(u.params[D::P_HOLD].base==0);
    up();CHECK(u.params[D::P_HOLD].base==32767 && !u.modern());
    const auto legacy=bases(s);for(int x:{18,70,155,210,280})tap(x,213);CHECK(bases(s)==legacy && !u.modern());
}
void delay_clear_and_rows(){
    Scene s(UnitKind::Delay);auto& u=*static_cast<D*>(s.unit);neutral();
    const struct {int x,p;} rows[]={{32,D::P_MIX},{106,D::P_TONE},{180,D::P_WOW},{265,D::P_SAT}};
    for(const auto& r:rows){
        s.set(r.p,14000);capture(s);down(r.x,203);move(r.x+6,203);up();
        CHECK(u.params[r.p].base==14000+6*32767/144 && !param_lens_is_open());
    }
    stationary(s,D::P_TIME,50,50,6,0,q15(6*32767/144));
    const auto before=bases(s);const int dirty=mutations;
    for(int reason=0;reason<4;++reason){
        u.reset();capture(s);g_ui_frame=1000;down(280,216);
        if(reason==0){g_ui_frame+=44;move(280,216);up();}
        if(reason==1){move(280,228);move(280,216);g_ui_frame+=46;move(280,216);up();}
        if(reason==2){g_drag=Drag::None;capture(s);g_drag=Drag::ParamBase;g_ui_frame+=46;move(280,216);up();}
        if(reason==3){input::nav_opened=true;input::frame(0,KEY_TOUCH);input::nav_opened=false;g_ui_frame+=46;move(280,216);up();}
        CHECK(!u.visual_state().clearing);
    }
    u.reset();g_ui_frame=2000;down(280,204);g_ui_frame+=44;move(280,204);CHECK(!u.visual_state().clearing);
    ++g_ui_frame;move(280,204);CHECK(u.visual_state().clearing);up();
    CHECK(bases(s)==before && mutations==dirty);
    u.reset();tap(280,228);CHECK(param_lens_is_open() && !u.visual_state().clearing);neutral();
}
void chorus_controls(){
    Scene s(UnitKind::Chorus);auto& u=*static_cast<C*>(s.unit);neutral();
    stationary(s,C::P_DEPTH,68,192,0,-16,q15(16*32767/160));
    stationary(s,C::P_MIX,299,140,0,-16,q15(16*32767/160));
    stationary(s,C::P_RATE,130,43,6,0,q15(6*32767/121));
    for(int p:{C::P_DEPTH,C::P_MIX}){
        const int x=p==C::P_DEPTH?68:299,y=200;
        for(q15 origin:{q15(0),q15(17000),q15(32767)}){
            s.set(p,origin);capture(s);down(x,y);move(x,y-160);
            CHECK(u.params[p].base==32767);move(x,y);CHECK(u.params[p].base==origin);
            move(x,239);CHECK(u.params[p].base==std::max(0,int(origin)+(y-239)*32767/160));
            move(x,y);CHECK(u.params[p].base==origin);up();
        }
        // Horizontal noise on a vertical control must not reapply its origin.
        s.set(p,12000);down(x,y);u.params[p].base=23456;move(x-3,y);up();CHECK(u.params[p].base==23456);
    }
    s.engine->clear_track(0);g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(s.engine->set_step_lock(0,7,s.slot,C::P_DEPTH,10000));const auto before=bases(s);
    down(68,192);move(68,176);up();CHECK(seq_display_value(s.slot,C::P_DEPTH,0)==10000+16*32767/160 && bases(s)==before);
    base_context(s);neutral();cancellations(s,C::P_DEPTH,68,192,0,-16);lock_cancellation(s,C::P_MIX,299,140,0,-16);
    // Fixed rectangle boundaries are still accepted regardless of stored endpoints.
    const struct {int x,y,w,h,p;} boxes[]={{10,28,231,31,C::P_RATE},{8,170,79,47,C::P_DEPTH},
        {278,96,42,73,C::P_MIX},{246,170,70,47,C::P_MIX}};
    for(q15 v:{q15(0),q15(32767)})for(const auto& b:boxes)for(int x:{b.x,b.x+b.w-1})for(int y:{b.y,b.y+b.h-1}){
        s.set(b.p,v);capture(s);const auto old=bases(s);down(x,y);CHECK(chorus_focus_selected()==b.p);up();CHECK(bases(s)==old);++target_cases;
    }
}
struct GhostBox {int p,x,y,w,h;};
constexpr GhostBox GHOST_BOXES[]={
    {G::P_TONE,82,42,158,20},{G::P_FINE,82,62,131,22},{G::P_DECAY,20,86,62,98},
    {G::P_DRIFT,242,43,26,70},{G::P_FREEZE,130,96,62,46},{G::P_MIX,270,112,50,89},
    {G::P_SMEAR,92,169,148,34},{G::P_RATE,54,203,202,25}
};
void ghost_corridors(){
    Scene s(UnitKind::Ghost);auto& u=*static_cast<G*>(s.unit);neutral();
    for(size_t i=0;i<std::size(GHOST_BOXES);++i)for(size_t j=i+1;j<std::size(GHOST_BOXES);++j){
        const auto& a=GHOST_BOXES[i];const auto& b=GHOST_BOXES[j];
        CHECK(a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y);
    }
    for(q15 value:{q15(0),q15(32767)})for(const auto& b:GHOST_BOXES){
        for(int x:{b.x,b.x+b.w/2,b.x+b.w-1})for(int y:{b.y,b.y+b.h/2,b.y+b.h-1}){
            for(int p=0;p<u.num_params;++p)s.set(p,p==G::P_FREEZE?0:value);
            capture(s);const auto before=bases(s);down(x,y);CHECK(ghost_focus_selected()==b.p);up();
            auto expected=before;if(b.p==G::P_FREEZE)expected[b.p]=32767;
            CHECK(bases(s)==expected);++target_cases;
        }
    }
    for(const auto point:{std::pair<int,int>{81,54},{240,54},{241,76},{268,90},{269,130},{90,185},{255,202},{125,120}}){
        s.set(G::P_FREEZE,0);capture(s);const auto before=bases(s);down(point.first,point.second);move(point.first+2,point.second);up();
        CHECK(bases(s)==before && !param_lens_is_open());++target_cases;
    }
    // Every visible rail endpoint belongs to its own stable corridor.
    const struct {int p,x0,y0,x1,y1;} rails[]={{G::P_TONE,82,54,232,54},{G::P_FINE,90,76,210,76},
        {G::P_DECAY,61,92,61,174},{G::P_DRIFT,254,59,254,111},{G::P_MIX,294,116,294,181},
        {G::P_SMEAR,111,183,225,183},{G::P_RATE,64,220,250,220}};
    for(const auto& r:rails)for(int endpoint=0;endpoint<2;++endpoint){
        s.set(G::P_FREEZE,0);s.set(r.p,endpoint?0:32767);capture(s);
        down(endpoint?r.x1:r.x0,endpoint?r.y1:r.y0);CHECK(ghost_focus_selected()==r.p);up();++target_cases;
    }
    s.set(G::P_FREEZE,0);stationary(s,G::P_DECAY,61,130,0,-8,q15(8*32767/82));
    stationary(s,G::P_SMEAR,170,183,8,0,q15(8*32767/114));
    stationary(s,G::P_DRIFT,254,85,0,-8,q15(8*32767/52));
    stationary(s,G::P_MIX,294,150,0,-8,q15(8*32767/65));
    stationary(s,G::P_FINE,150,76,8,0,512);
    stationary(s,G::P_RATE,150,220,8,0,q15(8*32767/186));
    cancellations(s,G::P_FINE,150,76,8,0);lock_cancellation(s,G::P_FINE,150,76,8,0);
}
void ghost_existing(){
    Scene s(UnitKind::Ghost);auto& u=*static_cast<G*>(s.unit);neutral();s.set(G::P_FREEZE,0);s.set(G::P_FINE,10000);
    tap(220,73);CHECK(u.params[G::P_FINE].base==0 && ghost_focus_selected()==G::P_FINE);
    s.set(G::P_TONE,14000);capture(s);
    const auto note=[&](){return int(std::lround(69+12*std::log2(G::root_millihz(u.params[G::P_TONE].base,0)/440000.0)));};
    const int initial=note();tap(200,30);CHECK(note()==initial+1);tap(100,30);CHECK(note()==initial);
    // Preference toggle never retunes; one mode has 6px musical steps, the other raw travel.
    s.set(G::P_TONE,14000);const q15 tone=u.params[G::P_TONE].base;tap(150,10);CHECK(u.params[G::P_TONE].base==tone);
    down(150,54);move(151,54);const q15 first=u.params[G::P_TONE].base;move(150,54);up();CHECK(u.params[G::P_TONE].base==tone);
    tap(150,10);down(150,54);move(151,54);const q15 second=u.params[G::P_TONE].base;move(150,54);up();
    CHECK((first==tone && second==tone+32767/150) || (second==tone && first==tone+32767/150));
    const bool long_tail=u.long_tail;tap(260,10);CHECK(u.long_tail!=long_tail);tap(260,10);CHECK(u.long_tail==long_tail);
    s.set(G::P_MIX,12000);tap(160,117);CHECK(u.params[G::P_FREEZE].base==32767);s.set(G::P_FREEZE,32767);capture(s);
    const auto held=bases(s);down(254,85);move(254,70);up();CHECK(bases(s)==held);
    down(294,150);move(294,140);up();CHECK(u.params[G::P_MIX].base!=held[G::P_MIX]);
    tap(160,117);CHECK(u.params[G::P_FREEZE].base==0);s.set(G::P_FREEZE,0);
    // A newly installed/current owner must not borrow a stale frozen mailbox.
    g_ui_snapshot.effect_focus_valid=true;g_ui_snapshot.effect_focus_kind=uint8_t(UnitKind::Ghost);
    g_ui_snapshot.effect_focus_chain=0;g_ui_snapshot.effect_focus_slot=s.slot;g_ui_snapshot.focus_effective[G::P_FREEZE]=32767;
    s.set(G::P_FINE,12000);down(150,76);move(158,76);up();CHECK(u.params[G::P_FINE].base==12512);
    s.engine->clear_track(0);g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(s.engine->set_step_lock(0,7,s.slot,G::P_FINE,10000));const auto before=bases(s);
    down(150,76);move(158,76);up();CHECK(seq_display_value(s.slot,G::P_FINE,0)==10512 && bases(s)==before);
    base_context(s);neutral();capture(s);
}
} // namespace
void delay_group(){delay_taps();delay_clear_and_rows();}
void chorus_group(){chorus_controls();}
void ghost_group(){ghost_corridors();ghost_existing();}
} // namespace usability_gestures
