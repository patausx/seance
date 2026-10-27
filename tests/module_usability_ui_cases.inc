// Appended to the existing raster fixtures with their main renamed. Only the
// groups below run. HID dispatch/editor/capture and patch IO are current code.
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace usability_cases {
namespace f=fader_lancet_cases;
namespace hid=osc_input_main;
using Scene=f::Scene;
using A=ArpUnit;
int duals=0,peak=0,toggles=0,invalid_files=0;
std::string where;
void neutral(){param_lens_cancel();hid::nav_opened=false;if(g_eng)hid::frame(0,0);g_drag=Drag::None;}
void down(int x,int y,u32 mods=0){hid::touch={x,y};hid::frame(KEY_TOUCH,KEY_TOUCH|mods);}
void move(int x,int y,u32 mods=0){hid::touch={x,y};hid::frame(0,KEY_TOUCH|mods);}
void up(u32 mods=0){hid::frame(0,mods,KEY_TOUCH);}
void tap(int x,int y){down(x,y);up();}
void toggle(int note){tap(35+76*note,113);++toggles;}
bool has(const std::vector<TextRecord>& list,const std::string& value){return std::any_of(list.begin(),list.end(),[&](const TextRecord&t){return t.text==value;});}
using Params=std::array<Param,MAX_PARAMS>;
Params params(const Unit& u){Params out;std::copy(std::begin(u.params),std::end(u.params),out.begin());return out;}
void same(const Unit& u,const Params& old){for(int p=0;p<u.num_params;++p){const auto&a=old[p];const auto&b=u.params[p];CHECK(a.base==b.base&&a.mod_depth==b.mod_depth&&a.src==b.src&&a.mod_enabled==b.mod_enabled);}}
struct Pair {std::vector<u32> top,bottom;std::vector<TextRecord> upper,lower;int top_count=0,bottom_count=0;};
void bounds(bool overlay){
    if(outside_rects||outside_lines||(!overlay&&text_overlaps))std::fprintf(stderr,"usability %s w%d: rect%d line%d text%d\n",where.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines);
    if(!overlay)CHECK(!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
    if(overlay){const auto first=std::find_if(texts.begin(),texts.end(),[](const TextRecord&t){return t.text=="link";});CHECK(first!=texts.end());
        for(auto a=first;a!=texts.end();++a)for(auto b=first;b!=a;++b)CHECK(a->x>=b->x+b->w||a->x+a->w<=b->x||a->y>=b->y+b->h||a->y+a->h<=b->y);}
}
Pair draw(Scene&s){
    Pair r;const auto old=params(*s.unit);const int dirty=mutations;const auto clock=s.engine->transport_frame;
    f::surface(true);{f::AudioHidden hidden;top_draw();}bounds(false);r.top=pixels;r.upper=texts;r.top_count=objects;
    f::surface(false);{f::AudioHidden hidden;editor_draw();}bounds(param_lens_is_open());r.bottom=pixels;r.lower=texts;r.bottom_count=objects;
    CHECK(r.top_count+r.bottom_count<=8192);peak=std::max(peak,r.top_count+r.bottom_count);++duals;
    const auto again=[&](){
        f::surface(true);{ModuleInspectorNoEngine hidden;top_draw();}bounds(false);CHECK(pixels==r.top&&objects==r.top_count);
        f::surface(false);{ModuleInspectorNoEngine hidden;f::lower(*s.unit);param_lens_draw_bottom();}bounds(param_lens_is_open());CHECK(pixels==r.bottom&&objects==r.bottom_count);
    };
    again();{f::PoisonAfterCapture poison(s);again();}
    same(*s.unit,old);CHECK(mutations==dirty&&s.engine->transport_frame==clock);return r;
}
void lens(Scene&s,int p){
    neutral();if(s.kind==UnitKind::Stranular)stranular_focus_select(p);else module_inspector_select(s.kind,p);
    s.capture();hid::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());s.capture();
}
void arp_ui(){
    Scene s(UnitKind::Arp);neutral();auto& a=*static_cast<A*>(s.unit);
    // Every authored interval, every independent slot, including noncanonical
    // raw values: OFF is still raw zero, enable restores the exact stored raw.
    for(int i=0;i<4;++i)for(int note=-24;note<=24;++note){
        const int p=A::P_NOTE1+i;const q15 raw=q15(std::min(32767,int(A::note_value(note))+17));s.set(p,raw);
        auto& q=a.params[p];q.src=ModSource::SigilC;q.mod_depth=-7000;q.mod_enabled=false;
        const auto before=params(a);const int dirty=mutations;
        toggle(i);CHECK(q.base==0&&A::note_semitones(q.base)==A::NOTE_OFF);CHECK(a.note_recall.mask&(1u<<i));CHECK(a.note_recall.values[i]==raw);
        s.capture();a.reset();CHECK(a.note_recall.values[i]==raw);
        toggle(i);CHECK(q.base==raw&&!(a.note_recall.mask&(1u<<i)));same(a,before);CHECK(mutations==dirty+2);
    }
    for(int i=0;i<4;++i){s.set(A::P_NOTE1+i,A::note_value(i*5-7));toggle(i);}CHECK(a.note_recall.mask==15);
    const auto remembered=a.note_recall;
    // Focus/lens/navigation do not erase base recall; it belongs to the Unit.
    g_overlay=Overlay::None;arp_focus_capture();g_overlay=Overlay::Editor;s.capture();
    lens(s,A::P_GATE);neutral();
    for(int i=0;i<4;++i){toggle(i);CHECK(a.params[A::P_NOTE1+i].base==remembered.values[i]);}
    s.set(A::P_NOTE4,0);a.note_recall={};toggle(3);CHECK(a.params[A::P_NOTE4].base==A::note_value(0));
    // A different card cannot inherit another card's disabled note.
    s.set(A::P_NOTE2,A::note_value(19));toggle(1);auto* original=s.engine->replace_unit(0,0,new A);
    auto* other=static_cast<A*>(s.engine->chains[0].units[0]);other->params[A::P_NOTE2].base=0;
    toggle(1);CHECK(other->params[A::P_NOTE2].base==A::note_value(0));delete s.engine->replace_unit(0,0,original);
    s.capture();toggle(1);CHECK(a.params[A::P_NOTE2].base==A::note_value(19));
    // Injected legacy lock path stays isolated from base metadata.
    s.set(A::P_NOTE1,A::note_value(-11));toggle(0);const auto base_memory=a.note_recall;
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=6;
    CHECK(s.engine->set_step_lock(0,6,0,A::P_NOTE1,A::note_value(16)));
    toggle(0);CHECK(seq_display_value(0,A::P_NOTE1,123)==0);s.capture();toggle(0);
    CHECK(seq_display_value(0,A::P_NOTE1,0)==A::note_value(16));CHECK(a.params[A::P_NOTE1].base==0);
    CHECK(a.note_recall.mask==base_memory.mask&&a.note_recall.values[0]==base_memory.values[0]);
    s.engine->clear_step_locks(0,6);for(int p=0;p<4;++p)CHECK(s.engine->set_step_lock(0,6,0,p,1000));
    const auto hash=pattern_hash(s.engine->pattern);toggle(1);CHECK(pattern_hash(s.engine->pattern)==hash&&g_seq_lock_flash>0);
    CHECK(a.note_recall.mask==base_memory.mask);g_seq_lock_armed=false;g_view=View::Chains;neutral();
}
using Bytes=std::vector<uint8_t>;
struct Temp {
    std::filesystem::path path;
    Temp(){char pattern[]="/tmp/seance-usability-patch-XXXXXX";const char* p=mkdtemp(pattern);CHECK(p);path=p;}
    ~Temp(){std::filesystem::remove_all(path);}
};
Bytes read_file(const std::filesystem::path&p){std::ifstream file(p,std::ios::binary);CHECK(file.good());return Bytes(std::istreambuf_iterator<char>(file),{});}
void write_file(const std::filesystem::path&p,const Bytes&data){std::ofstream file(p,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));CHECK(file.good());}
uint32_t le32(const Bytes&data,size_t at){CHECK(at+4<=data.size());return uint32_t(data[at])|(uint32_t(data[at+1])<<8)|(uint32_t(data[at+2])<<16)|(uint32_t(data[at+3])<<24);}
std::vector<Bytes> chunks(const Bytes&data){CHECK(data.size()>=5);std::vector<Bytes> out;
    for(size_t at=5;at<data.size();){CHECK(at+8<=data.size());const size_t end=at+8+le32(data,at+4);CHECK(end<=data.size());out.emplace_back(data.begin()+at,data.begin()+end);at=end;}return out;}
std::string id(const Bytes&chunk){CHECK(chunk.size()>=8);return std::string(chunk.begin(),chunk.begin()+4);}
Bytes assemble(const Bytes&source,const std::vector<Bytes>&list){Bytes out(source.begin(),source.begin()+5);for(const auto&c:list)out.insert(out.end(),c.begin(),c.end());return out;}
void arp_storage(){
    Temp temp;Scene s(UnitKind::Arp);neutral();auto& a=*static_cast<A*>(s.unit);
    const int notes[]={-24,-1,7,24};
    for(int i=0;i<4;++i){s.set(A::P_NOTE1+i,A::note_value(notes[i]));toggle(i);}const auto remembered=a.note_recall;CHECK(remembered.mask==15);
    std::unique_ptr<Unit> copied(clone_unit(&a));CHECK(copied&&copied->kind==UnitKind::Arp);
    const auto& recall=static_cast<A*>(copied.get())->note_recall;CHECK(recall.mask==15);
    for(int i=0;i<4;++i)CHECK(recall.values[i]==remembered.values[i]&&copied->params[A::P_NOTE1+i].base==0);
    const auto fullpath=temp.path/"recall.snc";CHECK(patchio::save(*s.engine,fullpath.c_str()));const Bytes full=read_file(fullpath);
    auto list=chunks(full);const auto pos=std::find_if(list.begin(),list.end(),[](const Bytes&c){return id(c)=="ARPM";});CHECK(pos!=list.end());const Bytes arpm=*pos;
    CHECK(arpm.size()==21&&le32(arpm,4)==13&&arpm[8]==1&&arpm[9]==1&&arpm[12]==15);
    list.erase(pos);const Bytes old=assemble(full,list);
    a.note_recall={};const auto oldpath=temp.path/"without.snc";CHECK(patchio::save(*s.engine,oldpath.c_str()));CHECK(read_file(oldpath)==old);a.note_recall=remembered;
    // Metadata may precede CORE; old files lack ARPM entirely.
    list.insert(list.begin(),arpm);const auto reordered=temp.path/"reordered.snc";write_file(reordered,assemble(full,list));
    auto loaded=std::make_unique<Engine>();patchio::SampleManifest manifest;
    CHECK(patchio::load(*loaded,reordered.c_str(),&manifest));auto* r=static_cast<A*>(loaded->chains[0].units[0]);CHECK(r&&r->kind==UnitKind::Arp&&r->note_recall.mask==15);
    for(int i=0;i<4;++i)CHECK(r->note_recall.values[i]==remembered.values[i]&&r->params[A::P_NOTE1+i].base==0);
    CHECK(patchio::save(*loaded,(temp.path/"resaved.snc").c_str()));CHECK(read_file(temp.path/"resaved.snc")==full);
    // Actual UI enables correctly after reload without consulting the old UI/card.
    g_eng=loaded.get();g_edit_slot=g_sel_slot=0;arp_focus_capture();for(int i=0;i<4;++i){toggle(i);CHECK(r->params[A::P_NOTE1+i].base==remembered.values[i]);}
    g_eng=s.engine.get();CHECK(patchio::load(*loaded,oldpath.c_str(),&manifest));CHECK(!static_cast<A*>(loaded->chains[0].units[0])->note_recall.mask);
    // Reminder is inaudible metadata: same raw pool/modulation -> exact stereo PCM.
    CHECK(patchio::load(*loaded,fullpath.c_str(),&manifest));auto plain=std::make_unique<Engine>();CHECK(patchio::load(*plain,oldpath.c_str(),&manifest));
    for(auto* eng:{loaded.get(),plain.get()}){auto* arp=static_cast<A*>(eng->chains[0].units[0]);arp->params[A::P_NOTE1].base=A::note_value(0);arp->params[A::P_NOTE1].snap();eng->transport_reset();eng->transport_play();}
    std::array<q15,512> left{},right{};for(int block=0;block<64;++block){loaded->render(left.data(),256);plain->render(right.data(),256);CHECK(left==right);}
    // Invalid/truncated/duplicate metadata must fail transactionally.
    auto victim=std::make_unique<Engine>();victim->chains[2].units[1]=new OscUnit;Unit* identity=victim->chains[2].units[1];victim->set_bpm(137);
    auto reject=[&](const Bytes&bad){const auto p=temp.path/"bad.snc";write_file(p,bad);CHECK(!patchio::load(*victim,p.c_str(),&manifest));CHECK(victim->chains[2].units[1]==identity&&victim->bpm==137);++invalid_files;};
    auto without=chunks(old);
    for(int failure=0;failure<10;++failure){auto broken=arpm;auto rows=without;
        if(failure==0)broken[8]=2; // version
        if(failure==1)broken[9]=0; // count
        if(failure==2)broken[10]=NUM_CHAINS;
        if(failure==3)broken[11]=UNITS_PER_CHAIN;
        if(failure==4)broken[12]=0;
        if(failure==5)broken[12]=16;
        if(failure==6){broken[13]=broken[14]=0;}
        if(failure==7){broken[12]=1;} // noncanonical inactive values
        if(failure==8){broken[10]=2;broken[11]=1;} // non-ARP address
        rows.push_back(broken);if(failure==9)rows.push_back(broken);reject(assemble(full,rows));}
    auto truncated=full;truncated.resize(6);reject(truncated);
    auto rows=without;auto duplicate=arpm;duplicate[4]=24;duplicate[9]=2;duplicate.insert(duplicate.end(),arpm.begin()+10,arpm.end());rows.push_back(duplicate);reject(assemble(full,rows));
    // Memory moves with copy/move/delete-undo, never a global pointer-keyed cache.
    CHECK(s.engine->move_unit_before(0,0,UNITS_PER_CHAIN)>=0);CHECK(a.note_recall.mask==15);
    g_eng=s.engine.get();neutral();
}
void noise(){
    using N=NoiseUnit;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        palette::set(theme);Scene s(UnitKind::Noise);neutral();s.set(N::P_DENSITY,19000);noise_focus_select(N::P_DENSITY);
        auto&q=s.unit->params[N::P_DENSITY];q.src=ModSource::SigilB;q.mod_depth=-9000;q.mod_enabled=true;
        s.set(N::P_COLOUR,10922);const auto before=params(*s.unit);s.capture();where="noise pink density";const auto pink=draw(s);same(*s.unit,before);CHECK(has(pink.lower,"pink / density stored"));
        int accent=0;for(int y=68;y<99;++y)for(int x=14;x<=152;++x)accent+=pink.bottom[y*320+x]==palette::spot();CHECK(accent==0);
        for(int colour:{0,21844,32767}){s.set(N::P_COLOUR,q15(colour));s.capture();where="noise used density";const auto image=draw(s);CHECK(!has(image.lower,"pink / density stored"));
            accent=0;for(int y=68;y<99;++y)for(int x=14;x<=152;++x)accent+=image.bottom[y*320+x]==palette::spot();CHECK(accent>0);
            CHECK(q.base==19000&&q.src==ModSource::SigilB&&q.mod_depth==-9000&&q.mod_enabled);}
        // The actual effective colour, not the stored colour, owns dormancy.
        s.set(N::P_COLOUR,0);s.unit->params[N::P_COLOUR].effective=10922;s.capture();CHECK(has(draw(s).lower,"pink / density stored"));
        s.set(N::P_COLOUR,10922);s.unit->params[N::P_COLOUR].effective=0;s.capture();CHECK(!has(draw(s).lower,"pink / density stored"));
        lens(s,N::P_DENSITY);const auto saved=params(*s.unit);draw(s);same(*s.unit,saved);neutral();
    }
}
void strn(){
    using S=StranularUnit;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        palette::set(theme);Scene s(UnitKind::Stranular);neutral();auto&u=*static_cast<S*>(s.unit);
        for(int p=0;p<12;++p){s.set(p,q15(p==S::P_PITCH||p==S::P_PITCH_ENV?-16384:p==S::P_FREEZE||p==S::P_HOLD||p==S::P_SYNC?0:16384));}
        for(int p=0;p<12;++p){where="strn lens "+std::to_string(p);lens(s,p);const auto old=params(u);const auto image=draw(s);char expected[24];stranular_focus_value_text(expected,sizeof expected,p,u.params[p].base);
            CHECK(has(image.lower,expected));same(u,old);neutral();}
        const struct {int p; q15 raw;const char* expected;} examples[]={
            {S::P_SIZE,0,"4ms"},{S::P_SIZE,32767,"256ms"},{S::P_PITCH,-32767,"-24.00st"},
            {S::P_PITCH_ENV,32767,"+24.00st"},{S::P_SPRAY,32767,"256ms"},
            {S::P_REVERSE,32767,"100%"},{S::P_MIX,0,"0%"},{S::P_CLUSTER,32767,"x4"},
            {S::P_FREEZE,0,"off"},{S::P_HOLD,32767,"on"}};
        for(const auto&c:examples){s.set(c.p,c.raw);lens(s,c.p);where="strn units "+std::string(c.expected);CHECK(has(draw(s).lower,c.expected));neutral();}
        u.long_mode=true;s.set(S::P_SIZE,32767);lens(s,S::P_SIZE);CHECK(has(draw(s).lower,"1024ms"));neutral();
        const char* divisions[]={"1/16","1/8","1/4","1/2"};
        for(int div=0;div<4;++div){u.sync_division=uint8_t(div);s.set(S::P_SYNC,32767);lens(s,S::P_DENS);CHECK(has(draw(s).lower,std::string(divisions[div])+" sync"));neutral();}
        lens(s,S::P_PITCH);up(KEY_L);const q15 original=u.params[S::P_PITCH].base;
        down(140,102,KEY_L);move(143,102,KEY_L);CHECK(u.params[S::P_PITCH].base==std::min(32767,int(original)+3*65534/271));up(KEY_L);neutral();
    }
    char text[8];stranular_focus_cancel();stranular_focus_value_text(text,sizeof text,0,0);CHECK(std::string(text)=="--");
}
void presentation(){
    const UnitKind kinds[]={UnitKind::Delay,UnitKind::Chorus,UnitKind::Arp,UnitKind::Noise,UnitKind::Stranular,UnitKind::Ghost};
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink})for(auto kind:kinds){
        palette::set(theme);Scene s(kind);neutral();
        for(int state=0;state<3;++state){s.unit->bypass=state==1;s.engine->chains[0].mute=state==2;s.capture();where="module "+std::to_string(int(kind))+" state "+std::to_string(state);draw(s);}
        s.unit->bypass=false;s.engine->chains[0].mute=false;
        for(int p=0;p<s.unit->num_params;++p){if(kind==UnitKind::Stranular)stranular_focus_select(p);else if(kind==UnitKind::Ghost)ghost_focus_select(p);else module_inspector_select(kind,p);
            s.capture();where="module "+std::to_string(int(kind))+" param "+std::to_string(p);draw(s);}
    }
}
int run(){int count=0,failed=0;
    const auto group=[&](const char* name,auto test){++count;try{test();std::printf("ok: usability %s\n",name);}catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL usability %s [%s]: %s\n",name,where.c_str(),e.what());param_lens_cancel();hid::nav_opened=false;g_drag=Drag::None;}};
    group("delay real-lift toggles / clear / cancellation",usability_gestures::delay_group);
    group("chorus longer upward drags / cancellation",usability_gestures::chorus_group);
    group("ghost fixed corridors / endpoints / cancellation",usability_gestures::ghost_group);
    group("arp all49x4 note recall / routes / lock isolation",arp_ui);
    group("arp copy / optional metadata / transaction / identical PCM",arp_storage);
    group("noise pink dormant density / retained routes / both themes",noise);
    group("strn native units / frozen lens / existing gesture law",strn);
    group("six modules / all parameters / frozen dual-screen draws",presentation);
    std::printf("module usability: %d/%d groups; %d frozen duals; %d note toggles; %d invalid files; shared peak %d/8192\n",count-failed,count,duals,toggles,invalid_files,peak);
    std::puts("zero inherited test groups; real main/editor/engine host only; no hardware/touch-feel/FPS claims");return failed?1:0;
}
}
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="--proof-only")return usability_cases::run();return 2;}
