// Audio only, run from workspace root. No UI, video, network or saved patch edits.
// g++ -O2 -std=c++17 -Icore tools/render_breakbeat_audio.cpp core/engine/*.cpp core/synth/{sampler,wav_loader,wav_writer}.cpp -o /tmp/seance_breakbeat
// /tmp/seance_breakbeat promo/breakbeat-01/engine-raw.wav
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/delay_time.h"
#include "engine/vca_envelope.h"
#include "synth/wav_loader.h"
#include "synth/wav_writer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace seance;
namespace {
constexpr int SR=32000, STEP=2400, BAR=38400, BARS=32, TOTAL=40*SR;
void need(bool ok){if(!ok)throw std::runtime_error("audio render precondition failed");}
void param(Unit& u,int p,int v){u.params[p].base=q15(v);u.params[p].snap();u.control_tick();}
q15 note(int n){return q15(((n-24)*32767+36)/72);}
struct Hit {int frame,chain,source,gain,speed,pan;};
}
int main(int argc,char** argv){
    try {
        need(argc==2);const std::filesystem::path output=argv[1];
        need(!std::filesystem::exists(output)&&!std::filesystem::exists(output.string()+".tmp"));
        if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
        const char* names[]={"kick_punch.wav","kick_deep.wav","snare_x0x.wav","snare_synth.wav","hat_closed.wav","hat_bright.wav","rimshot.wav","conga.wav","clap_x0x.wav"};
        auto& bank=trackr::synth::SampleBank::instance();
        for(int i=0;i<9;++i){
            trackr::synth::Sample s;
            const std::string path=std::string("sample_library/starter_cc0/wav/")+names[i];
            need(trackr::synth::load_wav_to_sample(path.c_str(),s,SR,SR*10)==trackr::synth::WavLoadResult::Ok);
            need(bank.replace_sample(i,std::move(s),trackr::synth::SampleOrigin::SdWav,names[i])==trackr::synth::SampleEditResult::Ok);
        }
        auto engine=std::make_unique<Engine>();
        std::array<PlayUnit*,3> drum{};
        for(int c=0;c<3;++c){
            drum[c]=new PlayUnit;engine->chains[c].units[0]=drum[c];
            param(*drum[c],PlayUnit::P_MODE,0);param(*drum[c],PlayUnit::P_LOOP,0);
            param(*drum[c],PlayUnit::P_SPEED,8192);
            engine->chains[c].volume=c==2?15500:18000;
        }
        auto* echo=new DelayUnit;engine->chains[1].units[1]=echo;
        param(*echo,DelayUnit::P_TIME,delay_time::free_value(STEP*3));
        param(*echo,DelayUnit::P_MIX,2200);param(*echo,DelayUnit::P_FEEDBACK,4000);
        param(*echo,DelayUnit::P_WOW,0);param(*echo,DelayUnit::P_SAT,0);
        auto* bass=new OscUnit;engine->chains[3].units[0]=bass;
        param(*bass,OscUnit::P_SHAPE,OscUnit::shape_anchor(OscUnit::Wave::Tri));
        param(*bass,OscUnit::P_SUB,7000);param(*bass,OscUnit::P_DRIFT,0);param(*bass,OscUnit::P_GAIN,12500);
        auto* filter=new FilterUnit;engine->chains[3].units[1]=filter;
        param(*filter,FilterUnit::P_CUTOFF,9000);param(*filter,FilterUnit::P_RES,9000);param(*filter,FilterUnit::P_DRIVE,4500);
        auto* env=new VcaUnit;engine->chains[3].units[2]=env;
        param(*env,VcaUnit::P_ATTACK,vca_envelope::time_value(80));
        param(*env,VcaUnit::P_DECAY,vca_envelope::time_value(5200));
        engine->chains[3].volume=18000;
        engine->set_bpm(200);engine->set_sequencer_enabled(false);engine->transport_play();
        std::vector<Hit> hits;
        auto add=[&](int bar,int step,int c,int sample,int gain,int offset=0,int speed=8192,int pan=0){
            const int f=bar*BAR+step*STEP+offset;
            if(f>=0&&f<BARS*BAR)hits.push_back({f,c,sample,gain,speed,pan});
        };
        for(int b=0;b<BARS;++b){
            const bool intro=b<4,half=b>=16&&b<20,fill=b%4==3,late=b>=28;
            const int root=std::array<int,4>{31,34,29,27}[(b/4)%4];
            // Kick/snare interplay, rather than a four-on-the-floor loop.
            std::vector<int> kicks=half?std::vector<int>{0,7,10}:std::vector<int>{0,6,8,11};
            if(b%2&&!half)kicks={0,3,8,10,14};
            if(fill&&!half)kicks={0,6,9,11,14};
            for(int s:kicks)add(b,s,0,s==0?1:0,s==0?29000:24000);
            for(int s:half?std::vector<int>{8}:std::vector<int>{4,12})add(b,s,1,2,30000,64,8192,500);
            // Low-velocity ghosts, snare replies, pitched roll at phrase endings.
            if(!intro&&!half){
                add(b,3,1,2,6500,-80,7900,-1100);
                add(b,b%2?9:10,1,2,10000,120,8400,1400);
                if(b%2)add(b,15,1,3,8500,-60,8192,-1200);
            }
            if(fill){
                add(b,13,1,2,13000,0,8000,-1400);
                add(b,14,1,2,19000,0,8400,1600);
                add(b,15,1,2,23000,0,9000,-1200);
                if(!half)add(b,15,1,2,12500,STEP/2,9800,1800);
            }
            // Alternating hats with tiny deterministic swing, occasional rim/conga.
            for(int s=0;s<16;++s){
                if((intro||half)&&(s%2))continue;
                if(!intro&&!half&&s%8==5)continue;
                const int offset=(s%2)?160:0;
                int source=s%8==6?5:4;
                int gain=s%4==0?16000:s%2?7500:11500;
                if(!intro&&!half&&s==7&&b%2){source=6;gain=15500;}
                if(!intro&&s==15&&b%4==2){source=7;gain=18000;}
                add(b,s,2,source,gain,offset,8192+(s%3-1)*160,(s%2?1:-1)*6500);
            }
            // Short bass answers; the drums stay in front, breakdown goes half-time.
            if(b>=2){
                const std::vector<int> steps=half?std::vector<int>{0,10}:std::vector<int>{0,3,6,8,11,14};
                for(int s:steps){
                    const int pitch=root+((s==6||s==14)?7:0)+(late&&s==11?12:0);
                    add(b,s,3,pitch,half?8500:12500,s%2?80:0,0,0);
                }
            }
            if(late&&b%2){
                for(int i=0;i<3;++i)add(b,14,1,3,16000+i*3500,i*STEP*2/3,8200+i*650,(i-1)*2500);
            }
        }
        std::stable_sort(hits.begin(),hits.end(),[](const Hit&a,const Hit&b){return a.frame<b.frame;});
        trackr::synth::Sample audio;audio.channels=2;audio.data.resize(TOTAL*2);
        int cursor=0;
        auto render_to=[&](int end){while(cursor<end){const int n=std::min(128,end-cursor);engine->render(audio.data.data()+cursor*2,n);cursor+=n;}};
        for(const Hit& h:hits){
            render_to(h.frame);engine->chains[h.chain].pan=q15(h.pan);
            if(h.chain<3){
                auto& p=*drum[h.chain];p.sample_slot=h.source;
                param(p,PlayUnit::P_GAIN,h.gain);param(p,PlayUnit::P_SPEED,h.speed);
            }else{
                param(*bass,OscUnit::P_PITCH,note(h.source));param(*bass,OscUnit::P_GAIN,h.gain);
                const int b=h.frame/BAR;
                param(*filter,FilterUnit::P_CUTOFF,(b>=16&&b<20)?6500:9000+(b%8)*650);
            }
            engine->trigger_chain(h.chain);
        }
        render_to(BARS*BAR);engine->transport_stop();render_to(TOTAL);
        int peak=0,clipped=0;long double squares=0;
        for(q15 v:audio.data){peak=std::max(peak,std::abs(int(v)));clipped+=(v==32767||v==-32768);squares+=int64_t(v)*v;}
        need(peak>0&&clipped==0);
        need(trackr::synth::save_sample_wav(output.c_str(),audio,SR,TOTAL)==trackr::synth::WavSaveResult::Ok);
        std::printf("200bpm; 32bars+1.6s tail; 40s stereo32kHz; %zu scripted events; peak=%d; rms=%.2f; clipped=%d\n",hits.size(),peak,double(std::sqrt(squares/audio.data.size())),clipped);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"breakbeat: %s\n",e.what());return 1;}
}
