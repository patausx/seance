#include "pad_io.h"
#include <cstdio>
#include <cstring>
namespace seance::padio {
static bool w8(FILE*f,uint8_t v){return fwrite(&v,1,1,f)==1;}
static bool w16(FILE*f,int16_t v){uint8_t b[2]={(uint8_t)v,(uint8_t)((uint16_t)v>>8)};return fwrite(b,1,2,f)==2;}
static bool r8(FILE*f,uint8_t&v){return fread(&v,1,1,f)==1;}
static bool r16(FILE*f,int16_t&v){uint8_t b[2];if(fread(b,1,2,f)!=2)return false;v=(int16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8));return true;}
bool save(const MacroPad pads[8],const char*path){if(!pads||!path)return false;char tmp[256];size_t n=strlen(path);if(n+4>=sizeof tmp)return false;memcpy(tmp,path,n);memcpy(tmp+n,".tmp",5);FILE*f=fopen(tmp,"wb");if(!f)return false;bool ok=fwrite("PAD1",1,4,f)==4&&w8(f,1);for(int p=0;p<8&&ok;++p){ok=fwrite(pads[p].name,1,sizeof pads[p].name,f)==sizeof pads[p].name&&w8(f,pads[p].route_count);for(int r=0;r<MacroPad::MAX_ROUTES&&ok;++r){const auto&x=pads[p].routes[r];ok=w8(f,x.chain)&&w8(f,x.slot)&&w8(f,x.param)&&w16(f,x.minimum)&&w16(f,x.maximum);}}ok=ok&&!ferror(f)&&fflush(f)==0;if(fclose(f)!=0)ok=false;if(!ok){remove(tmp);return false;}if(rename(tmp,path)!=0){remove(tmp);return false;}return true;}
bool load(MacroPad pads[8],const char*path){if(!pads||!path)return false;FILE*f=fopen(path,"rb");if(!f)return false;MacroPad parsed[8];char magic[4];uint8_t version;bool ok=fread(magic,1,4,f)==4&&!memcmp(magic,"PAD1",4)&&r8(f,version)&&version==1;for(int p=0;p<8&&ok;++p){ok=fread(parsed[p].name,1,sizeof parsed[p].name,f)==sizeof parsed[p].name&&r8(f,parsed[p].route_count);parsed[p].name[sizeof parsed[p].name-1]=0;if(parsed[p].route_count>MacroPad::MAX_ROUTES)ok=false;for(int r=0;r<MacroPad::MAX_ROUTES&&ok;++r){auto&x=parsed[p].routes[r];int16_t lo=0,hi=0;ok=r8(f,x.chain)&&r8(f,x.slot)&&r8(f,x.param)&&r16(f,lo)&&r16(f,hi);if(ok){x.minimum=lo;x.maximum=hi;if(x.chain>=NUM_CHAINS||x.slot>=UNITS_PER_CHAIN||x.param>=MAX_PARAMS)ok=false;}}}int tail=fgetc(f);if(tail!=EOF)ok=false;fclose(f);if(ok)memcpy(pads,parsed,sizeof parsed);return ok;}
}
