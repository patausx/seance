// Appended to existing real main/editor harness; only the named groups run.
#include "../platform/3ds/editor_style.h"
namespace editor_gpu {
bool fail=false,frame=false;
unsigned inits=0,deletes=0,flushes=0,images=0;
std::set<C3D_Tex*> textures;
uint32_t texel(C3D_Tex* t,int x,int y) {
    x=std::clamp(x,0,63);y=std::clamp(y,0,63);unsigned index=0;
    for(unsigned b=0;b<3;++b){index|=((unsigned(x)>>b)&1)<<(2*b);index|=((unsigned(y)>>b)&1)<<(2*b+1);}
    const auto* p=static_cast<const unsigned char*>(t->data)+4*(((y/8)*8+x/8)*64+index);
    return 0xff000000u|unsigned(p[3])|unsigned(p[2])<<8|unsigned(p[1])<<16;
}
void begin(){frame=true;editor_style::frame();frame=false;}
}
bool C3D_TexInit(C3D_Tex*t,u16 w,u16 h,GPU_TEXCOLOR fmt){
    CHECK(!editor_gpu::frame&&w==64&&h==64&&fmt==GPU_RGBA8);++editor_gpu::inits;
    if(editor_gpu::fail)return false;
    CHECK(!editor_gpu::textures.count(t));t->data=std::malloc(w*h*4);CHECK(t->data);t->width=w;t->height=h;
    std::memset(t->data,0xa5,w*h*4);editor_gpu::textures.insert(t);return true;
}
void C3D_TexSetFilter(C3D_Tex*,GPU_TEXTURE_FILTER_PARAM a,GPU_TEXTURE_FILTER_PARAM b){CHECK(a==GPU_LINEAR&&b==GPU_LINEAR);}
void C3D_TexSetWrap(C3D_Tex*,GPU_TEXTURE_WRAP_PARAM a,GPU_TEXTURE_WRAP_PARAM b){CHECK(a==GPU_CLAMP_TO_EDGE&&b==GPU_CLAMP_TO_EDGE);}
void C3D_TexFlush(C3D_Tex*t){CHECK(editor_gpu::frame&&editor_gpu::textures.count(t));++editor_gpu::flushes;}
void C3D_TexBind(int slot,C3D_Tex*t){CHECK(editor_gpu::frame&&slot==0&&editor_gpu::textures.count(t));}
void C3D_TexDelete(C3D_Tex*t){CHECK(!editor_gpu::frame&&editor_gpu::textures.erase(t)==1);std::free(t->data);t->data=nullptr;++editor_gpu::deletes;}
bool C2D_DrawImageAt(C2D_Image im,float x,float y,float z,const C2D_ImageTint*tint,float sx,float sy){
    CHECK(!editor_gpu::frame&&editor_gpu::textures.count(im.tex)&&im.subtex&&z==0&&sx==1&&sy==1);
    const auto& uv=*im.subtex;CHECK(x>=0&&y>=0&&x+uv.width<=width&&y+uv.height<=240);
    const unsigned alpha=tint?tint->corners[0].color>>24:255;unsigned c;
    if(tint)for(auto t:tint->corners)CHECK(t.blend==0&&(t.color>>24)==alpha);
    ++objects;++editor_gpu::images;
    // Test-boundary cache only: opaque full sheets are immutable between GPU
    // uploads. Avoid resampling identical 96k pixels thousands of times; partial
    // restoration still evaluates its own UVs independently for the crop proof.
    struct Cached {C3D_Tex* texture=nullptr;unsigned upload=~0u;Tex3DS_SubTexture uv{};std::vector<u32> pixels;};
    static Cached cached[2];auto& cache=cached[width==400?1:0];
    const bool full=x==0&&y==0&&uv.width==width&&uv.height==240&&alpha==255;
    const bool same=cache.texture==im.tex&&cache.upload==editor_gpu::flushes&&cache.uv.left==uv.left&&
        cache.uv.right==uv.right&&cache.uv.top==uv.top&&cache.uv.bottom==uv.bottom;
    if(full&&same){pixels=cache.pixels;return true;}
    for(int yy=int(y);yy<int(y)+uv.height;++yy)for(int xx=int(x);xx<int(x)+uv.width;++xx){
        const double tx=(uv.left+(xx+.5-x)/uv.width*(uv.right-uv.left))*64-.5;
        const double ty=(1-uv.top+(yy+.5-y)/uv.height*(uv.top-uv.bottom))*64-.5;
        const int ix=int(std::floor(tx)),iy=int(std::floor(ty));const double fx=tx-ix,fy=ty-iy;
        const auto a=editor_gpu::texel(im.tex,ix,iy),b=editor_gpu::texel(im.tex,ix+1,iy);
        const auto d=editor_gpu::texel(im.tex,ix,iy+1),e=editor_gpu::texel(im.tex,ix+1,iy+1);
        c=alpha<<24;
        for(unsigned sh:{0u,8u,16u})c|=unsigned(std::lround((1-fy)*((1-fx)*((a>>sh)&255)+fx*((b>>sh)&255))+fy*((1-fx)*((d>>sh)&255)+fx*((e>>sh)&255))))<<sh;
        pixel(xx,yy,c);
    }
    if(full){cache.texture=im.tex;cache.upload=editor_gpu::flushes;cache.uv=uv;cache.pixels=pixels;}
    return true;
}
namespace editor_fullscreen_cases {
namespace e=editor_style;
namespace f=fader_lancet_cases;
int duals=0,peak=0,materials=0;
void prepare(){e::capture();editor_gpu::begin();}
void geometry(){
    if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"fullscreen %s w%d outside %d/%d overlaps%d\n",f::case_name.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
f::Pair render_pair(f::Scene&s,bool detached=true){
    prepare();const int dirty=mutations;const auto clock=s.engine->transport_frame;const auto uploads=editor_gpu::flushes;
    f::Pair out;
    module_inspector_surface(true);{f::AudioHidden hidden;top_draw();}geometry();out.top=pixels;out.upper=objects;
    module_inspector_surface(false);{f::AudioHidden hidden;editor_draw();}
    // The translucent MOD overlay intentionally crosses underlying text.
    if(param_lens_is_open()){texts.clear();text_overlaps=0;}geometry();out.bottom=pixels;out.lower=objects;
    CHECK(out.upper+out.lower<=8192);peak=std::max(peak,out.upper+out.lower);++duals;
    if(detached){
        const auto again=[&](){
            module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==out.top&&objects==out.upper);
            module_inspector_surface(false);{ModuleInspectorNoEngine hidden;f::lower(*s.unit);
                if(param_lens_is_open()){texts.clear();text_overlaps=0;param_lens_draw_bottom();}}
            CHECK(pixels==out.bottom&&objects==out.lower);
        };
        again();{f::PoisonAfterCapture poison(s);again();}
    }
    CHECK(mutations==dirty&&s.engine->transport_frame==clock&&editor_gpu::flushes==uploads);return out;
}
void material(){
    e::shutdown();editor_gpu::fail=true;CHECK(!e::init());
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Grimoire,palette::Theme::BlackMass,palette::Theme::Custom}){
        palette::set(theme);prepare();
        for(bool top:{false,true}){
            module_inspector_surface(top);e::paper(width);CHECK(objects<=int(top?240:192));const auto full=pixels;
            for(int y:{0,17,119,239})for(int x:{0,41,159,319})CHECK(pixels[y*width+x]==e::sample(top,x+.5f,y+.5f));
            ui::rect(17,39,53,21,palette::spot());e::paper_rect(17,39,53,21);CHECK(pixels==full);++materials;
        }
    }
    e::shutdown();editor_gpu::fail=false;CHECK(e::init());
    const auto allocations=editor_gpu::inits;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Grimoire,palette::Theme::BlackMass,palette::Theme::Custom}){
        palette::set(theme);const auto before=editor_gpu::flushes;e::capture();CHECK(editor_gpu::flushes==before);editor_gpu::begin();
        const auto uploaded=editor_gpu::flushes;CHECK(uploaded==before+1);
        for(bool top:{false,true}){
            module_inspector_surface(top);e::paper(width);CHECK(objects==1);const auto full=pixels;
            for(int y:{0,17,119,239})for(int x:{0,41,159,319})for(unsigned shift:{0u,8u,16u})
                CHECK(std::abs(int((pixels[y*width+x]>>shift)&255)-int((e::sample(top,x+.5f,y+.5f)>>shift)&255))<=1);
            ui::rect(17,39,53,21,palette::spot());e::paper_rect(17,39,53,21);
            // Crop UVs are floats; byte rounding at an exact half can differ by
            // one between a whole-sheet quad and its clipped quad in this host
            // sampler. Outside the restored region must remain strictly exact.
            for(int yy=0;yy<240;++yy)for(int xx=0;xx<width;++xx){const int at=yy*width+xx;
                if(xx<17||xx>=70||yy<39||yy>=60)CHECK(pixels[at]==full[at]);
                else for(unsigned shift:{0u,8u,16u})CHECK(std::abs(int((pixels[at]>>shift)&255)-int((full[at]>>shift)&255))<=1);}
            module_inspector_surface(top);menu_style::paper(width);const auto menu=pixels;
            prepare();module_inspector_surface(top);menu_style::paper(width);CHECK(pixels==menu);
            ++materials;
        }
        for(int i=0;i<4;++i){host_time_ms+=999999;prepare();CHECK(editor_gpu::flushes==uploaded&&editor_gpu::inits==allocations);}
    }
    palette::set(palette::Theme::Pink);prepare();palette::animate_to(palette::Theme::Graphite,0);
    for(unsigned t:{0u,35u,84u,85u,120u,170u}){palette::capture(t);prepare();module_inspector_surface(true);e::paper(400);CHECK(objects==1);++materials;}
}
void modules(){
    f::sample_fixture();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Grimoire,palette::Theme::BlackMass,palette::Theme::Custom}){
        palette::set(theme);
        for(const auto& item:f::catalogue){
            f::case_name=std::string(item.name)+" theme"+std::to_string(int(theme));f::Scene s(item.kind);render_pair(s);
            if(theme!=palette::Theme::Graphite&&theme!=palette::Theme::Pink)continue;
            for(int v:{0,32767}){s.set(item.param,q15(v));s.capture();render_pair(s);}
            s.restore();s.signal(24000);render_pair(s);s.signal(24000,0);render_pair(s);
            s.unit->bypass=true;s.capture();render_pair(s);s.unit->bypass=false;s.engine->chains[0].mute=true;s.capture();render_pair(s);
            s.restore();const q15 base=s.unit->params[item.param].base;
            g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
            CHECK(s.engine->set_step_lock(0,4,s.slot,item.param,14000));s.capture();render_pair(s);CHECK(s.unit->params[item.param].base==base);
            s.restore();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());param_lens_touch_release();s.capture();render_pair(s);param_lens_cancel();
        }
    }
}
void alternates(){
    palette::set(palette::Theme::Graphite);
    {f::Scene s(UnitKind::Osc);auto&u=*static_cast<OscUnit*>(s.unit);u.clean=false;for(bool b:{false,true}){u.shape_morph=b;s.capture();render_pair(s);}}
    {f::Scene s(UnitKind::Fm);for(int p:{FmUnit::P_RATIO_FINE,FmUnit::P_PITCH_ENV}){fm_focus_select(p);s.capture();render_pair(s);}}
    {f::Scene s(UnitKind::Grain);auto&u=*static_cast<GrainUnit*>(s.unit);for(bool b:{false,true}){u.clean=b;u.clock_sync=b;s.capture();render_pair(s);}}
    {f::Scene s(UnitKind::Comp);for(int p:{CompUnit::P_KNEE,CompUnit::P_HP,CompUnit::P_CLOCK}){comp_focus_select(p);s.capture();render_pair(s);}}
    {f::Scene s(UnitKind::Reverb);for(bool b:{false,true}){static_cast<ReverbUnit*>(s.unit)->set_modern(b);s.capture();render_pair(s);}}
    {f::Scene s(UnitKind::SamplePlayer);auto&u=*static_cast<PlayUnit*>(s.unit);
        play_focus_touch_body(u,160,110);play_focus_touch_release(u);s.capture();render_pair(s,false);editor_back();s.capture();render_pair(s,false);}
}
void needles(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);
        for(bool vertical:{false,true})for(int edit:{0,1,30,59,60})for(int live:{0,30,60})for(bool linked:{false,true}){
            module_inspector_surface(false);
            const module_fader::State state{linked,false,false};
            if(vertical)module_fader::needle_vertical(180,90,150,150-edit,150-live,state);
            else module_fader::needle_horizontal(20,80,110,20+edit,20+live,state);
            CHECK(objects<=40&&!outside_rects&&!outside_lines);
            const int index=vertical?(150-live)*320+180:110*320+20+live;
            if(linked||live!=edit)CHECK(pixels[index]==palette::spot());
            else {const int x=vertical?180:20+edit,y=vertical?150-edit:110;CHECK(pixels[y*320+x]==palette::spot());
                for(int d=-4;d<=4;++d)CHECK(pixels[(y+(vertical?0:d))*320+x+(vertical?d:0)]==palette::spot());}
        }
    }
}
void capture(const std::filesystem::path&dir){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);f::sample_fixture();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);
        const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        for(const auto& item:f::catalogue){
            if(item.kind==UnitKind::Filter){filter_interaction_cases::Scene s;prepare();
                filter_interaction_cases::surface(s,true);dump_surface(dir,prefix+item.name+"_top");
                filter_interaction_cases::surface(s,false);dump_surface(dir,prefix+item.name+"_bottom");continue;}
            f::Scene s(item.kind);render_pair(s);module_inspector_surface(true);top_draw();dump_surface(dir,prefix+item.name+"_top");
            module_inspector_surface(false);editor_draw();dump_surface(dir,prefix+item.name+"_bottom");
        }
    }
    std::printf("fullscreen capture: 56 duals, native geometry, software texture sampling; no regression groups\n");
}
int run(){int total=0,failed=0;auto test=[&](const char*name,auto fn){++total;try{fn();std::printf("ok: %s\n",name);}catch(const std::exception&ex){++failed;std::fprintf(stderr,"FAIL %s: %s\n",name,ex.what());}std::fflush(stdout);};
    test("material init/failure/capture/upload/UV/crop/themes/menu isolation",material);
    test("new needles and linked live centers",needles);
    test("28 modules five themes frozen duals + lock/mute/bypass/MOD",modules);
    test("legacy/subpages retain full work area",alternates);
    test("filter input/log drag/locks/lens/ownership regression",[]{CHECK(filter_interaction_cases::proof()==0);});
    e::shutdown();CHECK(editor_gpu::textures.empty());
    std::printf("fullscreen editor: %d/%d groups; %d duals; %d material cases; peak %d/8192 raw host objects; ASAN/UBSAN; software GPU boundary not hardware\n",total-failed,total,duals,materials,peak);return failed?1:0;
}
}
int main(int argc,char**argv){
    theme_test::select();editor_style::init();editor_fullscreen_cases::prepare();
    if(argc==2&&std::string(argv[1])=="--proof-only")return editor_fullscreen_cases::run();
    if(argc==3&&std::string(argv[1])=="--capture-only"){editor_fullscreen_cases::capture(argv[2]);editor_style::shutdown();return 0;}
    return 2;
}
