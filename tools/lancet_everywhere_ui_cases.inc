// Extra current-module poses; all geometry is drawn by the actual editors.
// Appended inside module_headings_cases, not a replacement for the frame catalogue.
void lancet_glyphs(Scene& s,const std::string& name) {
    s.capture();editor_style::capture();editor_gpu::begin();f::surface(false,true);
    {f::AudioHidden hidden;editor_draw();}f::record_glyphs=false;
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
    int checked=0;
    for(int y=26;y<224;++y)for(int x=0;x<320;++x){const int at=y*320+x;
        if(f::glyph_mask[at]){if(pixels[at]!=f::glyph_ink[at]){
            std::fprintf(stderr,"lancet glyph overwritten: %s x%d y%d\n",name.c_str(),x,y);CHECK(false);}
            ++checked;}}
    CHECK(checked>0);
}
void lancet_stress(const std::string& prefix) {
    for(const auto& item:f::catalogue)for(int pose=0;pose<7;++pose) {
        Scene s(item.kind);
        for(int p=0;p<s.unit->num_params;++p) {
            auto& q=s.unit->params[p];const int lo=q.bipolar?-32767:0;
            const int value=pose==0?lo:pose==1?32767:pose==2?0:pose==3?12000:0;
            q.base=q15(value);q.snap();
            if(pose>=3) {
                q.src=ModSource::SigilA;q.mod_enabled=pose!=6;q.mod_depth=0;
                q.target=q.effective=q15(pose==3?value:pose==4?32767:lo);
            }
        }
        s.unit->control_tick();
        if(pose==5)s.engine->chains[0].mute=true;
        const auto name=prefix+item.name+"_lancet_pose"+std::to_string(pose);
        lancet_glyphs(s,name);take(s,name,"lancet-pose");
    }
    std::printf("lancet: all current modules extrema/zero/equal-linked/diverged/mute/disabled-route poses passed\n");
}
