// Only --rev-dense-only: exact main.cpp HID dispatch, real editor and engine.
namespace {
void test_rev_actual_actions_fine() {
    RevFixture f;auto& u=*f.rev;rev_visual_reset(f);
    const int dirty=mutations;
    input::down(150,10);CHECK(u.params[4].base==0 && mutations==dirty);input::up();CHECK(u.params[4].base==32767);
    input::down(150,10);input::move(200,10);input::move(150,10);input::up();CHECK(u.params[4].base==32767);
    input::down(150,10);input::up();CHECK(u.params[4].base==0);
    input::down(210,10);CHECK(!u.bypass);input::up();CHECK(u.bypass);
    input::down(210,10);input::move(234,10);input::up();CHECK(u.bypass);
    input::down(210,10);input::up();CHECK(!u.bypass);
    for(u32 key:{u32(KEY_A),u32(KEY_X),u32(KEY_R),u32(KEY_B),u32(KEY_SELECT)}) {
        input::down(150,10);input::frame(key,key|KEY_TOUCH);input::frame(0,0);CHECK(u.params[4].base==0);
    }
    input::down(150,10);input::nav_opened=true;input::frame(0,KEY_TOUCH);input::nav_opened=false;input::frame(0,0);CHECK(u.params[4].base==0);
    input::down(150,10);g_overlay=Overlay::None;reverb_focus_capture();g_overlay=Overlay::Editor;input::up();CHECK(u.params[4].base==0);
    reverb_focus_input(0,0,false);u.params[0].base=12000;
    input::down(50,40);input::move(74,40);CHECK(u.params[0].base==15276);
    input::touch={74,40};input::frame(KEY_R,KEY_R|KEY_TOUCH);CHECK(u.params[0].base==15276);
    input::move(98,40);CHECK(u.params[0].base==15276+24*32767/(240*8));
    const q15 fine=u.params[0].base;input::touch={98,40};input::frame(0,KEY_TOUCH,KEY_R);CHECK(u.params[0].base==fine);
    input::move(122,40);input::up();CHECK(u.params[0].base==fine+3276);
    // Same pointer slot, new unit: no pending action transfers to replacement.
    input::down(150,10);delete f.engine->replace_unit(0,0,new ReverbUnit);f.rev=static_cast<ReverbUnit*>(f.engine->chains[0].units[0]);
    input::up();CHECK(f.rev->params[4].base==0);
}
q15 rev_bank_lock(const Engine& e,int bank,int step,int param,q15 fallback=-1) {
    const auto& s=e.sequence_pattern(bank).steps[0][step];
    for(int i=0;i<s.lock_count;++i)if(s.locks[i].slot==0 && s.locks[i].param==param)return s.locks[i].value;
    return fallback;
}
void test_rev_actual_banks_lens() {
    RevFixture f;auto& u=*f.rev;rev_visual_reset(f);const auto bases=rev_values(u);
    g_overlay=Overlay::None;g_view=View::Sequencer;extent_bank(f,2);
    f.engine->request_pattern(7);CHECK(f.engine->active_pattern==7 && sequencer_edit_bank()==2);
    g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    auto& step=f.engine->sequence_pattern(2).steps[0][5];step.locks[0]={0,0,10000};step.lock_count=1;
    input::down(50,40);input::move(74,40);input::up();CHECK(rev_bank_lock(*f.engine,2,5,0)==13276 && rev_values(u)==bases);
    CHECK(f.engine->pattern.steps[0][5].lock_count==0);
    input::down(150,10);CHECK(step.lock_count==1);input::up();CHECK(rev_bank_lock(*f.engine,2,5,4)==32767 && rev_values(u)==bases);
    step.locks[2]={0,1,15000};step.locks[3]={0,2,16000};step.lock_count=4;
    const int dirty=mutations;input::down(50,198);input::move(74,198);input::up();CHECK(g_seq_lock_flash>0 && mutations==dirty && rev_bank_lock(*f.engine,2,5,3)==-1);
    input::down(150,10);g_seq_lock_step=6;input::up();CHECK(rev_bank_lock(*f.engine,2,6,4)==-1);
    g_seq_lock_step=5;input::down(150,10);g_seq_lock_chain=1;input::up();CHECK(rev_bank_lock(*f.engine,2,5,4)==32767);
    g_seq_lock_chain=0;input::down(210,10);input::up();CHECK(!u.bypass);
    g_seq_lock_armed=false;g_view=View::Chains;f.engine->cancel_sequence_audition();
    for(bool modern:{false,true}) {
        CHECK(u.set_modern(modern));input::frame(0,0);u.params[1].base=32767;u.params[1].snap();
        input::down(210,40);input::up();CHECK(reverb_focus_selected()==1);
        input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());param_lens_capture();rev_visual_surface(u,true,true);
        CHECK(has_text(modern?"8.00s":"91.5%"));
        const auto frozen=pixels;CHECK(u.set_modern(!modern));
        rev_visual_surface(u,true,true);CHECK(pixels==frozen); // mode was captured, not read at draw
        rev_visual_surface(u,false,true);CHECK(has_text(modern?"base 8.00s":"base 91.5%"));
        CHECK(u.set_modern(modern));
        input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    }
}
void test_rev_actual_frozen_facade() {
    RevFixture f;auto& u=*f.rev;rev_visual_reset(f);rev_visual_feed(u,12000);
    cleanup_capture(f);const auto top=cleanup_draw(true);const int top_count=objects;
    const auto bottom=cleanup_draw(false);cleanup_pair_budget(top_count);
    rev_visual_surface(u,true);CHECK(pixels==top);rev_visual_surface(u,false);CHECK(pixels==bottom);
    for(int s=1;s<UNITS_PER_CHAIN;++s)delete f.engine->replace_unit(0,s,new StrainUnit);
    cleanup_capture(f);cleanup_draw(true);CHECK(has_text("strain"));
    cleanup_draw(false);
}
int run_rev_dense_ui() {
    int failed=0,executed=0;
    const auto run=[&](const char* name,auto fn){++executed;try{fn();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("rev relative controls / freeze ownership",test_rev_controls);
    run("rev stored locks / full bank / modulation",test_rev_locks_lens);
    run("rev touch geometry / paper / palette",test_rev_menu_style_geometry);
    run("rev all parameters / extrema / native gothic masks",test_rev_draw);
    run("rev selected modulation panel / frozen geometry",test_rev_menu_lens_draw);
    run("rev pure geometry / exact envelope law / modes",test_rev_visual_model);
    run("rev passive shape / freeze states / live mix",test_rev_visual_passive_freeze);
    run("rev true-energy frozen duals / detached engine",test_rev_visual_frozen);
    run("rev actual main lift / escape / keys / fine reanchor",test_rev_actual_actions_fine);
    run("rev actual independent bank locks / frozen mode readout",test_rev_actual_banks_lens);
    run("rev actual editor/top facade / full six-unit route",test_rev_actual_frozen_facade);
    std::printf("rev UI integration: %d/%d; host shared peak %d/8192 (not hardware fps)\n",executed-failed,executed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
