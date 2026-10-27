#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::chain_columns {
constexpr int Slots = 6, Add = Slots, Items = Slots + 1;
constexpr int Wide = 160, Narrow = 32, Width = 320;
struct Box { int x=0, w=0; };
struct Layout { Box box[Items]{}; };
// Center the WHOLE occupied strip (including the one add item), never the
// selected tile. Selection changes widths but not the group's outer edges.
inline int group_left(int count) {
    count=std::clamp(count,0,Slots);
    const int items=count+(count<Slots);
    return (Width-Wide-(items-1)*Narrow)/2;
}
// Order contains installed physical slot ids, not UnitKind (duplicates are legal).
inline Layout layout(const int* order, int count, int selected) {
    Layout out;
    int x=group_left(count);
    for(int i=0;i<count;++i){const int slot=order[i],w=slot==selected?Wide:Narrow;
        out.box[slot]={x,w};x+=w;}
    if(count<Slots)out.box[Add]={x,selected==Add?Wide:Narrow};
    return out;
}
inline int hit(const Layout& l,int x){
    if(x<0||x>=Width)return -1;
    for(int i=0;i<Items;++i)if(l.box[i].w&&x>=l.box[i].x&&x<l.box[i].x+l.box[i].w)return i;
    return -1;
}
struct Motion {
    Layout from{},target{},shown{};
    uint64_t began=0;
    bool ready=false;
    void sample(uint64_t now){
        const unsigned age=now<began?200:unsigned(std::min<uint64_t>(200,now-began));
        const float r=1-float(age)/200, t=1-r*r*r;
        for(int i=0;i<Items;++i){
            const auto a=from.box[i],b=target.box[i];
            const int left=int(std::lround(a.x+(b.x-a.x)*t));
            const int right=int(std::lround(a.x+a.w+(b.x+b.w-a.x-a.w)*t));
            shown.box[i]={left,std::max(0,right-left)};
        }
    }
    void aim(const Layout& next,uint64_t now,bool snap=false){
        if(!ready||snap){from=target=shown=next;began=now;ready=true;return;}
        sample(now);
        bool changed=false;
        for(int i=0;i<Items;++i)changed|=next.box[i].x!=target.box[i].x||next.box[i].w!=target.box[i].w;
        if(changed){from=shown;target=next;began=now;}
    }
};
inline void preview_order(const int* order,int count,int source,int gap,int* out){
    int at=0;
    for(int i=0;i<count;++i)if(order[i]!=source)out[at++]=order[i];
    gap=std::clamp(gap,0,count-1);
    for(int i=count-1;i>gap;--i)out[i]=out[i-1];
    out[gap]=source;
}
} // namespace seance::platform::ui3ds::chain_columns
