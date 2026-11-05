// Included once in PLAY's private controller. No second Document/DSP owner.
enum class WordMenu:uint8_t {None,Tools,Mode,Trigger,Loop,Gate,Bypass,Quantize,Slice,AutoKind,Snap};
enum class WordOp:uint8_t {Back,Open,Menu,Confirm,Apply,ResetTrim,Nudge,Handle,Fine,ResetFades,CutFocus,CutReverse,CutMerge,Primary,AutoMethod,LiveArm,LiveStop,LiveMark,Listen,Seam,Toggle,SetParam,Mode,Slice,CycleSlice,Snap,Whole};
enum class WordConfirm:uint8_t {Replace,Clear,Save,Detach,Crop};
struct WordCell {layout::Box box{};char label[48]={},value[24]={};WordOp op=WordOp::Back;int arg=0,param=-1;
    Gesture gesture=Gesture::None;int64_t raw=0,live=0,lo=0,hi=32767;bool enabled=true,selected=false,linked=false,chevron=false;};
struct WordFrame {bool modal=false,confirm=false,surface=false;WordMenu menu=WordMenu::None;WordConfirm operation=WordConfirm::Replace;
    word_menu::Layout layout{};word_menu::Frame motion{};int count=0,selected=0,ax=0,ay=0;
    WordCell cells[12]{};char title[64]={},anchor[48]={},lines[3][48]={};} word_frame;
static_assert(std::is_trivially_copyable<WordFrame>::value&&sizeof(WordFrame)<2304,"bounded pointer-free PLAY words");
void inline_build(WordFrame&);
Frame word_upper;
struct WordParent {Frame native{};WordFrame snapshot{};Options v{};WordMenu menu=WordMenu::None;int selected=0,ax=0,ay=0;char anchor[48]={};};
struct WordController {WordMenu menu=WordMenu::None;WordConfirm operation=WordConfirm::Replace;bool confirm=false,dirty=false,quarantine=false;
    int selected=0,id=0,depth=0,ax=264,ay=14;char anchor[48]={};WordParent parents[8]{};
    Engine*engine=nullptr;Context source{};uint32_t ink=0,spot=0;word_menu::State motion{};
    struct Press {bool owned=false,armed=false;int index=-1,x=0,y=0;layout::Box box{};} press;
} words;
bool words_modal(){return words.menu!=WordMenu::None||words.confirm;}
bool words_surface(){return frame.active&&frame.v.page>Page::Loop;}
void words_cancel(bool quarantine){
    // Cancel navigation only: do not release an unrelated sounding PLAY voice.
    // Only PLAY owns this cancellation tail. Inactive capture also runs while
    // sigils/other pages drag; their tokens must not quarantine the next frame.
    const bool tail=words.quarantine||(quarantine&&(words.press.owned||stroke.kind!=Gesture::None||g_drag==Drag::PlaySliceSeam));
    if(words.depth){const auto&v=words.parents[0].v;view.page=v.page;view.sound=v.sound;view.selection_page=v.selection_page;}
    words.menu=WordMenu::None;words.confirm=words.dirty=false;words.depth=words.selected=words.id=0;
    words.press={};words.engine=nullptr;words.source={};words.motion={};words.anchor[0]=0;
    words.quarantine=tail;word_frame={};
}
bool words_push(){if(words.depth==8)return false;auto&p=words.parents[words.depth++];p.native=frame;p.snapshot=word_frame;p.v=view;p.menu=words.menu;p.selected=words.selected;p.ax=words.ax;p.ay=words.ay;std::memcpy(p.anchor,words.anchor,sizeof p.anchor);return true;}
void words_pop(){
    finish();words.press={};words.confirm=false;
    if(words.depth){const auto p=words.parents[--words.depth];
        // Workspace focus/viewport return exactly; audition and processing options
        // remain their actual controller owners, not stale tokens from the stack.
        const auto live=view;view=p.v;view.token=live.token;
        view.normalize=live.normalize;view.dc=live.dc;view.bake=live.bake;view.reverse=live.reverse;view.live=live.live;
        view.auto_count=live.auto_count;view.sensitivity=live.sensitivity;view.snap=live.snap;view.fine=live.fine;
        view.cut=live.cut;view.cut_end=live.cut_end; // real focus choices/marks survive navigation
        view.first=live.first;view.span=live.span;view.scale=live.scale; // explicit view commands persist too
        words.menu=p.menu;words.selected=p.selected;words.ax=p.ax;words.ay=p.ay;std::memcpy(words.anchor,p.anchor,sizeof words.anchor);
    }else {words.menu=WordMenu::None;view.page=view.selection_page;}
    words.dirty=true;
}
void words_root(){finish();words.depth=0;words.menu=WordMenu::None;words.confirm=false;words.press={};words.dirty=true;}
void words_open_menu(WordMenu menu,const char*anchor,int x,int y,int selected=0){
    if(!words_push())return;
    finish();words.menu=menu;words.confirm=false;words.selected=selected;
    words.ax=x;words.ay=y;std::snprintf(words.anchor,sizeof words.anchor,"%s",anchor);words.dirty=true;
    words.engine=g_eng;words.source=ctx;words.ink=m::ink();words.spot=m::spot();
}
void words_tools(){const char*labels[]={"trim","chop","loop"};const int i=int(view.selection_page);
    words_open_menu(WordMenu::Tools,labels[i],pl::tool(i).x0,40);}
void words_open(Page page){if(!words_push())return;finish();words.menu=WordMenu::None;words.confirm=false;
    sheet(page);if(page==Page::Config)view.group=Group::File;
    words.dirty=true;}
void words_confirm(WordConfirm op){if(!words_push())return;finish();words.menu=WordMenu::None;words.confirm=true;words.operation=op;words.selected=0;
    words.engine=g_eng;words.source=ctx;words.ink=m::ink();words.spot=m::spot();words.dirty=true;}
WordCell& word_cell(WordFrame&f,layout::Box b,const char*label,WordOp op,int arg=0,bool enabled=true,bool selected=false){
    auto&c=f.cells[f.count++];c.box=b;std::snprintf(c.label,sizeof c.label,"%s",label);c.op=op;c.arg=arg;c.enabled=enabled;c.selected=selected;return c;
}
WordCell& word_cell(WordFrame&f,int col,int row,const char*label,WordOp op,int arg=0,bool enabled=true,bool selected=false){return word_cell(f,pw::cell(col,row),label,op,arg,enabled,selected);}
void word_param(WordFrame&f,int col,int row,const char*label,int param,Gesture gesture,const char*value,int64_t raw,int64_t lo,int64_t hi,bool enabled=true){
    auto&c=word_cell(f,col,row,label,WordOp::SetParam,0,enabled);c.param=param;c.gesture=gesture;
    std::snprintf(c.value,sizeof c.value,"%s",value);c.raw=raw;c.lo=lo;c.hi=std::max(lo+1,hi);c.live=param<0?raw:frame.live[param];c.linked=param>=0&&(frame.links&(1u<<param));
}
void words_build(WordFrame&f){
    f={};f.modal=words_modal();f.confirm=words.confirm;f.menu=words.menu;f.operation=words.operation;f.surface=words_surface();f.selected=words.selected;
    const auto&v=frame.v;const auto&d=frame.d;const auto&e=d.edit;
    const bool can=frame.loaded&&!frame.busy&&!frame.missing&&!frame.mismatch;
    const bool exact=can&&d.modern,cut=exact&&d.cuts.valid(v.cut,frame.frames);
    char t[64],a[24];
    if(f.confirm){
        static const char*titles[]={"replace cuts?","clear cuts?","save selection copy?","detach raw source?","crop trim copy?"};
        std::snprintf(f.title,sizeof f.title,"%s",titles[int(f.operation)]);
        const char*l0=f.operation==WordConfirm::Replace?(v.auto_count==0?"method: attack":v.auto_count==4?"method: equal 4":v.auto_count==8?"method: equal 8":"method: equal 16"):
            f.operation==WordConfirm::Clear?"remove all shared cuts":f.operation==WordConfirm::Save?"source processing options only":"copy source audio / unprocessed";
        std::snprintf(f.lines[0],48,"%s",l0);
        if(f.operation==WordConfirm::Save)std::snprintf(f.lines[0],48,"%s / %s / %s / %s",v.dc?"dc":"raw dc",v.normalize?"-1db":"no norm",v.bake?"fades":"no fades",v.reverse?"reverse":"forward");
        if(f.operation==WordConfirm::Crop){seconds(a,sizeof a,frame.end-frame.start);std::snprintf(f.lines[0],48,"trim %s / raw audio",a);}
        std::snprintf(f.lines[1],48,"%s",f.operation<=WordConfirm::Clear?"score cut ids stay unchanged":f.operation==WordConfirm::Save?"keep current source":"use copy on this card");
        std::snprintf(f.lines[2],48,"%s","original audio stays intact");
        word_cell(f,pl::footer(0),"cancel",WordOp::Back);
        word_cell(f,pl::footer(2),"confirm",WordOp::Apply,int(f.operation),can&&(f.operation!=WordConfirm::Replace||frame.overview_ready||v.auto_count!=0),true);return;
    }
    if(f.modal){
        f.ax=words.ax;f.ay=words.ay;std::memcpy(f.anchor,words.anchor,sizeof f.anchor);
        // Build labels first, then derive the entire list/hit layout from the
        // actual invoking word and the widest rendered item, never menu kind.
        auto item=[&](const char*l,WordOp op,int arg=0,bool enabled=true){word_cell(f,layout::Box{},l,op,arg,enabled);};
        auto detail=[&](const char*l,Page p){item(l,WordOp::Open,int(p));};
        switch(f.menu){
        case WordMenu::Tools:
            if(v.selection_page==Page::Cuts){detail("auto cuts >",Page::Auto);detail("live cuts >",Page::Live);detail("part / nudge >",Page::Edit);detail("snap >",Page::Zoom);}
            else if(v.selection_page==Page::Trim){detail("fades >",Page::Fades);item("reset trim",WordOp::ResetTrim,0,exact);detail("snap >",Page::Zoom);}
            else {detail("loop seam >",Page::Edit);detail("snap >",Page::Zoom);}break;
        case WordMenu::Mode:for(int i=0;i<3;++i)item(i==0?"normal":i==1?"stretch":"tape",WordOp::Mode,i,can);break;
        case WordMenu::Trigger:item("gate",WordOp::SetParam,C::P_MODE*256+1,can);item("one shot",WordOp::SetParam,C::P_MODE*256,can);break;
        case WordMenu::Gate:for(int i=0;i<6;++i){std::snprintf(t,sizeof t,"%s step",play_mode::gate_label(play_mode::gate_value(i)));item(t,WordOp::SetParam,C::P_GATE*256+i,exact&&play_mode::is_gate(d.bases[C::P_MODE]));}break;
        case WordMenu::Loop:case WordMenu::Bypass:case WordMenu::Quantize:
            for(int i=0;i<2;++i)item(i?"on":"off",WordOp::SetParam,
                (f.menu==WordMenu::Loop?C::P_LOOP:f.menu==WordMenu::Bypass?20:21)*256+i,can);break;
        case WordMenu::Slice:
            item("trim window",WordOp::Slice,0,can);std::snprintf(t,sizeof t,"cut %02d",words.id);
            item(t,WordOp::Slice,words.id,can&&words.id>0&&d.cuts.valid(words.id-1,frame.frames));
            item("< previous id",WordOp::CycleSlice,-1);item("next id >",WordOp::CycleSlice,1);break;
        case WordMenu::AutoKind:for(int i:{0,4,8,16}){std::snprintf(t,sizeof t,i?"equal %d":"attack",i);item(t,WordOp::AutoMethod,i);}break;
        case WordMenu::Snap:for(int i=0;i<4;++i)item(i==0?"off":i==1?"zero":i==2?"attack":"beat",WordOp::Snap,i);break;
        case WordMenu::None:break;
        }
        int widest=0;for(int i=0;i<f.count;++i)widest=std::max(widest,ui::text_w(f.cells[i].label));
        f.layout=pw::below(f.ax,f.ay,f.count,widest);
        for(int i=0;i<f.count;++i){auto&c=f.cells[i];const auto&b=f.layout;c.box={b.left,b.y-8+i*b.step,b.right,b.y-8+(i+1)*b.step};}
        f.selected=clamp(f.selected,0,f.count-1);return;
    }
    if(!f.surface){inline_build(f);return;}
    auto dropdown=[&](int col,int row,const char*l,WordMenu menu,bool enabled=true){auto&c=word_cell(f,col,row,l,WordOp::Menu,int(menu),enabled);c.chevron=true;};
    auto nudge_pair=[&](int row){word_cell(f,0,row,"-1 smp",WordOp::Nudge,-1,exact);word_cell(f,1,row,"+1 smp",WordOp::Nudge,1,exact);};
    switch(v.page){
    case Page::Edit:
        if(v.selection_page==Page::Cuts){std::snprintf(f.title,sizeof f.title,"part %02d / boundary %u smp",v.cut+1,cut?d.cuts.at[v.cut]:0);
            word_cell(f,0,0,"< previous part",WordOp::CutFocus,-1,cut);word_cell(f,1,0,"next part >",WordOp::CutFocus,1,cut);
            word_cell(f,0,1,"edit boundary >",WordOp::Primary,0);nudge_pair(2);
        }else {std::snprintf(f.title,sizeof f.title,"loop seam");
            for(int i=0;i<3;++i){const Handle h=i==0?Handle::LoopIn:i==1?Handle::LoopOut:Handle::Crossfade;sample_value(a,sizeof a,handle_value(e,h));word_param(f,i==1?1:0,i==2?1:0,i==0?"in":i==1?"out":"xfade",-1,Gesture::Edge,a,handle_value(e,h),i==0||i==1?e.start:0,i==0||i==1?e.end:(e.loop_end-e.loop_start-1)/2,exact);f.cells[f.count-1].arg=int(h);}
            word_cell(f,1,1,"listen seam",WordOp::Seam,0,exact);nudge_pair(2);
            std::snprintf(t,sizeof t,"nudge %s",handle_name(v.handle));word_cell(f,0,3,t,WordOp::Handle,-1,exact,true);word_cell(f,1,3,"view >",WordOp::Open,int(Page::Zoom));}break;
    case Page::Fades:std::snprintf(f.title,sizeof f.title,"fades");
        for(int i=0;i<2;++i){const Handle h=i?Handle::FadeOut:Handle::FadeIn;sample_value(a,sizeof a,handle_value(e,h));word_param(f,i,0,i?"fade out":"fade in",-1,Gesture::Edge,a,handle_value(e,h),0,e.end-e.start,exact);f.cells[f.count-1].arg=int(h);}
        word_cell(f,0,1,"nudge in",WordOp::Handle,int(Handle::FadeIn),exact,v.handle==Handle::FadeIn);word_cell(f,1,1,"nudge out",WordOp::Handle,int(Handle::FadeOut),exact,v.handle==Handle::FadeOut);nudge_pair(2);
        word_cell(f,0,3,v.fine?"fine on":"fine off",WordOp::Fine,0,exact,v.fine);word_cell(f,1,3,"reset fades",WordOp::ResetFades,0,exact);break;
    case Page::Auto:std::snprintf(f.title,sizeof f.title,"auto cuts / %d proposed",frame.proposed.count(frame.frames));
        dropdown(0,0,v.auto_count==0?"attack":v.auto_count==4?"equal 4":v.auto_count==8?"equal 8":"equal 16",WordMenu::AutoKind);
        std::snprintf(a,sizeof a,"%d%%",v.sensitivity);word_param(f,1,0,"sense",-1,Gesture::Gate,a,v.sensitivity,0,100,can);f.cells[f.count-1].arg=100;
        word_cell(f,0,1,"review replace >",WordOp::Confirm,int(WordConfirm::Replace),can);word_cell(f,1,1,"clear cuts >",WordOp::Confirm,int(WordConfirm::Clear),can);break;
    case Page::Live:std::snprintf(f.title,sizeof f.title,"live cuts");word_cell(f,0,0,"arm source pass",WordOp::LiveArm,0,exact&&!v.live);word_cell(f,1,0,"stop / disarm",WordOp::LiveStop,0,v.live||frame.audition);
        word_cell(f,1,1,"a / mark cursor",WordOp::LiveMark,0,exact&&v.live&&frame.audition);break;
    case Page::Config:std::snprintf(f.title,sizeof f.title,"files");
        word_cell(f,0,0,"save copy >",WordOp::Confirm,int(WordConfirm::Save),can);word_cell(f,1,0,"source options >",WordOp::Open,int(Page::Processing));
        word_cell(f,0,1,"detach raw >",WordOp::Confirm,int(WordConfirm::Detach),can);word_cell(f,1,1,"crop copy >",WordOp::Confirm,int(WordConfirm::Crop),exact&&v.selection_page==Page::Trim&&(frame.start||frame.end<frame.frames));break;
    case Page::Processing:std::snprintf(f.title,sizeof f.title,"copy processing");
        word_cell(f,0,0,v.dc?"remove dc on":"remove dc off",WordOp::Toggle,0,true,v.dc);word_cell(f,1,0,v.normalize?"normalize -1db":"normalize off",WordOp::Toggle,1,true,v.normalize);
        word_cell(f,0,1,v.bake?"bake fades on":"bake fades off",WordOp::Toggle,2,true,v.bake);word_cell(f,1,1,v.reverse?"reverse on":"reverse off",WordOp::Toggle,3,true,v.reverse);word_cell(f,0,2,"review copy >",WordOp::Confirm,int(WordConfirm::Save),can);break;
    case Page::Zoom:std::snprintf(f.title,sizeof f.title,"view");std::snprintf(t,sizeof t,"snap %s",snap_name());dropdown(0,0,t,WordMenu::Snap);word_cell(f,1,0,v.fine?"fine on":"fine off",WordOp::Fine,0,true,v.fine);
        word_cell(f,0,1,"zoom / fit >",WordOp::Primary,0);word_cell(f,0,2,"reset view 1x",WordOp::Whole);break;
    default:break;
    }
}
void words_closed_draw(){
    if(!word_frame.surface){pw::identity();precision_bottom();if(pw::pen.cache)module_rand_draw(pw::pen);return;}
    pw::identity();char t[48];std::snprintf(t,sizeof t,"%.32s >",frame.loaded?frame.name:"no source");pw::header(76,220,t,m::ink());
    right(306,m::kHeaderTextY,"files >",m::ghost());if(pw::pen.cache)module_rand_draw(pw::pen);
    pw::text(14,70,word_frame.title,m::spot());
    for(int i=0;i<word_frame.count;++i){const auto&c=word_frame.cells[i];const int x=c.box.x0,y=c.box.y0;
        const auto ink=!c.enabled?m::ghost():c.selected?m::spot():m::ink();
        if(c.gesture!=Gesture::None){pw::text(x,y+4,c.label,m::ghost());pw::pen.right(c.box.x1,y+4,c.value,ink);
            const auto at=[&](int64_t v){return x+5+int((c.box.x1-x-10)*std::clamp<int64_t>(v-c.lo,0,c.hi-c.lo)/(c.hi-c.lo));};
            module_fader::horizontal(pw::pen,x+5,c.box.x1-5,c.box.y1-9,at(c.raw),at(c.live),{c.linked,!c.enabled,frame.quiet});
        }else {pw::text(x+4,y+8,c.label,ink);if(c.chevron)pw::pen.chevron(x+ui::text_w(c.label)+12,y+11,ink);
            if(c.selected)pw::line(x+4,y+20,x+4+ui::text_w(c.label)-1,y+20,m::spot());}
    }
    pw::text(14,m::kTextY,"back",m::ghost());
    pw::pen.right(m::right_edge(320),m::kTextY,"seq >",m::ghost());
}
// Confirmation bodies borrow the helper's exact reveal alpha through Extra;
// the sink suppresses tree geometry, rather than inventing a second fade law.
struct WordAlphaPen {void line(int,int,int,int,uint32_t)const{} void text(int,int,const char*,uint32_t)const{}};
void words_tree(const WordFrame&f){
    pw::text(f.ax,f.ay,f.anchor,m::spot());
    word_menu::draw(pw::pen,f.layout,f.motion,f.count,f.selected,[&](int i){return f.cells[i].label;},[](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
}
void words_draw(){
    if(!word_frame.modal){words_closed_draw();return;}
    // The cache is frozen after guard, uploaded by existing sigils_gpu_frame.
    if(!sigil_surface::bottom_cache.draw())m::paper(320);
    const auto&f=word_frame;
    if(f.confirm){pw::text(24,62,f.title,m::spot());
        word_menu::draw(WordAlphaPen{},f.layout,f.motion,1,0,[](int){return "";},[&](int,int,unsigned alpha,bool){
            for(int i=0;i<3;++i)pw::text(24,98+28*i,f.lines[i],word_menu::faded(i==1?m::spot():m::ink(),alpha));
            pw::text(14,m::kTextY,"cancel",word_menu::faded(m::ghost(),alpha));
            pw::pen.right(m::right_edge(320),m::kTextY,"confirm",word_menu::faded(f.cells[1].enabled?m::spot():m::ghost(),alpha));
        },m::ink(),m::spot(),m::branch());return;}
    // Actual anchor/stem/list use the same commands in foreground and cache.
    words_tree(f);
    pw::text(14,m::kTextY,"b / outside closes",m::ghost());
}
void words_capture(){
    // Source/palette/record/lens/project invalidates modal before any new cache.
    if(words_modal()&&(words.engine!=g_eng||!context()||words.source.unit!=ctx.unit||words.source.generation!=ctx.generation||
       words.source.pcm!=ctx.pcm||words.ink!=m::ink()||words.spot!=m::spot()||frame.busy||param_lens_is_open()))words_cancel(true);
    WordFrame next;words_build(next);
    if(words_modal()&&words.dirty){
        // Reconstruct the precise invoking stack from frozen DTOs, not the
        // child that just closed. One existing pixel cache/texture serves every
        // level; each receding parent is its actual native command stream.
        const Frame saved=frame;const WordFrame saved_words=word_frame;
        auto&cache=sigil_surface::bottom_cache;
        int base=words.depth-1;while(base>0&&words.parents[base].snapshot.modal)--base;
        frame=words.parents[base].native;word_frame=words.parents[base].snapshot;
        for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;++x){
            uint32_t c=0xff000000u;for(unsigned s:{0u,8u,16u}){unsigned sum=0;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=(m::sample(false,float(x*2+dx),float(y*2+dy))>>s)&255;
                c|=((sum+2)/4)<<s;}cache.pixels[y*sigil_surface::W+x]=c;
        }
        pw::pen=sigil_surface::Painter{&cache};words_closed_draw();pw::pen={};
        for(int i=base+1;i<words.depth;++i){cache.finish();word_frame=words.parents[i].snapshot;
            pw::pen=sigil_surface::Painter{&cache};words_tree(word_frame);pw::pen={};
        }
        cache.finish();frame=saved;word_frame=saved_words;
    }
    const unsigned identity=words_modal()?unsigned(words.confirm?100+int(words.operation):int(words.menu)):0;
    next.motion=word_menu::capture(words.motion,osGetTime(),identity,next.selected,next.layout,[&](int i){return next.cells[clamp(i,0,std::max(0,next.count-1))].label;},words.dirty);
    word_upper=words_modal()&&words.depth?words.parents[words.depth-1].native:frame;
    words.dirty=false;word_frame=next;
}
void words_activate(int operation,int argument){
    const WordOp op=WordOp(operation);bool export_now=false;
    if(!g_eng||!g_audio)return;
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u)||!same(document(*u),expected)||param_lens_is_open()||
            (words_modal()&&(words.engine!=g_eng||words.ink!=m::ink()||words.spot!=m::spot()||g_eng->play_editor_busy()))){words_cancel(true);return;}
        finish();
        if(op==WordOp::Back){words_pop();return;}
        switch(op){
        case WordOp::Open:
            if(argument>=100){words_open(Page::Config);view.group=Group(argument-100);}
            else {if(Page(argument)==Page::Auto){view.auto_count=0;propose();}words_open(Page(argument));}break;
        case WordOp::Menu:{const auto menu=WordMenu(argument);int selected=0;
            if(menu==WordMenu::Mode)selected=workspace(frame);else if(menu==WordMenu::Trigger)selected=play_mode::is_gate(u->params[C::P_MODE].base)?0:1;
            else if(menu==WordMenu::Gate)selected=play_mode::gate_index(u->params[C::P_GATE].base);
            else if(menu==WordMenu::Slice){words.id=seq_slice::decode(u->params[C::P_SLICE].base);if(!words.id){const auto cuts=document(*u).cuts;for(int i=0;i<16;++i)if(cuts.valid(i,ctx.frames)){words.id=i+1;break;}}selected=seq_slice::decode(u->params[C::P_SLICE].base)?1:0;}
            else if(menu==WordMenu::Snap)selected=view.snap;else if(menu==WordMenu::AutoKind)selected=view.auto_count==4?1:view.auto_count==8?2:view.auto_count==16?3:0;
            else if(menu==WordMenu::Loop)selected=u->params[C::P_LOOP].base>16383;
            else if(menu==WordMenu::Bypass)selected=u->bypass;
            else if(menu==WordMenu::Quantize)selected=u->pitch_quantize;
            const WordCell*invoker=nullptr;int row=-1;
            for(int i=0;i<word_frame.count;++i)if(word_frame.cells[i].op==op&&word_frame.cells[i].arg==argument){invoker=&word_frame.cells[i];row=i;break;}
            if(!invoker||!invoker->enabled)break;
            const int x=word_frame.modal?word_frame.layout.x:!word_frame.surface?invoker->box.x1-ui::text_w(invoker->label)-14:invoker->box.x0+4;
            const int y=word_frame.modal?word_frame.layout.y+row*word_frame.layout.step:!word_frame.surface?40:invoker->box.y0+8;
            words_open_menu(menu,invoker->label,x,y,selected);break;}
        case WordOp::Confirm:words_confirm(WordConfirm(argument));break;
        case WordOp::Apply:
            if(!words.confirm||int(words.operation)!=argument||!editable(*u))break;
            if(words.operation<=WordConfirm::Clear){if(words.operation==WordConfirm::Replace&&!overview.ready&&!view.auto_count)break;
                command_begin();apply_cuts(*u,words.operation==WordConfirm::Clear?pe::Cuts{}:proposed);finish();words_pop();}
            else {view.copy=words.operation==WordConfirm::Save?Copy::Save:words.operation==WordConfirm::Detach?Copy::Detach:Copy::Crop;
                view.page=Page::Copy;export_now=true;}break;
        case WordOp::ResetTrim:if(editable(*u)){command_begin();auto e=prepare(*u);e.start=0;e.end=ctx.frames;set_edit(*u,e);finish();words_pop();}break;
        case WordOp::Nudge:nudge(*u,argument);break;
        case WordOp::Handle:if(argument>=0)view.handle=Handle(argument);else view.handle=view.handle==Handle::LoopIn?Handle::LoopOut:view.handle==Handle::LoopOut?Handle::Crossfade:Handle::LoopIn;break;
        case WordOp::Fine:view.fine=!view.fine;break;
        case WordOp::ResetFades:if(editable(*u)){command_begin();auto e=prepare(*u);e.fade_in=e.fade_out=0;set_edit(*u,e);finish();}break;
        case WordOp::CutFocus:choose_cut(*u,argument);break;
        case WordOp::CutReverse:case WordOp::CutMerge:if(editable(*u)){auto c=document(*u).cuts;if(!c.valid(view.cut,ctx.frames))break;command_begin();
            if(op==WordOp::CutReverse)c.reverse^=uint16_t(1u<<view.cut);else {const int id=c.merge(view.cut,ctx.frames);if(id>=0){view.cut=id;view.cut_end=false;}}
            apply_cuts(*u,c);
            if(op==WordOp::CutReverse&&preview_cut==view.cut&&g_eng->play_preview_active(view.token))
                g_eng->update_play_preview(view.token,c.at[view.cut],c.end(view.cut,ctx.frames),(c.reverse&(1u<<view.cut))!=0,c.speed_q8[view.cut],c.pitch_semitones[view.cut]);
            finish();}break;
        case WordOp::Primary:{const bool seam=view.page==Page::Edit&&view.selection_page==Page::Cuts;
            words_root();view.page=view.selection_page;view.sound=false;
            if(seam){view.owner=Owner::Second;view.add=view.move=view.cut_end=false;}
            break;}
        case WordOp::AutoMethod:view.auto_count=argument;propose();words_pop();break;
        case WordOp::LiveArm:if(editable(*u)&&u->modern())view.live=preview_locked(*u,0,ctx.frames,false);break;
        case WordOp::LiveStop:view.live=false;stop_locked();break;
        case WordOp::LiveMark:live_mark(*u);break;
        case WordOp::Listen:if(view.live||audition_active(*u)){view.live=false;stop_locked();}else listen(*u);break;
        case WordOp::Seam:seam(*u);break;
        case WordOp::Toggle:if(argument>=0&&argument<4){bool*p=argument==0?&view.dc:argument==1?&view.normalize:argument==2?&view.bake:&view.reverse;*p=!*p;}break;
        case WordOp::SetParam:if(editable(*u)){const int p=argument/256,val=argument%256;command_begin();
            if(p==20){if(u->bypass!=bool(val)){stop_locked();u->bypass=val;}}
            else if(p==21){if(u->pitch_mode()==C::PitchMode::Legacy)u->pitch_quantize=val;}
            else {if(p==C::P_MODE&&!u->modern())u->set_modern(true);
                q15 next=p==C::P_GATE?play_mode::gate_value(val):val?32767:0;
                if(p==C::P_SPEED){const int raw=u->params[p].base;next=raw&&(raw<0)==bool(val)?q15(raw):q15((val?-1:1)*(raw?std::clamp(std::abs(raw),1,32767):8192));if(next!=raw)stop_locked();}
                u->params[p].base=next;view.selected=p;}
            command_end(*u);if(words_modal())words_pop();}break;
        case WordOp::Mode:if(argument>=0&&argument<3){words_pop();mode(*u,argument);}break;
        case WordOp::Slice:if(editable(*u)&&(argument==0||document(*u).cuts.valid(argument-1,ctx.frames))){command_begin();if(seq_slice::decode(u->params[C::P_SLICE].base)!=argument)u->params[C::P_SLICE].base=seq_slice::encode(argument);view.selected=C::P_SLICE;command_end(*u);words_pop();}break;
        case WordOp::CycleSlice:{int id=words.id;const auto c=document(*u).cuts;for(int i=id+argument;i>=1&&i<=16;i+=argument)if(c.valid(i-1,ctx.frames)){words.id=i;break;}break;}
        case WordOp::Snap:view.snap=argument;view.snapped=false;words_pop();break;
        case WordOp::Whole:view.first=0;view.span=ctx.frames;view.scale=1;break;
        default:break;
        }
    }
    if(export_now){export_copy();words_pop();}
}
bool words_touch(C&u,int x,int y){
    if(words.quarantine)return true;
    if(!words_modal()&&!words_surface())return false;
    if(words.press.owned||stroke.kind!=Gesture::None)return true;
    platform::AudioOut::Guard guard(*g_audio);if(!identify(u))return true;
    if(words_modal()){
        words.press.owned=true;words.press.x=x;words.press.y=y;words.press.index=-1;
        // Real main routes continued touch by Drag, not by modal visibility.
        // Capture it even for a disabled row so leaving the box cancels safely.
        g_drag=Drag::PlaySliceSeam;
        for(int i=0;i<word_frame.count;++i)if(word_frame.cells[i].box.contains(x,y)){words.selected=i;words.press.index=i;words.press.box=word_frame.cells[i].box;words.press.armed=word_frame.cells[i].enabled;return true;}
        words_pop();words.quarantine=true;return true;
    }
    auto hit=[&](layout::Box box,Action op,int arg=0){if(!box.contains(x,y))return false;queue(op,arg,box,x,y);return true;};
    if(hit(pl::source,Action::Source)||hit(pl::files,Action::Sheet,int(Page::Config)))return true;
    if(pl::footer(2).contains(x,y)){queue(Action::Sequence,0,pl::footer(2),x,y);return true;}
    if(pl::footer(0).contains(x,y)){queue(Action::Word,int(WordOp::Back),pl::footer(0),x,y);return true;}
    for(int i=0;i<word_frame.count;++i){const auto&c=word_frame.cells[i];if(!c.box.contains(x,y))continue;if(!c.enabled)return true;
        if(c.gesture!=Gesture::None){view.selected=c.param>=0?c.param:c.gesture==Gesture::CutSpeed?C::P_SPEED:C::P_START;
            if(c.gesture==Gesture::Edge)view.handle=Handle(c.arg);
            if(editable(u)){begin(c.gesture,x,y);stroke.argument=c.param>=0?c.param:c.arg;stroke.box=c.box;}return true;}
        queue(Action::Word,int(c.op)+c.arg*256,c.box,x,y);return true;
    }return true;
}
bool words_release(bool commit){
    if(!words.press.owned)return false;
    const auto p=words.press;words.press={};
    if(commit&&p.armed&&p.index>=0&&p.index<word_frame.count){const auto c=word_frame.cells[p.index];words_activate(int(c.op),c.arg);}
    if(commit)words.quarantine=false;
    return true;
}
