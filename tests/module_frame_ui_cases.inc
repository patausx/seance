// Appended to the private frame capture fixture inside module_headings_cases.
// Open editors only: full route names, real slot selection, holes and bypass.
void frame_route_names(const std::string& prefix) {
    CHECK(std::string(editor_style::module_name(UnitKind::Ghost))=="ghost");
    CHECK(std::string(editor_style::module_name(UnitKind::Strain))=="strain");
    CHECK(std::string(editor_style::module_name(UnitKind::Stranular))=="swarm");
    CHECK(std::string(editor_style::module_name(UnitKind::Glitch))=="glitch");
    UnitKind longest=UnitKind::Glitch;
    for(const auto& item:f::catalogue)
        if(ui::text_w(editor_style::module_name(item.kind))>ui::text_w(editor_style::module_name(longest)))longest=item.kind;
    const UnitKind mixed[]={UnitKind::Glitch,UnitKind::Strain,UnitKind::Stranular,
                           UnitKind::Ghost,UnitKind::Noise,UnitKind::Mouth};
    for(auto kind:{UnitKind::Ghost,UnitKind::Stranular})for(int selected_slot:{0,2,5})
    for(const char* mode:{"mixed","holes","longest"}) {
        Scene s(kind);
        for(auto*& u:s.engine->chains[0].units){if(u!=s.unit)delete u;u=nullptr;}
        s.slot=selected_slot;g_edit_slot=g_sel_slot=selected_slot;
        for(int i=0;i<UNITS_PER_CHAIN;++i) {
            if(i==selected_slot)s.engine->chains[0].units[i]=s.unit;
            else if(std::string(mode)!="holes" || (i!=1&&i!=3)) {
                s.engine->chains[0].units[i]=make_unit(std::string(mode)=="longest"?longest:mixed[i]);
                CHECK(s.engine->chains[0].units[i]);
            }
            if(auto* u=s.engine->chains[0].units[i])u->bypass=(i%2)==0;
        }
        take(s,prefix+f::item(kind).name+"_route_"+mode+std::to_string(selected_slot),std::string("route-")+mode);
        module_inspector_surface(true);{f::AudioHidden hidden;top_draw();}
        CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
        std::vector<TextRecord> route;
        for(const auto& t:texts)if(t.call_y==35)route.push_back(t);
        size_t at=0;int x=14,selected_x=-1,selected_w=0;
        for(int i=0;i<UNITS_PER_CHAIN;++i)if(const auto* u=s.engine->chains[0].units[i]) {
            if(at){CHECK(at<route.size()&&route[at].text=="/"&&route[at].call_x==x);++at;x+=14;}
            const char* name=editor_style::module_name(u->kind);const int w=ui::text_w(name);
            CHECK(at<route.size()&&route[at].text==name&&route[at].call_x==x&&route[at].scale==1);
            CHECK(x>=14&&x+w<=386);++at;
            if(i==selected_slot){selected_x=x;selected_w=w;}
            if(u->bypass)for(int px=x;px<=x+w-2;++px)CHECK(pixels[39*400+px]==editor_style::ghost());
            x+=w+8;
        }
        CHECK(at==route.size()&&selected_x>=14);
        for(int px=14;px<386;++px)
            CHECK((pixels[45*400+px]==editor_style::spot())==(px>=selected_x&&px<=selected_x+selected_w-2));
    }
    std::printf("module editors: full route names / six slots / holes / selection / bypass passed\n");
}

// Visible centres of the fixed 14px caption row, not a broad input suite.
void frame_header_actions(const std::string& prefix) {
    frame_route_names(prefix);
    { Scene s(UnitKind::Osc); auto& u=*static_cast<OscUnit*>(s.unit);
      const bool q=u.pitch_quantize,b=u.bypass;const int dirty=mutations;
      tap(283,18);tap(312,18);CHECK(u.pitch_quantize==q&&u.bypass==b&&mutations==dirty);
      tap(284,18);CHECK(u.pitch_quantize!=q&&u.bypass==b);
      take(s,prefix+"osc_frame_qnt","frame-qnt");
      tap(311,18);CHECK(u.pitch_quantize==q&&u.bypass==b);
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      s.capture();const int locked_dirty=mutations;tap(296,18);
      CHECK(u.pitch_quantize==q&&u.bypass==b&&mutations==locked_dirty);
      take(s,prefix+"osc_frame_qnt_lock","frame-qnt-lock"); }
    { Scene s(UnitKind::Ghost);const bool b=s.unit->bypass;
      tap(214,18);CHECK(s.unit->bypass!=b);take(s,prefix+"ghost_frame_byp","frame-byp");
      tap(214,18);CHECK(s.unit->bypass==b);const int dirty=mutations;
      take(s,prefix+"ghost_frame_note_before","frame-note-before");const bool note=module_inspector_text("note");
      tap(145,18);take(s,prefix+"ghost_frame_note_after","frame-note-after");
      CHECK(module_inspector_text("note")!=note&&mutations==dirty);tap(145,18); }
    { Scene s(UnitKind::Stranular);const bool b=s.unit->bypass;
      const auto freeze=s.unit->params[StranularUnit::P_FREEZE].base;
      const auto hold=s.unit->params[StranularUnit::P_HOLD].base;
      tap(239,18);CHECK(s.unit->bypass!=b);
      CHECK(s.unit->params[StranularUnit::P_FREEZE].base==freeze&&s.unit->params[StranularUnit::P_HOLD].base==hold);
      take(s,prefix+"strn_frame_byp","frame-byp");tap(239,18);CHECK(s.unit->bypass==b); }
    { Scene s(UnitKind::Fm);auto& u=*static_cast<FmUnit*>(s.unit);const bool drone=u.drone;
      tap(145,18);CHECK(u.drone!=drone);take(s,prefix+"fm_frame_drone","frame-drone");
      tap(145,18);CHECK(u.drone==drone); }
    { Scene s(UnitKind::Grain);take(s,prefix+"gran_frame_fine_before","frame-fine-before");
      const bool fine=module_inspector_text("fine /32");const int dirty=mutations;
      tap(200,18);take(s,prefix+"gran_frame_fine_after","frame-fine-after");
      CHECK(module_inspector_text("fine /32")!=fine&&mutations==dirty);tap(200,18); }
    { Scene s(UnitKind::Bitcrush);auto& u=*static_cast<BitUnit*>(s.unit);
      CHECK(u.modern());const int dirty=mutations;
      tap(218,14);take(s,prefix+"bit_frame_setup","bit-setup");CHECK(mutations==dirty);
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      take(s,prefix+"bit_frame_setup_lock","bit-setup-lock");tap(218,14);
      u.set_modern(false);s.capture();tap(183,14);CHECK(!u.modern());
      take(s,prefix+"bit_frame_legacy_lock","bit-legacy-lock");
      g_seq_lock_armed=false;g_view=View::Chains;
      take(s,prefix+"bit_frame_legacy","bit-legacy");tap(183,14);CHECK(u.modern());
      take(s,prefix+"bit_frame_extended","bit-explicit-extend"); }
    { Scene s(UnitKind::Strain);auto& u=*static_cast<StrainUnit*>(s.unit);
      CHECK(u.modern());const int dirty=mutations;
      tap(218,14);take(s,prefix+"strain_frame_setup","strain-setup");CHECK(mutations==dirty);
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      take(s,prefix+"strain_frame_setup_lock","strain-setup-lock");tap(218,14);
      u.set_modern(false);s.capture();tap(183,14);CHECK(!u.modern());
      take(s,prefix+"strain_frame_legacy_lock","strain-legacy-lock");
      g_seq_lock_armed=false;g_view=View::Chains;
      take(s,prefix+"strain_frame_legacy","strain-legacy");tap(183,14);CHECK(u.modern());
      take(s,prefix+"strain_frame_extended","strain-explicit-extend"); }
    { Scene s(UnitKind::Ring);auto& u=*static_cast<RingUnit*>(s.unit);CHECK(u.modern());
      editor_touch_down(80,90);editor_touch_drag(80,73);editor_touch_release();
      CHECK(ring_focus_selected()==RingUnit::P_FINE&&u.params[RingUnit::P_FINE].base>0);
      take(s,prefix+"ring_frame_fine","ring-fine-gesture");
      u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
      take(s,prefix+"ring_frame_fine_mod","ring-fine-mod");param_lens_cancel();
      s.set(RingUnit::P_MODE,32767);g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;s.capture();
      CHECK(ring_focus_selected()==RingUnit::P_NOTE);take(s,prefix+"ring_frame_note","ring-note-primary");
      s.set(RingUnit::P_MODE,0);s.set(RingUnit::P_FREQ,0);s.set(RingUnit::P_FOLLOW,20000);
      tap(218,14);take(s,prefix+"ring_frame_updating","ring-4s-updating");
      for(int i=0;i<64;++i)s.capture();take(s,prefix+"ring_frame_slow","ring-4s-ready");
      u.set_modern(false);s.capture();
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      tap(183,14);CHECK(!u.modern());take(s,prefix+"ring_frame_legacy_lock","ring-legacy-lock");
      g_seq_lock_armed=false;g_view=View::Chains;take(s,prefix+"ring_frame_legacy","ring-legacy");
      tap(183,14);CHECK(u.modern());take(s,prefix+"ring_frame_extended","ring-explicit-extend"); }
    { Scene s(UnitKind::Tilt);auto& u=*static_cast<TiltUnit*>(s.unit);CHECK(u.extended());
      s.set(TiltUnit::P_TILT,21845);s.set(TiltUnit::P_RANGE,21845);s.set(TiltUnit::P_DRIVE,0);
      take(s,prefix+"tilt_frame_main","tilt-main");
      for(auto revision:{TiltUnit::Revision::Legacy,TiltUnit::Revision::Clear}){
        u.set_revision(revision);s.capture();const std::string rev=revision==TiltUnit::Revision::Legacy?"legacy":"clear";
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
        tap(183,14);CHECK(u.revision()==revision);take(s,prefix+"tilt_frame_"+rev+"_lock","tilt-old-lock");
        g_seq_lock_armed=false;g_view=View::Chains;take(s,prefix+"tilt_frame_"+rev,"tilt-old");
        tap(183,14);CHECK(u.extended());take(s,prefix+"tilt_frame_extend_"+rev,"tilt-explicit-extend");
      }
      tilt_focus_select(TiltUnit::P_TRIM);s.capture();u32 down=KEY_L;
      CHECK(param_lens_input(down,KEY_L,0,false));take(s,prefix+"tilt_frame_trim_mod","tilt-trim-mod");param_lens_cancel(); }
    { Scene s(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(s.unit);CHECK(u.modern());
      fold_focus_input(0,0,false);s.set(FoldUnit::P_SMOOTH,22000);
      take(s,prefix+"fold_frame_updating","fold-updating");for(int i=0;i<9;++i)s.capture();
      take(s,prefix+"fold_frame_wave","fold-wave");const int dirty=mutations;
      tap(218,14);take(s,prefix+"fold_frame_curve","fold-curve");CHECK(mutations==dirty);
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      take(s,prefix+"fold_frame_curve_lock","fold-curve-lock");
      u.set_modern(false);s.capture();tap(183,14);CHECK(!u.modern());take(s,prefix+"fold_frame_legacy_lock","fold-legacy-lock");
      g_seq_lock_armed=false;g_view=View::Chains;take(s,prefix+"fold_frame_legacy","fold-legacy");
      editor_touch_down(183,14);CHECK(!u.modern());editor_touch_release();CHECK(u.modern());
      for(int i=0;i<9;++i)s.capture();take(s,prefix+"fold_frame_extended","fold-explicit-extend");
      fold_focus_select(FoldUnit::P_SMOOTH);s.capture();u32 down=KEY_L;
      CHECK(param_lens_input(down,KEY_L,0,false));take(s,prefix+"fold_frame_smooth_mod","fold-smooth-mod");param_lens_cancel(); }
    { Scene s(UnitKind::Vca);auto& u=*static_cast<VcaUnit*>(s.unit);CHECK(u.modern());
      vca_focus_input(0,0,false);const int dirty=mutations;
      tap(218,14);take(s,prefix+"vca_frame_setup","vca-setup");CHECK(mutations==dirty);
      g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
      take(s,prefix+"vca_frame_setup_lock","vca-setup-lock");
      u.set_flexible(true);s.capture();tap(218,14);tap(245,136);CHECK(!u.modern());
      take(s,prefix+"vca_frame_ahd_lock","vca-ahd-lock");
      g_seq_lock_armed=false;g_view=View::Chains;take(s,prefix+"vca_frame_ahd_setup","vca-ahd-setup");
      tap(245,136);CHECK(u.modern());take(s,prefix+"vca_frame_extended","vca-explicit-extend");
      vca_focus_select(VcaUnit::P_RUN);s.capture();u32 down=KEY_L;
      CHECK(param_lens_input(down,KEY_L,0,false));take(s,prefix+"vca_frame_run_mod","vca-run-mod");param_lens_cancel();
      u.set_flexible(false);s.capture();take(s,prefix+"vca_frame_ad","vca-old-ad"); }
    std::printf("module frame: visible header centres + qnt bounds/lock passed\n");
}
