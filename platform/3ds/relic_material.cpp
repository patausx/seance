#include "relic_material.h"
#include "relic_material_data.h"
#include "ui_palette.h"
#include <citro2d.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace seance::platform::ui3ds::relic_material {
namespace {
struct V {float x=0,y=0,z=0;};
V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V operator*(V a,float b){return {a.x*b,a.y*b,a.z*b};}
float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V a){const float n=dot(a,a);return n<1e-12f?V{1,0,0}:a*(1.f/std::sqrt(n));}
V project(V a){return {(160+a.x)*.5f,(122+.94f*a.y-.36f*a.z)*.5f,a.z+.12f*a.y};}
V vector(relic::Vertex a){return {a.x/128.f,a.y/128.f,a.z/128.f};}
float edge(V a,V b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
struct Cell {float depth=-1e20f;uint8_t tone=255,hit=0,floor=0,horizontal=0;};
struct Span {uint32_t color=0;uint8_t x=0,y=0,w=0,h=0;};
struct Cache {
    V points[data::vertex_count],normals[data::vertex_count];
    Cell cells[width*height];
    uint32_t pixels[width*height]={},tones[8]={};
    Span spans[max_spans];uint8_t covered[width*height]={};
    relic::Vertex control[relic::SURFACE_VERTICES]={};
    uint32_t instance=0,spot=0,ink=0,shadow=0;
    uint64_t palette_revision=0,serial=0,uploaded_serial=0;
    int contact_x=0,contact_y=0;
    bool captured=false,contact=false,attempted=false,ready=false;
    Stats stats;
} cache;
static_assert(sizeof(Cache)<=224*1024,"bounded renderer CPU mesh/grid/cache scratch");
#ifdef __3DS__
C3D_Tex texture{};
// One 320x240 screen-sized nearest quad, only its 160x120 texels are sampled.
const Tex3DS_SubTexture subtex{320,240,0.f,1.f,160.f/256,1.f-120.f/128};
#endif
uint32_t mix(uint32_t a,uint32_t b,float t){uint32_t out=0xff000000u;
    for(unsigned s:{0u,8u,16u})out|=uint32_t(std::lround(((a>>s)&255)*(1-t)+((b>>s)&255)*t))<<s;
    return out;}
void colors(){
    constexpr int tones[8]={57,72,89,107,126,145,163,181};
    const bool legacy=theme_config::legacy(palette::current());
    const float tint=palette::visual_graphite()?.10f:.14f;
    cache.spot=palette::spot();cache.ink=palette::ink();
    // Exact reference neutral/spot recipe for the shipped pair. Other themes
    // use their own paper hue/endpoints and accent, never graphite/lilac constants.
    const uint32_t dark=mix(palette::bg(),palette::gray(0),.75f);
    const uint32_t pale=mix(palette::bg(),palette::gray(255),.5f);
    for(int i=0;i<8;++i){const uint32_t neutral=legacy?palette::rgb(tones[i],tones[i],tones[i]+3):mix(dark,pale,(tones[i]-57)/124.f);
        cache.tones[i]=mix(neutral,cache.spot,tint);}
    cache.shadow=legacy?palette::rgb(14,12,18):dark;
    cache.palette_revision=palette::revision();
}
// Fixed immutable material detail plus ONLY barycentric actual control motion.
// The first 225 samples are the exact authoritative Q7 knots, not resampled rest.
V binding(const relic::Surface& s,unsigned i){const auto b=data::bindings[i];
    const unsigned w[3]={b.wa,b.wb,4u-b.wa-b.wb},ids[3]={b.a,b.b,b.c};
    V out{};for(int k=0;k<3;++k)out=out+vector(s.vertices[ids[k]])*(w[k]*.25f);
    out.z+=b.dz/128.f;return out;}
// Raster bounds are fixed to the existing field, including shadow. Geometry is
// clipped, not allowed to intrude into the header/parameter/footer controls.
template<class Paint> void triangle(V a,V b,V c,Paint paint){
    const float area=edge(a,b,c.x,c.y);if(std::abs(area)<1e-10f)return;
    const int x0=std::max(grid_x,int(std::ceil(std::min({a.x,b.x,c.x})-.5f)));
    const int x1=std::min(grid_x+width-1,int(std::floor(std::max({a.x,b.x,c.x})-.5f)));
    const int y0=std::max(grid_y,int(std::ceil(std::min({a.y,b.y,c.y})-.5f)));
    const int y1=std::min(grid_y+height-1,int(std::floor(std::max({a.y,b.y,c.y})-.5f)));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const float u=edge(b,c,x+.5f,y+.5f)/area,v=edge(c,a,x+.5f,y+.5f)/area,w=1-u-v;
        if(u>=-1e-8f&&v>=-1e-8f&&w>=-1e-8f)paint(cache.cells[(y-grid_y)*width+x-grid_x],u,v,w);}
}
bool mesh(const relic::Surface& s){
    for(unsigned i=0;i<data::vertex_count;++i){const V p=binding(s,i);
        if(p.z<0||p.z>120||!std::isfinite(dot(p,p)))return false;
        cache.points[i]=p;cache.normals[i]={};}
    for(const auto f:data::faces){const V n=cross(cache.points[f.b]-cache.points[f.a],cache.points[f.c]-cache.points[f.a]);
        for(unsigned i:{unsigned(f.a),unsigned(f.b),unsigned(f.c)})cache.normals[i]=cache.normals[i]+n;}
    for(auto& n:cache.normals)n=unit(n);
    for(auto& c:cache.cells)c=Cell{};
    // Exact current CONTROL silhouette/pick-domain mask. Rest detail cannot
    // invent a moving outline outside the material that Model::begin can pick.
    for(const auto f:relic::surface_data::faces){V a=vector(s.vertices[f.a]),b=vector(s.vertices[f.b]),c=vector(s.vertices[f.c]);
        triangle(project(a),project(b),project(c),[](Cell& p,float,float,float){p.hit=1;});
        a.z=b.z=c.z=0;triangle(project(a),project(b),project(c),[](Cell& p,float,float,float){p.floor=1;});}
    const V light=unit({-.42f,-.58f,.88f}),fill=unit({.4f,.6f,.65f});
    for(const auto f:data::faces){const V a=project(cache.points[f.a]),b=project(cache.points[f.b]),c=project(cache.points[f.c]);
        triangle(a,b,c,[&](Cell& p,float u,float v,float w){const float depth=a.z*u+b.z*v+c.z*w;
            if(!p.hit||depth<=p.depth)return;
            const V n=unit(cache.normals[f.a]*u+cache.normals[f.b]*v+cache.normals[f.c]*w);
            // Original reference recess is zero; no invented pressure shading.
            const float level=std::clamp(.24f+.62f*std::max(0.f,dot(n,light))+.12f*std::max(0.f,dot(n,fill))+.02f*std::max(0.f,n.z),0.f,1.f);
            p.tone=uint8_t(std::clamp(int(level*7.999f),0,7));p.depth=depth;});}
    // Same projected-floor 7x7 box shadow, integer counts make it exact/bounded.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){unsigned count=0;
        for(int j=-3;j<=3;++j)if(x+j>=0&&x+j<width)count+=cache.cells[y*width+x+j].floor;
        cache.cells[y*width+x].horizontal=uint8_t(count);}
    ++cache.stats.meshes;return true;
}
void bitmap(){
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto& c=cache.cells[y*width+x];uint32_t out=0;
        if(c.tone<8)out=cache.tones[c.tone];
        else {unsigned count=0;for(int j=-3;j<=3;++j)if(y+j>=0&&y+j<height)count+=cache.cells[(y+j)*width+x].horizontal;
            const unsigned alpha=unsigned(std::lround(255*.32f*count/49.f));if(alpha)out=(cache.shadow&0xffffffu)|(alpha<<24);}
        // 2px reference cells are confined to the actual production field.
        if(x+grid_x<13||x+grid_x>144||y+grid_y<20)out=0;
        cache.pixels[y*width+x]=out;
    }
    ++cache.serial;++cache.stats.recolors;cache.stats.bitmap=true;
    // The admitted GPU path needs no software rectangle search per wave frame.
    // Build the exact portable plan only after allocation failure; normal draw
    // still waits for the successful upload fence and never shows stale pixels.
    cache.stats.spans=0;cache.stats.portable=false;
    if(cache.ready)return;
    // SAME pixels in a bounded software rectangle plan, not another shader.
    // Merge equal horizontal runs through adjacent rows. If a hostile material
    // exceeds the plan, say unavailable before drawing ANY spans (no partial body).
    unsigned count=0;std::memset(cache.covered,0,sizeof cache.covered);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const unsigned at=y*width+x;const uint32_t color=cache.pixels[at];
        if(!color||cache.covered[at])continue;
        unsigned end=x+1;while(end<width&&!cache.covered[y*width+end]&&cache.pixels[y*width+end]==color)++end;
        unsigned bottom=y+1;for(;bottom<height;++bottom){bool same=true;
            for(unsigned k=x;k<end;++k)if(cache.covered[bottom*width+k]||cache.pixels[bottom*width+k]!=color){same=false;break;}
            if(!same)break;}
        for(unsigned j=y;j<bottom;++j)for(unsigned k=x;k<end;++k)cache.covered[j*width+k]=1;
        if(count<max_spans)cache.spans[count]={color,uint8_t(x),uint8_t(y),uint8_t(end-x),uint8_t(bottom-y)};
        ++count;
    }
    cache.stats.spans=count;cache.stats.portable=count<=max_spans;
}
void contact(const relic::Surface& s){cache.contact=s.held&&s.face<relic::SURFACE_FACES;
    if(!cache.contact)return;
    const auto f=relic::surface_data::faces[s.face];V p{};unsigned total=0;const unsigned ids[3]={f.a,f.b,f.c};
    for(int k=0;k<3;++k){p=p+vector(s.vertices[ids[k]])*(s.bary[k]/65535.f);total+=s.bary[k];}
    if(total!=65535){cache.contact=false;return;}p=project(p);
    cache.contact_x=int(std::lround(p.x*2));cache.contact_y=int(std::lround(p.y*2));}
void mark(){if(!cache.contact)return;
    constexpr int d[8][2]={{-3,0},{-2,-1},{-1,-2},{0,-3},{1,-2},{2,-1},{3,0},{0,3}};
    const auto dot_at=[](int x,int y,uint32_t c){if(x>=25&&x<292&&y>=39&&y<194)C2D_DrawRectSolid(float(x),float(y),0,1,1,c);};
    for(const auto& p:d)dot_at(cache.contact_x+p[0],cache.contact_y+p[1],cache.spot);
    dot_at(cache.contact_x,cache.contact_y,cache.ink);}
#ifdef __3DS__
unsigned offset(unsigned x,unsigned y){unsigned z=0;for(unsigned b=0;b<3;++b){z|=((x>>b)&1)<<(2*b);z|=((y>>b)&1)<<(2*b+1);}
    return 4*(((y/8)*32+x/8)*64+z);}
#endif
} // namespace
bool init(){if(cache.attempted)return cache.ready;cache.attempted=true;
#ifdef __3DS__
    cache.ready=C3D_TexInit(&texture,256,128,GPU_RGBA8);
    if(cache.ready){
        // Transparent outside the fixed action field for the texture's lifetime.
        // No full 128KiB clear/repaint on each changing material frame.
        std::memset(texture.data,0,256*128*4);
        C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    }
#endif
    cache.stats.texture=cache.ready;return cache.ready;
}
void shutdown(){
#ifdef __3DS__
    if(cache.ready)C3D_TexDelete(&texture);
    texture={};
#endif
    cache.captured=cache.contact=cache.attempted=cache.ready=false;cache.stats={};cache.serial=cache.uploaded_serial=0;
}
void capture(const relic::Surface& s,uint32_t instance){
    const bool changed=!cache.captured||!cache.stats.bitmap||cache.instance!=instance||std::memcmp(cache.control,s.vertices,sizeof cache.control)!=0;
    const bool recolor=!cache.captured||cache.palette_revision!=palette::revision();
    if(changed){cache.stats.geometry_rejected=!mesh(s);if(cache.stats.geometry_rejected){
            // Never present another instance's old body, or fabricate repair motion.
            cache.stats.bitmap=cache.stats.portable=false;cache.contact=false;return;}
        std::memcpy(cache.control,s.vertices,sizeof cache.control);cache.instance=instance;}
    if(recolor)colors();
    if(changed||recolor)bitmap();
    cache.captured=true;contact(s);
}
void upload(){
#ifdef __3DS__
    if(!cache.ready||!cache.stats.bitmap||cache.uploaded_serial==cache.serial)return;
    // Main's successful SYNCDRAW fence completed the last texture reader. CPU
    // capture never touches this storage; failed FrameBegin keeps it byte-exact.
    auto* out=static_cast<uint8_t*>(texture.data);
    for(unsigned y=unsigned(grid_y);y<unsigned(grid_y+height);++y)
        for(unsigned x=unsigned(grid_x);x<unsigned(grid_x+width);++x){
            const uint32_t color=cache.pixels[(y-grid_y)*width+x-grid_x];
            auto* p=out+offset(x,y);p[0]=uint8_t(color>>24);p[1]=uint8_t(color>>16);p[2]=uint8_t(color>>8);p[3]=uint8_t(color);
        }
    C3D_TexFlush(&texture);C3D_TexBind(0,&texture);cache.uploaded_serial=cache.serial;++cache.stats.uploads;
#endif
}
bool draw(){if(!cache.stats.bitmap)return false;
#ifdef __3DS__
    if(cache.ready&&cache.uploaded_serial==cache.serial){const C2D_Image image{&texture,&subtex};C2D_DrawImageAt(image,0,0,0);mark();return true;}
#endif
    if(!cache.stats.portable)return false;
    for(unsigned i=0;i<cache.stats.spans;++i){const auto& s=cache.spans[i];C2D_DrawRectSolid(float(2*(grid_x+s.x)),float(2*(grid_y+s.y)),0,float(s.w*2),float(s.h*2),s.color);}
    mark();return true;
}
Stats stats(){return cache.stats;}
uint32_t pixel(unsigned x,unsigned y){return x<unsigned(width)&&y<unsigned(height)&&cache.stats.bitmap?cache.pixels[y*width+x]:0;}
relic::Vertex bound_vertex(const relic::Surface& s,unsigned sample){if(sample>=data::vertex_count)return {};
    const V v=binding(s,sample);return {int16_t(std::lround(v.x*128)),int16_t(std::lround(v.y*128)),int16_t(std::lround(v.z*128))};}
} // namespace seance::platform::ui3ds::relic_material

namespace seance::platform::ui3ds {
bool relic_material_init(){return relic_material::init();}
void relic_material_upload(){relic_material::upload();}
void relic_material_shutdown(){relic_material::shutdown();}
} // namespace seance::platform::ui3ds
