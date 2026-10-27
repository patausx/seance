#pragma once
#include "../../core/engine/units.h"
#include "../../core/engine/musical_note.h"
#include "ring_reference.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace seance::platform::ui3ds::ring_readout {
inline void frequency(char* out,size_t size,uint32_t mhz){
    if(mhz>=1000000){const unsigned n=(mhz+5000)/10000;std::snprintf(out,size,"%u.%02ukhz",n/100,n%100);}
    else {const unsigned n=(mhz+5)/10;std::snprintf(out,size,"%u.%02uhz",n/100,n%100);}
}
// Explicit revision, not the currently selected card: safe in score/lock readouts.
inline void value_text(char* out,size_t size,int p,q15 value,bool modern){
    using C=RingUnit;
    if(p<0||p>=(modern?C::P_COUNT:C::LEGACY_COUNT)){std::snprintf(out,size,"--");return;}
    if(!modern&&p==C::P_FREQ){const double hz=ring_reference::hz(value);char number[16];
        int end=std::snprintf(number,sizeof number,"%.2f",hz>=1000?hz/1000:hz)-1;
        while(end>0&&number[end]=='0')number[end--]='\0';
        if(end>0&&number[end]=='.')number[end]='\0';
        std::snprintf(out,size,"%s%s",number,hz>=1000?"khz":"hz");return;}
    if(p==C::P_FREQ)frequency(out,size,C::frequency_millihz(value));
    else if(p==C::P_NOTE)musical_note::name(out,size,musical_note::decode(value));
    else if(p==C::P_MODE)std::snprintf(out,size,"%s",value>=16384?"note":"free");
    else if(p==C::P_WAVE){const char* names[]={"sine","tri","pulse"};std::snprintf(out,size,"%s",names[C::wave_index(value)]);}
    else if(p==C::P_FOLLOW_TIME){const uint32_t tenth=(C::follow_time_us(value)+50)/100;
        std::snprintf(out,size,"%u.%ums",unsigned(tenth/10),unsigned(tenth%10));}
    else if(p==C::P_DRIVE){const int n=C::drive_centidb(value);std::snprintf(out,size,"+%d.%02ddb",n/100,n%100);}
    else if(p==C::P_FINE){const int n=C::fine_centicents(value),a=std::abs(n);std::snprintf(out,size,"%c%d.%02uct",n<0?'-':'+',a/100,unsigned(a%100));}
    else if(p==C::P_FEEDBACK||(modern&&p==C::P_FOLLOW)){
        const int v=std::max(-32767,int(value)),n=(std::abs(v)*100+16383)/32767*(v<0?-1:1);
        if(n)std::snprintf(out,size,"%+d%%",n);else std::snprintf(out,size,"0%%");}
    else std::snprintf(out,size,"%u%%",unsigned((uint32_t(std::max(0,int(value)))*100+16383)/32767));
}
}
