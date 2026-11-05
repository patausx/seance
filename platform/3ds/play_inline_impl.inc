// Included once in the guarded PLAY controller, after the shared word menu.
// Main-page controls use the same frozen WordCell DTO and actual lancet painter.
namespace part_pitch=seance::play_part_pitch;
void part_value(char*out,size_t n,int8_t semitone){
    std::snprintf(out,n,"%+d st",int(semitone));
}
void stop_stroke_locked(){
    if(stroke.audio_token&&!stroke.borrowed_audio&&g_eng)g_eng->stop_play_preview(stroke.audio_token);
    stroke.audio_token=0;
}
void latch_boundary_hold(){
    if(stroke.kind==Gesture::Cut&&stroke.hold_possible&&!stroke.held&&
       osGetTime()>=stroke.pressed&&osGetTime()-stroke.pressed>=kMergeHoldMs&&stroke.excursion<=kHoldMotion)stroke.held=true;
}
bool audio_gesture(Gesture g){return g==Gesture::Edge||g==Gesture::Cut||g==Gesture::Move||g==Gesture::PartPitch||
    g==Gesture::CutSpeed||g==Gesture::Note||g==Gesture::Tempo||g==Gesture::TapeSpeed||g==Gesture::Speed||g==Gesture::Gain||g==Gesture::Gate;}
void stroke_audio(C&u){
    if(!audio_gesture(stroke.kind)||(stroke.kind==Gesture::Gate&&view.page==Page::Auto))return;
    if(!editable(u)||u.bypass||g_eng->chains[ctx.chain].mute){stop_locked();return;}
    uint32_t a,b;selection(u,a,b);bool reverse=false;uint16_t rate=256;int8_t pitch=0;auto cuts=document(u).cuts;
    int id=view.selection_page==Page::Cuts?view.cut:-1;
    if(stroke.kind==Gesture::Cut&&stroke.direct_cut&&cuts.valid(stroke.cut_target,ctx.frames)){
        // A nonselected boundary sounds the actual adjacent part, not an unrelated focus.
        if(!cuts.valid(id,ctx.frames)||(cuts.at[stroke.cut_target]!=cuts.at[id]&&cuts.at[stroke.cut_target]!=cuts.end(id,ctx.frames)))id=stroke.cut_target;
    }
    if(stroke.merge_preview){const int left=cuts.merge(stroke.cut_target,ctx.frames);if(left>=0)id=left;}
    if(cuts.valid(id,ctx.frames)){a=cuts.at[id];b=cuts.end(id,ctx.frames);reverse=(cuts.reverse&(1u<<id))!=0;rate=cuts.speed_q8[id];pitch=cuts.pitch_semitones[id];}
    if(!stroke.audio_token&&!stroke.audio_attempted){
        stroke.audio_attempted=true;
        if(g_eng->play_preview_active(view.token)){stroke.audio_token=view.token;stroke.borrowed_audio=true;}
        else stroke.audio_token=g_eng->start_play_preview(ctx.chain,ctx.slot,a,b,true,reverse,rate,false,pitch);
    }
    // Exactly one part polarity/multiplier: Engine owns the global voice direction.
    if(stroke.audio_token&&!g_eng->update_play_preview(stroke.audio_token,a,b,reverse,rate,pitch))stop_stroke_locked();
}
void inline_build(WordFrame&f){
    const auto&d=frame.d;const auto&v=frame.v;const bool can=frame.loaded&&!frame.busy&&!frame.missing&&!frame.mismatch;
    const bool gate=d.modern&&play_mode::is_gate(d.bases[C::P_MODE]);const bool cut=v.selection_page==Page::Cuts&&d.cuts.valid(v.cut,frame.frames);
    const bool tape=d.pitch_mode==C::PitchMode::Legacy,stretch=d.pitch_mode==C::PitchMode::Stretch;
    auto choice=[&](layout::Box b,const char*s,int p,int value,bool selected=false,bool enabled=true){word_cell(f,b,s,WordOp::SetParam,p*256+value,can&&enabled&&(p==20||!frame.quiet),selected);};
    auto parameter=[&](layout::Box b,const char*s,int p,Gesture gesture,int64_t raw,int64_t lo,int64_t hi,bool enabled=true){
        auto&c=word_cell(f,b,s,WordOp::SetParam,0,can&&enabled&&!frame.quiet);c.gesture=gesture;c.param=p;c.raw=raw;c.lo=lo;c.hi=hi;
        c.live=p<0?raw:frame.live[p];c.linked=p>=0&&(frame.links&(1u<<p));
        play_readout::value_text(c.value,sizeof c.value,p,p<0?0:d.bases[p],d.modern,d.quantize,d.pitch_mode);return &c;};
    auto&mode_cell=word_cell(f,pl::mode,workspace_name(frame),WordOp::Menu,int(WordMenu::Mode),can&&!frame.quiet);mode_cell.chevron=true;
    choice(pl::shot,"one shot",C::P_MODE,0,d.modern&&!gate);
    if(gate){auto&c=word_cell(f,pl::gate,"gate",WordOp::SetParam,0,can&&!frame.quiet,true);c.gesture=Gesture::Gate;c.param=C::P_GATE;
        std::snprintf(c.label,sizeof c.label,"gate %s",play_mode::gate_label(d.bases[C::P_GATE]));}
    else choice(pl::gate,"gate",C::P_MODE,1);
    choice(pl::loop,d.bases[C::P_LOOP]>16383?"loop on":"loop off",C::P_LOOP,d.bases[C::P_LOOP]<=16383,d.bases[C::P_LOOP]>16383);
    choice(pl::reverse,d.bases[C::P_SPEED]<0?"rev on":"rev off",C::P_SPEED,d.bases[C::P_SPEED]>=0,d.bases[C::P_SPEED]<0);
    auto*c=parameter(pl::pitch,tape?"speed":"pitch",tape?C::P_SPEED:C::P_NOTE,tape?Gesture::TapeSpeed:Gesture::Note,
        tape?std::abs(int(d.bases[C::P_SPEED])):d.bases[C::P_NOTE],0,32767,tape||d.modern);
    if(tape)c->live=std::abs(int(frame.live[C::P_SPEED]));
    parameter(pl::gain,"gain",C::P_GAIN,Gesture::Gain,d.bases[C::P_GAIN],0,32767);
    if(stretch){auto*r=parameter(pl::rate,"rate",C::P_SPEED,Gesture::Tempo,std::max(2048,std::abs(int(d.bases[C::P_SPEED]))),2048,32767);
        r->live=std::max(2048,std::abs(int(frame.live[C::P_SPEED])));std::snprintf(r->value,sizeof r->value,"%.2fx",double(r->raw)/8192.);}
    if(v.selection_page==Page::Cuts){char t[32];std::snprintf(t,sizeof t,cut?"part %02d pitch":"part -- pitch",v.cut+1);
        auto*p=parameter(pl::part_pitch,t,-1,Gesture::PartPitch,cut?d.cuts.pitch_semitones[v.cut]:0,part_pitch::kMin,part_pitch::kMax,cut);
        if(cut)part_value(p->value,sizeof p->value,d.cuts.pitch_semitones[v.cut]);else std::snprintf(p->value,sizeof p->value,"--");
        word_cell(f,pl::part_reverse,cut&&(d.cuts.reverse&(1u<<v.cut))?"rev on":"rev off",WordOp::CutReverse,0,can&&cut&&!frame.quiet,cut&&(d.cuts.reverse&(1u<<v.cut)));
    }else {auto*g=parameter(pl::part_reverse,"gate",C::P_GATE,Gesture::Gate,d.bases[C::P_GATE],0,32767,d.modern&&gate);
        if(!gate)std::snprintf(g->value,sizeof g->value,"one shot");}
    choice(pl::footer(0),d.bypass?"bypass on":"bypass off",20,!d.bypass,d.bypass);
    choice(pl::footer(1),d.quantize?"quant on":"quant off",21,!d.quantize,d.quantize,tape);
}
void inline_draw_controls(){
    for(int i=0;i<word_frame.count;++i){const auto&c=word_frame.cells[i];const auto b=c.box;
        const uint32_t ink=!c.enabled?m::ghost():c.selected?m::spot():m::ink();
        if(c.gesture!=Gesture::None&&c.box.y0!=pl::gate.y0){
            const int y=b.y0+6;pw::text(b.x0,y,c.label,m::ghost());right(b.x1,y,c.value,ink);
            const auto at=[&](int64_t v){return b.x0+4+int((b.x1-b.x0-8)*std::clamp<int64_t>(v-c.lo,0,c.hi-c.lo)/(c.hi-c.lo));};
            if(c.enabled)module_fader::horizontal(pw::pen,b.x0+4,b.x1-4,b.y1-6,at(c.raw),at(c.live),{c.linked,false,frame.quiet});
            else module_fader::horizontal_track(pw::pen,b.x0+4,b.x1-4,b.y1-6);
        }else if(b.y0==pl::part_reverse.y0&&frame.v.selection_page==Page::Cuts){char t[24];
            std::snprintf(t,sizeof t,"part %02d",frame.v.cut+1);pw::text(b.x0,192,t,m::ghost());right(b.x1,192,"reverse",m::ghost());pw::text(b.x0,204,c.label,ink);
            if(c.selected)pw::line(b.x0,215,b.x0+ui::text_w(c.label)-1,215,m::spot());
        }else {const bool mode=b.y0==pl::mode.y0;
            const int y=mode?40:b.y0==pl::footer(0).y0?m::kTextY:b.y0+6,x=mode?b.x1-ui::text_w(c.label)-14:b.x0;
            pw::text(x,y,c.label,ink);if(c.chevron)pw::pen.chevron(x+ui::text_w(c.label)+8,y+3,ink);
            if(c.selected&&c.enabled&&y<m::kTextY)pw::line(x,y+12,x+ui::text_w(c.label)-1,y+12,m::spot());}
    }
}
bool inline_touch_controls(C&u,int x,int y){
    for(int i=0;i<word_frame.count;++i){const auto&c=word_frame.cells[i];if(!c.box.contains(x,y))continue;
        if(!c.enabled)return true;
        if(c.gesture!=Gesture::None){if(!editable(u))return true;
            view.selected=c.param>=0?c.param:C::P_SLICE;begin(c.gesture,x,y);stroke.box=c.box;stroke.argument=c.param;return true;}
        queue(Action::Word,int(c.op)+c.arg*256,c.box,x,y);return true;
    }
    return false;
}
