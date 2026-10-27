// Included inside step_focus_cases. Independent approved selection specification:
// no calls to step_focus geometry, clipping, fill or glyph replay helpers.
struct OraclePlate { int x,y,w,h; };
struct OraclePose { double x,y,w,h; };
OraclePose target_pose(int chain,int local) { return {8.+local*19.,double(layout::ROWS[chain]),18.,18.}; }
OraclePose travel(OraclePose from,OraclePose to,unsigned ms) {
    if(!ms)return from;if(ms>=100)return to;
    const double t=ms/100.,r=1-t,e=1-r*r*r,p=std::sin(3.14159265358979323846*t)*r*.7;
    const double stretch=std::min(std::abs(to.x-from.x),12.)*p,squash=3*p;
    OraclePose out{from.x+(to.x-from.x)*e-stretch/2,from.y+(to.y-from.y)*e+squash/2,
                   from.w+(to.w-from.w)*e+stretch,from.h+(to.h-from.h)*e-squash};
    out.w=std::clamp(out.w,3.,30.);out.h=std::clamp(out.h,12.,18.);
    out.x=std::clamp(out.x,0.,320.-out.w);out.y=std::clamp(out.y,41.,192.-out.h);return out;
}
OraclePlate raster(OraclePose p) { auto round=[](double v){return int(std::floor(v+.5));};return {round(p.x),round(p.y),round(p.w),round(p.h)}; }
OraclePlate oracle_plate(int c0,int s0,int c1,int s1,unsigned ms) { return raster(travel(target_pose(c0,s0),target_pose(c1,s1),ms)); }
bool covered(OraclePlate p,int x,int y) {
    const int yy=y-p.y;if(yy<0||yy>=p.h)return false;
    const int inset=yy==0||yy==p.h-1?2:yy==1||yy==p.h-2?1:0;
    return x>=p.x+inset&&x<p.x+p.w-inset;
}
bool native_glyph_pixel(char ch,int gx,int gy,int x,int y) {
    const int xx=x-gx,yy=y-gy;return xx>=0&&xx<6&&yy>=0&&yy<8&&(trackr::ui::glyph_row(ch,yy)&(1u<<(5-xx)));
}
bool stroke_pixel(int x0,int y0,int x1,int y1,int x,int y) {
    // Ordinary host diagonals are integer Bresenham. Inverse runtime strokes
    // are clipped butt-ended Citro2D quads. Test the latter independently using
    // perpendicular distance + endpoint projection at the host pixel centre.
    // No slash/curve pixel is skipped or accepted by a distance-to-expected test.
    if(y0==y1)return y==y0&&x>=std::min(x0,x1)&&x<=std::max(x0,x1);
    if(x0==x1)return x==x0&&y>=std::min(y0,y1)&&y<=std::max(y0,y1);
    const double dx=x1-x0,dy=y1-y0,px=x+.5-x0,py=y+.5-y0,len2=dx*dx+dy*dy;
    const double projection=px*dx+py*dy,cross=px*dy-py*dx;
    return projection>=0&&projection<=len2&&cross*cross<=len2*.25;
}
bool flow_inverse_pixel(int chain,int x,int y);
bool event_inverse_pixel(const std::vector<u32>& native,int layer,int x,int y) {
    bool glyph=native[y*320+x]==m::ink()||native[y*320+x]==m::ghost()||native[y*320+x]==m::branch();
    // A travelling event plate replays every crossed lane, including column
    // gaps. Flow diagonals need quad, not the ordinary host Bresenham coverage.
    if(layer==0)for(int row=0;row<4;++row)
        if((SequenceLifecycle)sequencer_snapshot().sequence_lifecycle[row]==SequenceLifecycle::Flow&&y>=layout::ROWS[row]&&y<layout::ROWS[row]+31)
            glyph=flow_inverse_pixel(row,x,y);
    for(int row=0;row<4;++row)for(int local=0;local<16;++local) {
        const int cx=8+19*local,cy=layout::ROWS[row];
        if(x<cx||x>=cx+18||y<cy||y>=cy+31)continue;
        const bool flow=layer==0&&(SequenceLifecycle)sequencer_snapshot().sequence_lifecycle[row]==SequenceLifecycle::Flow;
        if(layer==4) {
            const int condition=int(g_eng->pattern.steps[row][sequencer_current_page()*16+local].condition());
            if((condition==0||condition==4||condition==5)&&x>=cx+6&&x<=cx+10&&y>=cy+5&&y<=cy+11)
                glyph=native_glyph_pixel(condition==5?'2':'1',cx+2,cy+1,x,y)||
                      native_glyph_pixel(condition==0?'1':'2',cx+10,cy+9,x,y)||stroke_pixel(cx+6,cy+11,cx+10,cy+5,x,y);
        }
        if(sequencer_current_page()*16+local>=sequencer_snapshot().sequence_length[row]&&x>=cx+6&&x<=cx+10&&y>=cy+19&&y<=cy+21)
            glyph=(flow&&flow_inverse_pixel(row,x,y))||stroke_pixel(cx+6,cy+21,cx+10,cy+19,x,y);
    }
    return glyph;
}
void plate_oracle(const std::vector<u32>& actual,const std::vector<u32>& native,OraclePlate plate,int layer,bool full=true) {
    CHECK(actual.size()==76800&&native.size()==actual.size());
    CHECK(plate.x>=0&&plate.y>=41&&plate.x+plate.w<=320&&plate.y+plate.h<=192);
    int mask=0,inverse=0;
    for(int y=full?0:41;y<(full?240:194);++y)for(int x=0;x<320;++x) {
        u32 wanted=native[y*320+x];if(covered(plate,x,y)){++mask;const bool glyph=event_inverse_pixel(native,layer,x,y);inverse+=glyph;wanted=glyph?col_text_i():m::spot();}
        if(actual[y*320+x]!=wanted)std::fprintf(stderr,"plate oracle layer=%d xy=%d,%d got=%08x want=%08x plate=%d,%d,%d,%d\n",layer,x,y,actual[y*320+x],wanted,plate.x,plate.y,plate.w,plate.h);
        CHECK(actual[y*320+x]==wanted);++checked_pixels;
    }
    CHECK(mask==plate.w*plate.h-12);inverse_pixels+=inverse;++exact_plate_masks;
}
int flow_y(int chain,int local) {
    const int raw=std::min(std::abs(int(sequencer_snapshot().sequence_value[chain][sequencer_current_page()*16+local])),32767);
    const int percent=(raw*100+16383)/32767;return layout::ROWS[chain]+26-percent*23/100;
}
OraclePlate flow_plate(int chain,int local) { return {8+local*19,flow_y(chain,local)-5,18,10}; }
bool flow_inverse_pixel(int chain,int x,int y) {
    for(int local=0;local<16;++local) {
        const int xx=16+local*19,yy=flow_y(chain,local);
        if(y==yy&&x>=xx-1&&x<=xx+1)return true; // unchanged radius1 point
        if(local&&stroke_pixel(xx-19,flow_y(chain,local-1),xx,yy,x,y))return true;
    }
    return false;
}
void flow_oracle(const std::vector<u32>& actual,const std::vector<u32>& native,int chain,int local) {
    CHECK(actual.size()==76800&&native.size()==actual.size());const auto p=flow_plate(chain,local);
    CHECK(p.x>=8&&p.x+p.w<=311&&p.y>=43&&p.y+p.h<=190);int mask=0,ink=0;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
        u32 wanted=native[y*320+x];if(covered(p,x,y)){++mask;const bool glyph=flow_inverse_pixel(chain,x,y);ink+=glyph;wanted=glyph?col_text_i():m::spot();}
        if(actual[y*320+x]!=wanted)std::fprintf(stderr,"flow oracle row=%d step=%d xy=%d,%d got=%08x want=%08x plate=%d,%d,%d,%d\n",chain,local,x,y,actual[y*320+x],wanted,p.x,p.y,p.w,p.h);
        CHECK(actual[y*320+x]==wanted);++checked_pixels;
    }
    CHECK(mask==168&&ink>=3);inverse_pixels+=ink;++exact_plate_masks;
}
