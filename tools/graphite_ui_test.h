// Included only by the focused graphite integration target. Real page renderers
// and main held-L routing; no authored UI geometry or hardware claims.
#include "../platform/3ds/ui_bounce.h"
#include "../platform/3ds/ui_nav.h"
#include "../platform/3ds/ui_palette.h"
namespace seance::platform::ui3ds {
bool g_has_dsp=true;
Unit* g_clip_unit=nullptr;
const char* RECORDING_DIR="/tmp/seance-graphite-unused";
void workspace_reset_chain(int) { CHECK(false); } // ready-page fixture never sends
}
namespace {
// Inspect actual native text pixels over the actual button background at the
// same row, including the moving fill edge. Pure gray role presence alone would
// miss light-on-light text when formerly distinct color roles become aliases.
int graphite_contrast_pixels=0;
void graphite_text_contrast(int x,int y,const std::string& text,int background_x) {
    auto luminance=[](u32 color){
        const auto linear=[](unsigned v){const double s=v/255.0;return s<=0.04045?s/12.92:std::pow((s+0.055)/1.055,2.4);};
        return .2126*linear(color&255)+.7152*linear((color>>8)&255)+.0722*linear((color>>16)&255);
    };
    for(size_t i=0;i<text.size();++i)for(int row=0;row<8;++row)for(int col=0;col<6;++col)
        if(trackr::ui::glyph_row(text[i],row)&(1u<<(5-col))) {
            const double fg=luminance(pixels[(y+row)*320+x+int(i)*6+col]);
            const double bg=luminance(pixels[(y+row)*320+background_x]);
            const double ratio=(std::max(fg,bg)+0.05)/(std::min(fg,bg)+0.05);
            if(ratio<4.5)throw std::runtime_error("low graphite glyph contrast: "+text+" ratio="+std::to_string(ratio)+" row="+std::to_string(row));
            ++graphite_contrast_pixels;
        }
}
void graphite_performance_contrast() {
    for(int side=0;side<2;++side) {
        PerformanceFixture f;const int x=side?270:8;performance_view_touch_down(x+17,210);
        for(int elapsed=0;elapsed<24;++elapsed) {
            if(elapsed)++g_ui_frame;
            performance_view_touch_hold(x+17,210);f.refresh();performance_surface(false);
            const std::string progress=std::string(side?"b ":"a ")+std::to_string(elapsed*100/24)+"%";
            CHECK(has_text("hold")&&has_text(progress.c_str()));
            graphite_text_contrast(x+6,203,progress,x+2);graphite_text_contrast(x+9,215,"hold",x+2);
        }
        f.release(false);
    }
    PerformanceFixture f;f.save_scene(1);performance_surface(false);CHECK(has_text("retry"));
    graphite_text_contrast(277,203,"b",272);graphite_text_contrast(300,203,"!",272);graphite_text_contrast(276,215,"retry",272);
}
int graphite_extra(int argc,char** argv) {
    try {
        CHECK(argc==3);const std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
        namespace p=seance::platform::ui3ds::palette;
        CHECK(p::bg()==rgb(36,36,36)&&p::cell()==rgb(48,48,48));
        CHECK(p::ink()==rgb(244,244,244)&&p::spot()==rgb(181,154,232));
        CHECK(p::ghost()==rgb(184,184,184)&&p::branch()==rgb(255,255,255));
        CHECK(p::spot()!=p::ink()&&p::spot()!=p::branch()&&p::spot()!=p::ghost());
        CHECK(p::inverse()==p::cell()&&p::subdued()==rgb(112,112,112));
        std::ofstream manifest(dir/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\tclipped\toverlaps\n";
        int peak=0;
        auto capture=[&](const char* name,const std::function<void(bool)>& draw){
            auto surface=[&](bool top){
                width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;
                texts.clear();pixels.assign(width*HEIGHT,0);draw(top);
                CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);
                for(u32 c:pixels)CHECK(c==rgb(181,154,232)||((c&255)==((c>>8)&255)&&(c&255)==((c>>16)&255)));
                return pixels;
            };
            const auto top=surface(true);const int tc=objects;dump_surface(dir,std::string(name)+"_top");
            const auto bottom=surface(false);const int bc=objects;dump_surface(dir,std::string(name)+"_bottom");
            CHECK(tc+bc<8192);peak=std::max(peak,tc+bc);
            auto* e=g_eng;auto* a=g_audio;g_eng=nullptr;g_audio=nullptr;
            CHECK(surface(true)==top&&surface(false)==bottom);g_eng=e;g_audio=a;
            manifest<<name<<'\t'<<tc<<'\t'<<bc<<'\t'<<tc+bc<<"\t0\t0\n";
        };
        {
            OscFixture f;g_sel_chain=g_edit_chain=g_sel_slot=g_edit_slot=0;
            input::frame(0,0);const auto entered=input::frame(KEY_L,KEY_L);
            CHECK(entered.owned&&entered.skip&&param_lens_is_open());
            param_lens_capture();
            capture("held_l_osc",[](bool top){if(top)CHECK(param_lens_draw_top());else param_lens_draw_bottom();});
            input::frame(0,0,KEY_L);CHECK(!param_lens_is_open());input::frame(0,0);
        }
        {
            Fixture f;nav_close();g_view=View::Bounce;g_overlay=Overlay::None;
            bounce_enter();bounce_capture();
            capture("bounce_ready",[](bool top){if(top){bounce_draw_top();bounce_draw_indicator();}else bounce_draw();});
            bounce_cancel_input();
        }
        graphite_performance_contrast();
        std::printf("pass graphite extra: fixed roles, real-main held-L + ready bounce, 2 frozen dual pairs, peak %d/8192; neutral+lilac/0 bounds/0 collisions; 48 progress states+retry/%d native glyph pixels >=4.5 nominal contrast\n",peak,graphite_contrast_pixels);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL graphite extra: %s\n",e.what());return 1;}
}
}
