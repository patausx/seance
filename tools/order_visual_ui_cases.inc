// Included inside order_cases, after the original six regression groups.
// All geometry is observed from real native text calls/pixels, never order internals.
namespace p=seance::platform::ui3ds::palette;
namespace tc=seance::platform::ui3ds::theme_config;
constexpr const char* theme_names[]={"pink","graphite","grimoire","black-mass","custom"};
void use_theme(int theme){
    const tc::Colors custom{tc::rgb(213,232,220),tc::rgb(29,47,70),tc::rgb(186,40,94)};
    p::set(tc::Settings{p::Theme(theme),custom});
}
std::vector<int> chain_state(Fixture& f){
    f.refresh();const auto& s=g_ui_snapshot;
    std::vector<int> values{int(f.hash()),int(f.hash()>>32),mutations,s.active_pattern,s.pending_pattern,s.transport_playing,
        s.pattern_chain_length,s.pattern_chain_position,s.pattern_chain_bars_remaining,s.pattern_chain_loop,
        s.pattern_chain_playing,s.pattern_chain_paused,s.pattern_chain_held,s.pattern_chain_next_pending,
        s.pattern_chain_next_position,s.pattern_chain_next_pattern,s.pattern_chain_next_bars,
        s.pattern_chain_current_pattern,s.pattern_chain_current_bars,s.pattern_chain_start_pending,
        s.pattern_chain_armed_pattern,s.pattern_chain_armed_bars};
    for(const auto& step:f.engine->pattern_chain){values.push_back(step.pattern);values.push_back(step.bars);}
    return values;
}
void four(Fixture& f){enter(f);add(f,0,4);add(f,1,5);add(f,2,8);add(f,3,2);tap(f,40,10);}
std::pair<int,int> label(const char* wanted){
    for(auto it=bottom_texts.rbegin();it!=bottom_texts.rend();++it)if(it->text==wanted)return {it->call_x,it->call_y};
    CHECK(false);return {-999,-999};
}
void solid(const std::vector<u32>& image,int x,int y,int w,int h,u32 expected){
    CHECK(x>=0&&y>=0&&x+w<=320&&y+h<=240);
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)CHECK(image[yy*320+xx]==expected);
}
void numeric_mask(const std::vector<u32>& image,int center_x,int top,int bank,u32 foreground,u32 background){
    const auto* glyph=inspector_gothic::digit(char('1'+bank));CHECK(glyph);
    const int x=center_x-glyph->width/2,stride=(glyph->width+7)/8;
    int on_count=0,off_count=0;
    for(int yy=0;yy<glyph->height;++yy)for(int xx=0;xx<glyph->width;++xx){
        const bool on=note_display::bitmap[glyph->offset+yy*stride+xx/8]&(0x80u>>(xx&7));
        CHECK(image[(top+yy)*320+x+xx]==(on?foreground:background?background:p::paper_color(top+yy)));on_count+=on;off_count+=!on;
    }
    CHECK(on_count>100&&off_count>100);
}
void ruler_pixels(const std::vector<u32>& image,int x,int y,int bars,int done,u32 ink,u32 rail,u32 fill){
    for(int row=0;row<2;++row)for(int column=0;column<62;++column){
        u32 expected=fill;
        for(int bar=0;bar<bars;++bar){const int a=bar*62/bars,b=std::max(a+1,(bar+1)*62/bars-3);
            if(column>=a&&column<b&&row<(bar<done?2:1))expected=bar<done?ink:rail;
        }
        CHECK(image[(y+row)*320+x+column]==expected);
    }
}
void pixel_roles_and_chooser(){
    for(int theme=0;theme<tc::kCount;++theme){use_theme(theme);Fixture f;four(f);const auto state=chain_state(f);
        auto image=render_frozen(f);
        begin_surface(400,240);performance_view_draw_top();
        for(int x:{44,193,206,355})for(int y=40;y<169;++y)CHECK(pixels[y*400+x]==p::paper_color(y));
        for(int slot=0;slot<4;++slot){const int x=8+(slot%2)*156,y=51+(slot/2)*78;
            const u32 fill=slot==0?p::ink():p::cell();
            // Entire four edges are the surface itself: no one-pixel frame,
            // cut corner, shadow, accent outline, or current-bank special RGB.
            solid(image,x,y,148,1,fill);solid(image,x,y+71,148,1,fill);
            solid(image,x,y,1,72,fill);solid(image,x+147,y,1,72,fill);
            solid(image,x+3,y+20,5,45,fill);
            numeric_mask(image,x+29,y+23,slot,slot==0?p::bg():p::ink(),fill);
            ruler_pixels(image,x+68,y+43,f.engine->pattern_chain[slot].bars,0,slot==0?p::bg():p::ink(),
                slot==0?tc::mix(p::ink(),p::bg(),72):theme==0?p::branch():p::subdued(),fill);
        }
        CHECK(f.engine->set_pattern_chain_step(1,7,16));image=render_frozen(f);
        numeric_mask(image,193,74,7,p::ink(),p::cell());
        ruler_pixels(image,232,94,16,0,p::ink(),theme==0?p::branch():p::subdued(),p::cell());
        CHECK(f.engine->set_pattern_chain_step(1,1,5));refresh(f);
        tap(f,174,139);image=render_frozen(f);CHECK(label("2 bars")==std::make_pair(232,158));
        solid(image,164,129,148,1,p::ink());solid(image,8,51,148,1,p::cell());
        numeric_mask(image,193,152,3,p::bg(),p::ink());
        CHECK(chain_state(f)==state); // selection is UI-only
        tap(f,185,164);image=render_frozen(f);
        CHECK(label("card 04 / choose bank").second==33);
        for(int bank=0;bank<8;++bank){const int x=16+74*(bank%4),y=65+62*(bank/4);
            numeric_mask(image,x+33,y+9,bank,bank==3?p::bg():p::ink(),bank==3?p::ink():0);
            // Graphite and extended themes are flat. Pink paper is row-varying,
            // so check its unselected off-mask via a separate row-aware pass.
        }
        // Chooser owns tabs and inactive ordinary footer targets.
        tap(f,264,10);CHECK(order::active()&&!transition::active());CHECK(chain_state(f)==state);
        osc_input_main::down(16+2*74+30,127+20);const auto pressed=render_frozen(f);
        CHECK(chain_state(f)==state);solid(pressed,164,127,66,1,p::cell());
        CHECK(pressed!=image);osc_input_main::move(5,198);osc_input_main::up();advance(f,200);
        CHECK(chain_state(f)==state);CHECK(render_frozen(f)==image);
        // A fresh press commits once; the original touch cannot rearm after exit.
        osc_input_main::down(16+2*74+30,127+20);CHECK(chain_state(f)==state);osc_input_main::up();refresh(f);
        CHECK(f.engine->pattern_chain[3].pattern==6&&mutations==1);osc_input_main::up();CHECK(mutations==1);
        tap(f,185,164);tap(f,230,223);CHECK(mutations==1);render_frozen(f);
        CHECK(f.engine->resume_pattern_chain(0));refresh(f);image=render_frozen(f);
        // Selected is not live: running/current uses explicit text, never an
        // extra selected-looking fill. The already-selected fourth card stays ink.
        solid(image,8,51,148,1,p::cell());solid(image,164,129,148,1,p::ink());
        CHECK(label("ready").second==59);
    }
    // Inclusive native hit-grid edges for every new 66x52 chooser bank, plus
    // both full-width half-footer actions. Geometry must agree with the pixels.
    for(int bank=0;bank<8;++bank)for(auto point:{std::pair<int,int>{0,0},{65,0},{0,51},{65,51},{33,26}}){
        Fixture f;four(f);tap(f,185,164);const auto untouched=chain_state(f);
        osc_input_main::down(16+74*(bank%4)+point.first,65+62*(bank/4)+point.second);CHECK(chain_state(f)==untouched);
        osc_input_main::up();refresh(f);CHECK(f.engine->pattern_chain[3].pattern==bank);CHECK(mutations==(bank==3?0:1));
    }
    for(int x:{0,159,160,319}){Fixture f;four(f);tap(f,185,164);tap(f,x,239);CHECK(f.engine->pattern_chain_length==(x<160?3:4));CHECK(mutations==(x<160?1:0));}
}
void header_drag_and_live_pixels(){
    for(int dx:{0,9,58,115,147})for(int dy:{0,9,19}){
        Fixture f;four(f);tap(f,174,139);const auto original=chain_state(f);
        const int x=164+dx,y=129+dy;
        osc_input_main::down(x,y);osc_input_main::move(x,y-6);CHECK(chain_state(f)==original);
        osc_input_main::up();advance(f,200);CHECK(chain_state(f)==original); // <=6 never lifts
        osc_input_main::down(x,y);osc_input_main::move(x,y-68);CHECK(chain_state(f)==original);
        auto moving=render_frozen(f,nullptr,true);
        CHECK(label("2 bars")==std::make_pair(232,90)); // grab offset preserved, not card-center snap
        numeric_mask(moving,193,84,3,p::bg(),p::ink());
        osc_input_main::up();refresh(f);CHECK(mutations==1);CHECK(f.engine->pattern_chain[1].pattern==3);
        CHECK(f.engine->pattern_chain[0].pattern==0&&f.engine->pattern_chain[2].pattern==1&&f.engine->pattern_chain[3].pattern==2);
        osc_input_main::up();advance(f,200);CHECK(mutations==1);render_frozen(f);
    }
}
void reflow_and_settle(){
    for(int theme=0;theme<tc::kCount;++theme){use_theme(theme);Fixture f;four(f);tap(f,174,139);const auto original=chain_state(f);
        osc_input_main::down(180,139);osc_input_main::move(180,71);const auto start=render_frozen(f,nullptr,true);
        const auto neighbor_start=label("5 bars");CHECK(neighbor_start==std::make_pair(232,80));
        g_ui_frame+=120;CHECK(render_frozen(f,nullptr,true)==start); // no frame-count animation
        advance(f,60);const auto middle=render_frozen(f,nullptr,true);const auto neighbor_middle=label("5 bars");
        CHECK(neighbor_middle.first>76&&neighbor_middle.first<232&&neighbor_middle.second>80&&neighbor_middle.second<158);
        CHECK(start!=middle);CHECK(chain_state(f)==original);
        // Tick frequency is irrelevant: two frozen draws at identical ms remain identical.
        refresh(f);CHECK(render_frozen(f,nullptr,true)==middle);
        advance(f,140);const auto preview=render_frozen(f,nullptr,true);
        CHECK(label("5 bars")==std::make_pair(76,158));CHECK(label("8 bars")==std::make_pair(232,158));
        CHECK(label("2 bars")==std::make_pair(232,90));CHECK(preview!=middle);CHECK(chain_state(f)==original);
        osc_input_main::up();refresh(f);const auto dropped=render_frozen(f,nullptr,true);CHECK(mutations==1);
        CHECK(f.engine->pattern_chain[1].pattern==3);CHECK(label("2 bars").second==90);
        advance(f,60);const auto settle_middle=render_frozen(f,nullptr,true);const int mid_y=label("2 bars").second;
        CHECK(mid_y>80&&mid_y<90);CHECK(settle_middle!=dropped);
        advance(f,140);const auto settled=render_frozen(f);CHECK(label("2 bars")==std::make_pair(232,80));
        CHECK(settled!=settle_middle);const auto committed=chain_state(f);advance(f,1000);CHECK(render_frozen(f)==settled);CHECK(chain_state(f)==committed);
    }
}
void cancel_regrab_and_interference(){
    for(int reason=0;reason<7;++reason){Fixture f;four(f);tap(f,174,139);const auto original=chain_state(f);const auto home=render_frozen(f);
        osc_input_main::down(180,139);osc_input_main::move(180,71);advance(f,80);render_frozen(f,nullptr,true);
        if(reason==0){performance_view_touch_release(false);osc_input_main::up();}
        if(reason==1){osc_input_main::move(155,207);osc_input_main::up();} // gap/footer: never a drop
        if(reason==2){performance_view_buttons(KEY_L,KEY_L,0);osc_input_main::up();performance_view_buttons(0,0,KEY_L);}
        if(reason==3){g_overlay=Overlay::ConfirmDiscard;refresh(f);g_overlay=Overlay::None;osc_input_main::up();}
        if(reason==4){g_view=View::Sequencer;refresh(f);g_view=View::Performance;osc_input_main::up();}
        if(reason==5){order::close();order::open();osc_input_main::up();}
        if(reason==6){performance_view_buttons(KEY_START,KEY_START,0);osc_input_main::move(180,71);osc_input_main::up();performance_view_buttons(0,0,KEY_START);}
        refresh(f);CHECK(chain_state(f)==original);advance(f,200);CHECK(render_frozen(f)==home);
        // Fresh legitimate input after cancellation must not be swallowed or
        // inherit a stale pending drop, even while the return was active.
        osc_input_main::down(180,139);osc_input_main::move(180,71);performance_view_touch_release(false);osc_input_main::up();advance(f,30);
        const auto returning=render_frozen(f,nullptr,true);const auto at=label("2 bars");
        const int grab_x=at.first-68+16,grab_y=at.second-29+10;
        osc_input_main::down(grab_x,grab_y);render_frozen(f,nullptr,true);CHECK(label("2 bars")==at);
        advance(f,80);render_frozen(f,nullptr,true);CHECK(label("2 bars")==at); // freeze while held below threshold
        osc_input_main::move(grab_x,grab_y+8);render_frozen(f,nullptr,true);
        CHECK(label("2 bars")==std::make_pair(at.first,std::min(162,at.second+8)));
        performance_view_touch_release(false);osc_input_main::up();advance(f,200);
        CHECK(chain_state(f)==original);CHECK(render_frozen(f)==home);
        osc_input_main::down(180,139);osc_input_main::move(180,71);osc_input_main::up();advance(f,200);
        CHECK(mutations==1&&f.engine->pattern_chain[1].pattern==3);render_frozen(f);
    }
    // A new down replaces the held gesture, not an implicit release-to-place.
    Fixture fresh;four(fresh);const auto untouched=chain_state(fresh);
    osc_input_main::down(180,139);osc_input_main::move(180,71);advance(fresh,40);
    osc_input_main::down(18,60);osc_input_main::up();advance(fresh,200);CHECK(chain_state(fresh)==untouched);render_frozen(fresh);
}
void pages_identity_and_external_edits(){
    Fixture f;enter(f);for(int i=0;i<12;++i)add(f,i%8,i+1);tap(f,40,10);
    CHECK(f.engine->resume_pattern_chain(0));CHECK(f.engine->next_pattern_chain());refresh(f);
    const auto original=chain_state(f);const int current=f.engine->pattern_chain_current_pattern;
    osc_input_main::down(18,60);osc_input_main::move(305,33);CHECK(chain_state(f)==original);
    render_frozen(f,nullptr,true);g_ui_frame+=23;osc_input_main::move(305,33);render_frozen(f,nullptr,true);
    CHECK(label("8 bars")==std::make_pair(232,158)); // first page dwell jumps once, then must wait 24 frames
    ++g_ui_frame;osc_input_main::move(305,33);render_frozen(f,nullptr,true);
    CHECK(label("12 bars")==std::make_pair(232,158));CHECK(chain_state(f)==original);
    osc_input_main::move(290,140);osc_input_main::up();advance(f,200);
    CHECK(mutations==1&&f.engine->pattern_chain[11].pattern==0);CHECK(f.engine->pattern_chain_position==11);
    CHECK(f.engine->pattern_chain_current_pattern==current);CHECK(g_ui_snapshot.pattern_chain_next_position==0);
    CHECK(g_ui_snapshot.pattern_chain_next_pattern==1&&g_ui_snapshot.pattern_chain_next_bars==2);render_frozen(f);
    // A changed identity/length during a held drag cannot mutate another row on lift.
    for(int change=0;change<3;++change){Fixture other;four(other);osc_input_main::down(180,139);osc_input_main::move(180,71);
        if(change==0)add(other,7,16);
        else if(change==1)CHECK(other.engine->set_pattern_chain_step(3,7,16));
        else CHECK(other.engine->move_pattern_chain_step(0,1)); // source3 unchanged; unrelated identity change still invalidates held command
        refresh(other);const auto changed=chain_state(other);osc_input_main::up();advance(other,200);CHECK(chain_state(other)==changed);render_frozen(other);
    }
}
void raw_reorder_publication(){
    for(int theme=0;theme<tc::kCount;++theme)for(int source:{0,3}){
        use_theme(theme);Fixture f;four(f);CHECK(f.engine->resume_pattern_chain(0));CHECK(f.engine->next_pattern_chain());refresh(f);
        const auto original=chain_state(f);const int to=source==0?3:1;
        osc_input_main::down(source==0?18:180,source==0?61:139);
        osc_input_main::move(180,source==0?139:71);advance(f,200);CHECK(chain_state(f)==original);
        auto held=render_frozen(f,nullptr,true);
        if(source==3)solid(held,8,51,148,1,p::cell()); // no second inverse tile for old selection
        const auto before=g_ui_snapshot;osc_input_main::up();CHECK(mutations==1&&f.engine->pattern_chain[to].pattern==source);
        CHECK(std::memcmp(&before,&g_ui_snapshot,sizeof before)==0); // lift has not published the engine DTO
        const auto raw=render_frozen(f,nullptr,true,false);
        const int x=source==0?168:164,y=source==0?129:61;
        numeric_mask(raw,x+29,y+23,source,p::bg(),p::ink());
        CHECK(label("ready").second==(source==0?137:59));CHECK(label("next").second==(source==0?59:137));
        CHECK(label(source==0?"4 bars":"2 bars")==std::make_pair(x+68,y+29));
        CHECK(label("5 bars")==std::make_pair(76,source==0?80:158));
        numeric_mask(raw,37,source==0?74:152,1,p::ink(),p::cell()); // queued bank2 remains accepted tuple
        begin_surface(400,240);performance_view_draw_top();const auto raw_top=pixels;
        ++g_ui_frame;performance_view_tick();CHECK(render_frozen(f,nullptr,true,false)==raw); // stale DTO bridge lasts next actual main tick
        f.refresh();performance_view_tick();CHECK(render_frozen(f,nullptr,true)==raw);
        begin_surface(400,240);performance_view_draw_top();CHECK(pixels==raw_top);
        CHECK(g_ui_snapshot.pattern_chain_position==(source==0?3:0));
        CHECK(g_ui_snapshot.pattern_chain_next_position==(source==0?0:2));advance(f,200);render_frozen(f);
    }
    {
        Fixture duplicates;enter(duplicates);for(int i=0;i<4;++i)add(duplicates,0,4);tap(duplicates,40,10);
        CHECK(duplicates.engine->resume_pattern_chain(0));CHECK(duplicates.engine->next_pattern_chain());refresh(duplicates);
        const auto old=g_ui_snapshot;osc_input_main::down(18,61);osc_input_main::move(180,139);advance(duplicates,200);
        osc_input_main::up();CHECK(mutations==1);CHECK(std::memcmp(&old,&g_ui_snapshot,sizeof old)==0);
        const auto raw=render_frozen(duplicates,nullptr,true,false);
        CHECK(label("ready").second==137&&label("next").second==59); // identical tuples do not identify current/next cards
        numeric_mask(raw,197,152,0,p::bg(),p::ink());
        begin_surface(400,240);performance_view_draw_top();const auto raw_top=pixels;
        duplicates.refresh();performance_view_tick();CHECK(render_frozen(duplicates,nullptr,true)==raw);
        begin_surface(400,240);performance_view_draw_top();CHECK(pixels==raw_top);
        CHECK(g_ui_snapshot.pattern_chain_position==3&&g_ui_snapshot.pattern_chain_next_position==0);advance(duplicates,200);render_frozen(duplicates);
    }
    // A different frozen chain is not the exact pre-edit DTO. The bridge must
    // never rewrite a newer/external publication just because its count matches.
    Fixture f;four(f);osc_input_main::down(180,139);osc_input_main::move(180,71);advance(f,200);
    osc_input_main::up();g_ui_snapshot.pattern_chain_steps[0].pattern=7;
    const auto external=render_frozen(f,nullptr,true,false);
    numeric_mask(external,37,74,7,p::ink(),p::cell());
    numeric_mask(external,193,84,1,p::bg(),p::ink()); // displayed old index1, not forced pending bank4
    advance(f,200);render_frozen(f);
}
