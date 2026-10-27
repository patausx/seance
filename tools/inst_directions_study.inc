// Private visual study. Included ONLY by render_inst_directions_study.py's copy
// of ui_module_focus_inst.cpp; never included by production or its build.
// Drawings are illustrative identities, not acoustic/physical measurements.
namespace inst_directions {
void stroke(int x,int y,int xx,int yy,u32 ink) { ui::line(x,y,xx,yy,ink); }
void path(std::initializer_list<Point> points,u32 ink) {
    bool first=true;Point last{};
    for(const auto p:points) {
        if(!first)stroke(int(last.x),int(last.y),int(p.x),int(p.y),ink);
        last=p;first=false;
    }
}
void oval(int x,int y,int rx,int ry,u32 ink) {
    int ox=x+rx,oy=y;
    for(int i=1;i<=40;++i) {
        const float a=TAU*i/40;
        const int xx=int(std::lround(x+rx*std::cos(a))),yy=int(std::lround(y+ry*std::sin(a)));
        if(xx!=ox || yy!=oy)stroke(ox,oy,xx,yy,ink);
        ox=xx;oy=yy;
    }
}
u32 signal() {return frame.audible?m::spot():m::ghost();}
void voices() {
    // Length means this accepted voice's envelope, NOT peak level or sample time.
    for(int i=0;i<I::VOICE_COUNT;++i) {
        const auto& v=frame.visual.voices[i];const int y=75+i*35;
        const bool active=v.stage!=uint8_t(I::Stage::Off);
        if(active) {
            char text[12];
            if(v.instrument==5)std::snprintf(text,sizeof text,"%s",I::drum_name(I::drum_index(v.note)));
            else inst_readout::note_text(text,sizeof text,v.note);
            ui::text(145,y,text,i==frame.voice?signal():m::ink());
            right(375,y,I::instrument_name(v.instrument),m::ghost());
        } else ui::text(145,y,"--",m::ghost());
        stroke(145,y+18,375,y+18,m::branch());
        if(active && v.envelope) {
            const int end=145+int(v.envelope)*230/32767;
            const u32 color=i==frame.voice?signal():m::ink();
            stroke(145,y+18,end,y+18,color);
            ui::rect(end-1,y+17,2,3,color);
        }
    }
}
bool accepted(int note,bool drums) {
    for(const auto& v:frame.visual.voices)
        if(v.stage && (drums ? v.instrument==5 && I::drum_index(v.note)==note : v.instrument!=5 && v.note==note))return true;
    return false;
}
void keys() {
    const u32 active=signal();
    if(frame.hero==5) {
        for(int i=0;i<6;++i) {
            const int x=145+(i%3)*80,y=83+(i/3)*64;
            const bool on=accepted(i,true);
            if(on)ui::rect(x,y,71,47,active);
            else ui::frame(x,y,71,47,m::branch());
            const char* name=I::drum_name(i);
            ui::text(x+(71-ui::text_w(name))/2,y+20,name,on?palette::inverse():m::ghost());
        }
        return;
    }
    const int root=int(frame.note)/12*12;
    static constexpr int white[]={0,2,4,5,7,9,11};
    static constexpr int black[]={1,3,6,8,10};
    static constexpr int after[]={0,1,3,4,5};
    char text[12];inst_readout::note_text(text,sizeof text,root);
    ui::text(150,75,text,m::ghost());
    inst_readout::note_text(text,sizeof text,root+11);right(360,75,text,m::ghost());
    for(int i=0;i<7;++i) {
        const int x=150+i*30;const bool on=accepted(root+white[i],false);
        if(on)ui::rect(x+1,98,28,79,active);
        ui::frame(x,97,30,81,m::ghost());
    }
    for(int i=0;i<5;++i) {
        const int x=150+(after[i]+1)*30-8;
        ui::rect(x,97,16,48,accepted(root+black[i],false)?active:m::ink());
        // A paper-coloured side keeps an active white key from swallowing a black.
        stroke(x-1,98,x-1,144,palette::inverse());
        stroke(x+16,98,x+16,144,palette::inverse());
    }
    // This is a one-octave window, not a claim that other-octave tails are absent.
    int outside=0;
    for(const auto& v:frame.visual.voices)if(v.stage && (v.instrument==5 || v.note<root || v.note>root+11))++outside;
    if(outside) {
        std::snprintf(text,sizeof text,"+%d outside",outside);
        right(360,194,text,m::ghost());
    }
}
void piano(u32 ink,u32 faint,u32 accent) {
    // Grand piano: restrained top outline, short keyboard, three legs.
    path({{177,149},{177,115},{186,100},{207,89},{237,80},{271,77},{298,82},{315,92},{319,104},{311,117},{289,123},{259,126},{241,135},{237,149},{177,149}},ink);
    path({{184,113},{211,99},{241,89},{270,85},{295,89},{309,98}},faint);
    path({{177,149},{237,149},{249,159},{186,159},{177,149}},ink);
    for(int x=183;x<234;x+=6)stroke(x,150,x+7,157,faint);
    for(int i:{0,1,3,4,5,7})stroke(184+i*6,150,187+i*6,153,accent);
    path({{185,160},{187,186},{190,186},{190,160}},faint);
    path({{239,160},{238,183},{241,183},{244,159}},faint);
    path({{306,119},{304,156},{307,156},{309,118}},faint);
    stroke(230,138,267,100,faint);
}
void strings(bool bass,u32 ink,u32 faint,u32 accent) {
    const int dx=bass?-5:0;
    const auto p=[&](std::initializer_list<Point> points,u32 c) {
        bool first=true;Point old{};
        for(auto q:points) {q.x+=dx;if(!first)stroke(int(old.x),int(old.y),int(q.x),int(q.y),c);old=q;first=false;}
    };
    // Symmetric carved body with a narrow waist; no wobbling or fake simulation.
    p({{248,110},{236,104},{222,108},{215,120},{218,131},{231,138},{229,147},{215,156},{212,169},{218,181},{233,188},{251,190},{270,188},{285,181},{291,169},{287,155},{273,147},{272,138},{284,130},{287,120},{281,108},{266,104},{256,110}},ink);
    p({{247,111},{247,79},{244,74},{245,67},{249,64},{255,65},{258,70},{256,76},{255,111}},ink);
    p({{244,70},{258,70}},faint);
    p({{241,80},{260,80}},faint);p({{241,85},{260,85}},faint);
    for(int x=247;x<=256;x+=3)p({{float(x),78},{float(x),167}},x==253?accent:faint);
    p({{242,159},{261,159}},ink);
    p({{244,168},{251,184},{259,168}},ink);
    p({{233,142},{229,145},{232,151},{231,156},{227,158}},faint);
    p({{270,141},{266,145},{268,151},{267,156},{264,158}},faint);
    if(bass)p({{251,191},{251,208}},ink);
    else {p({{302,89},{289,196}},faint);p({{306,91},{293,197}},ink);}
}
void brass(u32 ink,u32 faint,u32 accent) {
    path({{177,129},{230,129},{266,125},{289,117},{308,103},{326,94},{330,94},{330,165},{326,165},{308,156},{289,144},{266,138},{177,138}},ink);
    oval(330,130,12,36,ink);
    oval(332,130,5,26,faint);
    path({{190,138},{186,149},{191,158},{201,164},{245,164},{255,159},{259,151},{259,139}},ink);
    path({{196,139},{195,149},{202,156},{243,156},{250,152},{252,140}},faint);
    for(int x:{215,230,245}) {
        stroke(x,117,x,150,faint);stroke(x-5,114,x+5,114,accent);
    }
    stroke(172,125,172,142,ink);stroke(173,130,182,130,ink);
}
void flute(u32 ink,u32 faint,u32 accent) {
    path({{170,160},{341,96},{345,106},{174,171},{170,160}},ink);
    for(int i=0;i<7;++i) {
        const int x=193+i*18,y=157-i*7;
        oval(x,y,3,3,i==3?accent:faint);
    }
    stroke(316,106,322,119,faint);stroke(181,156,185,167,faint);
    oval(331,106,3,2,ink);
}
void drums(u32 ink,u32 faint,u32 accent) {
    oval(253,112,64,20,ink);oval(253,112,56,14,faint);
    path({{189,113},{189,161},{195,171},{217,180},{252,184},{287,180},{310,171},{317,161},{317,113}},ink);
    path({{189,151},{194,161},{217,170},{252,175},{287,170},{311,161},{317,151}},faint);
    for(int x:{199,220,253,286,307}) {
        const int y=(x==253?134:(x==220||x==286)?132:126);
        stroke(x,y,x,y+29,ink);stroke(x-2,y+9,x+2,y+9,faint);
    }
    path({{207,81},{284,119}},accent);path({{208,78},{286,117}},faint);
    path({{294,80},{231,117}},ink);path({{295,83},{233,119}},faint);
}
void engraving() {
    const u32 ink=m::ink(),faint=m::ghost(),accent=signal();
    switch(frame.hero) {
        case 0:piano(ink,faint,accent);break;
        case 1:strings(false,ink,faint,accent);break;
        case 2:brass(ink,faint,accent);break;
        case 3:flute(ink,faint,accent);break;
        case 4:strings(true,ink,faint,accent);break;
        case 5:drums(ink,faint,accent);break;
    }
    // Small, honest envelope readout: artwork is static; this alone tracks voice.
    if(frame.voice>=0) {
        const int end=163+frame.visual.voices[frame.voice].envelope*198/32767;
        stroke(163,215,361,215,m::branch());
        stroke(163,215,end,215,accent);
    }
}
void draw(int variant) {
    if(variant==1)voices();
    else if(variant==2)keys();
    else if(variant==3)engraving();
}
} // namespace inst_directions
