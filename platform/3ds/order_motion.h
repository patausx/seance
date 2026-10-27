#pragma once
// Order-only presentation. Call from input/tick with one sampled UI clock;
// drawing reads the resulting coordinates without advancing this state.
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::order_motion {
struct Point {
    float x=0,y=0,from_x=0,from_y=0,to_x=0,to_y=0;
    uint64_t since=0;
    unsigned duration=0;
    bool ready=false,active=false;

    void snap(float px,float py) {
        x=from_x=to_x=px;y=from_y=to_y=py;ready=true;active=false;
    }
    void sample(uint64_t now) {
        if(!active)return;
        if(now<since||now-since>=duration){snap(to_x,to_y);return;}
        const float t=float(now-since)/float(duration),u=1.f-t;
        const float eased=1.f-u*u*u;
        x=from_x+(to_x-from_x)*eased;y=from_y+(to_y-from_y)*eased;
    }
    void aim(float px,float py,uint64_t now,unsigned ms) {
        sample(now);
        if(!ready||!ms){snap(px,py);return;}
        if(to_x==px&&to_y==py)return;
        from_x=x;from_y=y;to_x=px;to_y=py;since=now;duration=ms;active=true;
    }
};
static_assert(sizeof(Point)<=48,"order motion must remain small fixed UI state");
} // namespace seance::platform::ui3ds::order_motion
