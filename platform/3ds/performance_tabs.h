#pragma once
#include "menu_style.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::performance_tabs {
enum Page { Order, Perform, Banks };
// Dedicated 24px paper band. Column rules begin below it, never behind labels.
inline int target(int x,int y) {
    if(y<0 || y>=23 || x<0 || x>=320)return -1;
    return x<96?Order:x<216?Perform:Banks;
}
inline void draw(Page page) {
    const char* labels[]={"order","perform","banks"};
    const int xs[]={10,130,274};
    for(int i=0;i<3;++i) {
        ui::text(xs[i],8,labels[i],i==page?menu_style::spot():menu_style::ghost());
        if(i==page)ui::rect(xs[i],20,ui::text_w(labels[i])-1,1,menu_style::spot());
    }
}
}
