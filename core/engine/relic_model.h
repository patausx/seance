#pragma once
// Bounded production material owner. No source/PCM, HID, draw clock or DSP here.
#include "relic_surface_data.h"

namespace seance::relic {

// Portable semantic fields, NOT a memcpy/wire format. A patch codec must encode
// each field explicitly (including endianness), and hydrate via restore(). No
// token, sample clock, live attachment, process address or measured float is saved.
struct MaterialPose {
    Vertex vertices[SURFACE_VERTICES] = {};
    int16_t dent[SURFACE_VERTICES] = {};   // <=0, relative to the local undented base
    int16_t relief[SURFACE_VERTICES] = {}; // actual traveling Z field, Q7 pixels
};
struct Persistent {
    uint32_t topology = surface_data::topology;
    uint32_t phase1 = 0, phase2 = 0;
    uint16_t version = 1, vertex_count = SURFACE_VERTICES, face_count = SURFACE_FACES;
    uint8_t hold = 0, reserved = 0;
    MaterialPose material;
};

class Model {
public:
    static constexpr unsigned MAX_TRIALS = 12;
    static constexpr int MAX_DELTA_Q7 = 66 * 128;
    static constexpr int MAX_RELIEF_Q7 = 12 * 128;
    Model() { rest_runtime(); }
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete; // clone authored material via capture/restore, never a live owner
    const Surface& surface() const { return surface_; }
    const Features& features() const { return surface_.features; }
    static const Face* faces() { return surface_data::faces; }

    // Screen projector: x=160+x, y=122+.94*y-.36*z. Integer intersection of
    // EVERY current triangle, including the skirt; frontmost z+.12*y wins.
    // Barycentric weights sum to 65535; this is a material attachment, no hotspot.
    uint32_t begin(int screen_x, int screen_y) {
        if (surface_.held || generation_ == UINT32_MAX || screen_x < 25 || screen_x > 291 ||
            screen_y < 39 || screen_y > 193) return 0;
        const int64_t qx = int64_t(screen_x - 160) * 12800;
        const int64_t qy = int64_t(screen_y - 122) * 12800;
        int64_t best_depth = INT64_MIN;
        uint16_t best_face = 0, best_bary[3] = {};
        for (unsigned k = 0; k < SURFACE_FACES; ++k) {
            const Face f = faces()[k];
            const Vertex a = surface_.vertices[f.a], b = surface_.vertices[f.b], c = surface_.vertices[f.c];
            const int64_t ax = int32_t(a.x)*100, ay = int32_t(a.y)*94-int32_t(a.z)*36;
            const int64_t bx = int32_t(b.x)*100, by = int32_t(b.y)*94-int32_t(b.z)*36;
            const int64_t cx = int32_t(c.x)*100, cy = int32_t(c.y)*94-int32_t(c.z)*36;
            int64_t den = cross2(bx-ax,by-ay,cx-ax,cy-ay);
            if (!den) continue;
            int64_t u = cross2(bx-qx,by-qy,cx-qx,cy-qy);
            int64_t v = cross2(cx-qx,cy-qy,ax-qx,ay-qy);
            if (den < 0) { den = -den; u = -u; v = -v; }
            const int64_t w = den-u-v;
            if (u < 0 || v < 0 || w < 0) continue;
            const uint16_t bu = uint16_t(u*65535/den), bv = uint16_t(v*65535/den);
            const uint16_t bw = uint16_t(65535-uint32_t(bu)-bv);
            const int64_t depth = int64_t(bu)*(int32_t(a.z)*100+int32_t(a.y)*12) +
                                  int64_t(bv)*(int32_t(b.z)*100+int32_t(b.y)*12) +
                                  int64_t(bw)*(int32_t(c.z)*100+int32_t(c.y)*12);
            if (depth <= best_depth) continue;
            best_depth = depth; best_face = uint16_t(k);
            best_bary[0] = bu; best_bary[1] = bv; best_bary[2] = bw;
        }
        if (best_depth == INT64_MIN) return 0;
        capture_pose(capture_);
        const Face f = faces()[best_face];
        const Vertex a = surface_.vertices[f.a], b = surface_.vertices[f.b], c = surface_.vertices[f.c];
        const int32_t ox = int32_t((int64_t(a.x)*best_bary[0]+int64_t(b.x)*best_bary[1]+int64_t(c.x)*best_bary[2])/65535);
        const int32_t oy = int32_t((int64_t(a.y)*best_bary[0]+int64_t(b.y)*best_bary[1]+int64_t(c.y)*best_bary[2])/65535);
        for (unsigned i = 0; i < SURFACE_VERTICES; ++i) {
            const int32_t x = int32_t(surface_.vertices[i].x)-ox, y = int32_t(surface_.vertices[i].y)-oy;
            xy_weight_[i] = kernel(x,y,38*128,32*128);
            dent_weight_[i] = kernel(x,y,24*128,20*128);
        }
        origin_x_ = ox; origin_y_ = oy;
        input_x_ = input_y_ = input_depth_ = 0;
        surface_.face = best_face;
        for (unsigned i=0; i<3; ++i) surface_.bary[i] = best_bary[i];
        surface_.touch_token = ++generation_;
        surface_.held = true;
        return generation_;
    }

    // Absolute local displacement FROM BEGIN, never integrated from the last
    // admitted move. Retrying a limited target cannot creep through the gate.
    // Depth is independent 0..32767; pad calibration belongs to the HID owner.
    bool move(uint32_t token, int dx_q7, int dy_q7, q15 depth) {
        if (!owned(token) || depth < 0) return false;
        bound_delta(dx_q7,dy_q7);
        if (dx_q7 == input_x_ && dy_q7 == input_y_ && depth == input_depth_) return true;
        make_move(dx_q7,dy_q7,depth,xy_weight_);
        if (valid(trial_)) return commit_move(dx_q7,dy_q7,depth);

        // Admit the local dent separately. An XY topology/boundary limit must
        // not halve pad depth or renormalize an unrelated captured dent.
        unsigned budget = MAX_TRIALS-1;
        int admitted_depth = depth;
        make_move(0,0,depth,xy_weight_); --budget;
        if (!valid(trial_)) {
            make_move(0,0,0,xy_weight_); --budget;
            if (!valid(trial_)) return false;
            make_move(0,0,depth,xy_weight_);
            int fraction = path_limit(false); --budget;
            bool admitted = false;
            while (budget > 4) {
                admitted_depth = scale(depth,fraction);
                make_move(0,0,admitted_depth,xy_weight_); --budget;
                if (valid(trial_)) { admitted = true; break; }
                fraction = maximum(0,fraction-4); // Q7 endpoint rounding, not dyadic admission
            }
            if (!admitted) return false;
        }
        if (!dx_q7 && !dy_q7) return commit_move(dx_q7,dy_q7,depth);
        if (admitted_depth != depth) {
            make_move(dx_q7,dy_q7,admitted_depth,xy_weight_); --budget;
            if (valid(trial_)) return commit_move(dx_q7,dy_q7,depth);
        }

        // Both XY paths are fixed FROM BEGIN for this ray. Limit the 66px ray,
        // then take min(request,limit): growing input cannot jump to D/2^t.
        // The alternate path is directional and used ONLY after local admission
        // fails. Its attached triangle is a plateau, not a nearest-vertex hotspot.
        const uint64_t square = uint64_t(int64_t(dx_q7)*dx_q7+int64_t(dy_q7)*dy_q7);
        int length = int(root(square));
        if (uint64_t(length)*length < square) ++length;
        const int ray_x = int(int64_t(dx_q7)*MAX_DELTA_Q7/length);
        const int ray_y = int(int64_t(dy_q7)*MAX_DELTA_Q7/length);
        const int request = minimum(32767,int(int64_t(length)*32767/MAX_DELTA_Q7));
        make_move(ray_x,ray_y,admitted_depth,xy_weight_);
        int fraction = minimum(request,path_limit(true)); --budget;
        directional_weights(ray_x,ray_y);
        make_move(ray_x,ray_y,admitted_depth,directional_weight_);
        const int wider = minimum(request,path_limit(true)); --budget;
        const uint16_t* weights = xy_weight_;
        if (int64_t(wider)*contact_weight(directional_weight_) > int64_t(fraction)*contact_weight(xy_weight_)) {
            weights = directional_weight_; fraction = wider;
        }
        int rounding_backoff = 4;
        for (unsigned t=0; t<budget; ++t) {
            make_move(scale(ray_x,fraction),scale(ray_y,fraction),admitted_depth,weights);
            if (valid(trial_)) return commit_move(dx_q7,dy_q7,depth);
            fraction = maximum(0,fraction-rounding_backoff);
            rounding_backoff *= 2;
        }
        // No invalid last trial, release teleport, accumulation or ownership loss.
        return false;
    }
    bool lift(uint32_t token) {
        if (!owned(token)) return false;
        clear_owner();
        return true;
    }
    bool cancel(uint32_t token = 0) {
        if (!surface_.held || (token && !owned(token))) return false;
        clear_owner(); // commits LAST actual material; no synthetic final move
        return true;
    }
    void set_hold(bool value) { surface_.hold = value; }
    bool hold() const { return surface_.hold; }

    // Call from the engine sample timeline even if stopped/source missing/audio
    // disabled, never from a painter. Each step is exactly 320 samples. Splits
    // 1/31/32/137/320/512 do not change geometry, phase or measured features.
    void advance(uint32_t frames) {
        surface_.sample_clock += frames; // unsigned, defined wrap after ~18M years
        const uint64_t total = uint64_t(remainder_) + frames;
        uint32_t ticks = uint32_t(total/MECHANICS_FRAMES);
        remainder_ = uint16_t(total%MECHANICS_FRAMES);
        if (surface_.hold) return; // no passive recomputation, including features
        while (ticks--) step();
    }

    void capture(Persistent& p) const {
        p.topology = surface_data::topology; p.version = 1;
        p.vertex_count = SURFACE_VERTICES; p.face_count = SURFACE_FACES;
        p.phase1 = phase1_; p.phase2 = phase2_; p.hold = uint8_t(surface_.hold); p.reserved = 0;
        capture_pose(p.material);
    }
    static bool validate(const Persistent& p) {
        return p.version == 1 && p.topology == surface_data::topology &&
               p.vertex_count == SURFACE_VERTICES && p.face_count == SURFACE_FACES &&
               p.hold <= 1 && !p.reserved && valid(p.material);
    }
    bool restore(const Persistent& p) {
        if (!validate(p)) return false; // transactional; a stale live token stays live on failure
        install(p.material);
        phase1_ = p.phase1; phase2_ = p.phase2; surface_.hold = p.hold != 0;
        surface_.sample_clock = 0; remainder_ = 0; clear_owner();
        return true;
    }
    // Engine restart cancels the live owner. A deliberately restored/authored
    // held pose survives, including actual wave field/phase; free runtime resets
    // to neutral rest. Generation is NOT reset: old tokens can never revive.
    void reset_runtime() {
        clear_owner(); surface_.sample_clock = 0; remainder_ = 0;
        if (!surface_.hold) rest_runtime();
    }

private:
#ifdef SEANCE_RELIC_MODEL_TEST
    friend struct ModelTestAccess;
#endif
    Surface surface_;
    int16_t dent_[SURFACE_VERTICES] = {}, relief_[SURFACE_VERTICES] = {};
    MaterialPose capture_, trial_; // scratch is inline, never the audio-thread stack
    uint16_t xy_weight_[SURFACE_VERTICES] = {}, dent_weight_[SURFACE_VERTICES] = {};
    uint16_t directional_weight_[SURFACE_VERTICES] = {};
    int origin_x_ = 0, origin_y_ = 0;
    uint32_t generation_ = 0, phase1_ = 0, phase2_ = 0;
    int input_x_ = 0, input_y_ = 0, input_depth_ = 0;
    uint16_t remainder_ = 0;

    static int minimum(int a,int b) { return a < b ? a : b; }
    static int maximum(int a,int b) { return a > b ? a : b; }
    static int scale(int a,int b) { return int(int64_t(a)*b/32767); }
    static int64_t cross2(int64_t x,int64_t y,int64_t u,int64_t v) { return x*v-y*u; }
    // Fixed 32-iteration integer root. INT_MIN is widened before negation/square.
    static uint64_t root(uint64_t n) {
        uint64_t result = 0, bit = uint64_t(1)<<62;
        for (unsigned i=0; i<32; ++i,bit>>=2) {
            if (n >= result+bit) { n -= result+bit; result = (result>>1)+bit; }
            else result >>= 1;
        }
        return result;
    }
    static void bound_delta(int& x,int& y) {
        const int64_t xx=x, yy=y;
        const uint64_t square = uint64_t(xx*xx)+uint64_t(yy*yy);
        uint64_t length = root(square);
        if (length*length < square) ++length; // ceil: never exceed 66 native pixels
        if (length <= MAX_DELTA_Q7) return;
        x = int(xx*MAX_DELTA_Q7/int64_t(length)); y = int(yy*MAX_DELTA_Q7/int64_t(length));
    }
    // Compact C1 smoothstep of elliptical squared radius. All zero beyond local
    // support: subsequent held grabs cannot move old distant dents/vertices.
    static uint16_t kernel(int x,int y,int rx,int ry) {
        const int64_t r = int64_t(x)*x*32767/(int64_t(rx)*rx) + int64_t(y)*y*32767/(int64_t(ry)*ry);
        if (r >= 32767) return 0;
        const int64_t w = 32767-r;
        return uint16_t(w*w*(3*32767-2*w)/(int64_t(32767)*32767));
    }
    void make_move(int dx,int dy,int depth,const uint16_t* weights) {
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            const Vertex b = capture_.vertices[i];
            const int base_z = int(b.z)-capture_.relief[i];
            const int depth_limit = minimum(23*128,maximum(0,base_z-128)*55/100);
            const int dz = -scale(scale(depth_limit,depth),dent_weight_[i]);
            trial_.vertices[i] = {int16_t(b.x+scale(dx,weights[i])),int16_t(b.y+scale(dy,weights[i])),
                                 int16_t(base_z+dz+relief_[i])};
            trial_.dent[i] = int16_t(capture_.dent[i]+dz);
            trial_.relief[i] = relief_[i];
        }
    }
    bool commit_move(int dx,int dy,int depth) {
        install(trial_); input_x_=dx; input_y_=dy; input_depth_=depth; return true;
    }
    int64_t contact_weight(const uint16_t* weights) const {
        const Face f = faces()[surface_.face];
        return int64_t(weights[f.a])*surface_.bary[0]+int64_t(weights[f.b])*surface_.bary[1]+
               int64_t(weights[f.c])*surface_.bary[2];
    }
    void directional_weights(int dx,int dy) {
        const int length = int(root(uint64_t(int64_t(dx)*dx+int64_t(dy)*dy)));
        const Face f = faces()[surface_.face];
        int rear=0,front=0,cross=0;
        const unsigned ids[3] = {f.a,f.b,f.c};
        for (unsigned id : ids) {
            const int x=int(capture_.vertices[id].x)-origin_x_,y=int(capture_.vertices[id].y)-origin_y_;
            const int l=int((int64_t(x)*dx+int64_t(y)*dy)/length);
            const int c=int((-int64_t(x)*dy+int64_t(y)*dx)/length);
            rear=maximum(rear,-l); front=maximum(front,l); cross=maximum(cross,c < 0 ? -c : c);
        }
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            const int x=int(capture_.vertices[i].x)-origin_x_,y=int(capture_.vertices[i].y)-origin_y_;
            int l=int((int64_t(x)*dx+int64_t(y)*dy)/length);
            int c=int((-int64_t(x)*dy+int64_t(y)*dx)/length);
            l = l < 0 ? -maximum(0,-l-rear) : maximum(0,l-front);
            c = maximum(0,(c < 0 ? -c : c)-cross);
            // Fixed maximum ray support gives a rank-one XY path: determinant
            // varies only with its longitudinal gradient. Rear/cross stay local.
            directional_weight_[i] = uint16_t(maximum(xy_weight_[i],kernel(l,c,
                l > 0 ? 38*128+3*MAX_DELTA_Q7 : 38*128,32*128)));
        }
    }
    static void limit_margin(int64_t margin,int64_t slope,int& fraction) {
        if (margin < 0) { fraction=0; return; }
        if (slope >= 0 || margin >= -slope) return;
        // Use the exact ratio when safe; otherwise ceil-divide the denominator
        // first to avoid side-normal * Q15 overflow. valid() is the authority.
        const int64_t ratio = margin <= INT64_MAX/32767 ? margin*32767/(-slope) :
                              margin/((-slope+32766)/32767);
        fraction=minimum(fraction,int(ratio));
    }
    static void limit_range(int from,int to,int low,int high,int& fraction) {
        limit_margin(from-low,to-from,fraction);
        limit_margin(high-from,from-to,fraction);
    }
    static int64_t side_dot(Vertex a,Vertex b,Vertex c,Vertex ra,Vertex rb,Vertex rc) {
        const int64_t nx=cross2(int(b.y)-a.y,int(b.z)-a.z,int(c.y)-a.y,int(c.z)-a.z);
        const int64_t ny=cross2(int(b.z)-a.z,int(b.x)-a.x,int(c.z)-a.z,int(c.x)-a.x);
        const int64_t rx=cross2(int(rb.y)-ra.y,int(rb.z)-ra.z,int(rc.y)-ra.y,int(rc.z)-ra.z);
        const int64_t ry=cross2(int(rb.z)-ra.z,int(rb.x)-ra.x,int(rc.z)-ra.z,int(rc.x)-ra.x);
        return nx*rx+ny*ry;
    }
    Vertex path_base(unsigned i,bool xy) const {
        Vertex b=capture_.vertices[i];
        b.z=xy ? trial_.vertices[i].z : int16_t(int(b.z)-capture_.relief[i]+relief_[i]);
        return b;
    }
    // A fixed rank-one XY pull with fixed Z, or a dent with fixed XY, has affine
    // bounds/top/skirt admission. Intersect these half-lines, not dyadic targets.
    // Quantized endpoints need the unchanged valid() guard before installation.
    int path_limit(bool xy) const {
        int fraction=32767;
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            const Vertex a=path_base(i,xy),b=trial_.vertices[i];
            limit_range(a.x,b.x,-135*128,131*128,fraction);
            limit_range(int(a.y)*94,int(b.y)*94,-83*12800,71*12800,fraction);
            limit_range(int(a.y)*94-int(a.z)*36,int(b.y)*94-int(b.z)*36,-83*12800,71*12800,fraction);
        }
        for (unsigned k=0; k<SURFACE_FACES; ++k) {
            const Face f=faces()[k];
            const Vertex a=path_base(f.a,xy),b=path_base(f.b,xy),c=path_base(f.c,xy);
            const Vertex ta=trial_.vertices[f.a],tb=trial_.vertices[f.b],tc=trial_.vertices[f.c];
            if (k < SURFACE_FACES-2*SURFACE_SIDES) {
                const int64_t ar=cross2(int(b.x)-a.x,int(b.y)-a.y,int(c.x)-a.x,int(c.y)-a.y);
                const int64_t target=cross2(int(tb.x)-ta.x,int(tb.y)-ta.y,int(tc.x)-ta.x,int(tc.y)-ta.y);
                limit_margin(ar*1000-int64_t(surface_data::top_area[k])*205,(target-ar)*1000,fraction);
            } else {
                const Vertex ra=surface_data::rest[f.a],rb=surface_data::rest[f.b],rc=surface_data::rest[f.c];
                const int64_t ar=side_dot(a,b,c,ra,rb,rc),target=side_dot(ta,tb,tc,ra,rb,rc);
                limit_margin(ar*1000-side_dot(ra,rb,rc,ra,rb,rc)*205,(target-ar)*1000,fraction);
            }
        }
        return fraction;
    }
    static int sine(uint32_t phase) {
        const unsigned i = phase>>22, fraction = (phase>>6)&65535;
        const int a = surface_data::sine[i], b = surface_data::sine[(i+1)&1023];
        return a + int(int64_t(b-a)*fraction/65536);
    }
    int raw_relief(unsigned i) const {
        const auto& s = surface_data::sensors[i];
        return int((int64_t(525)*sine(s.phase1-phase1_)+int64_t(192)*sine(s.phase2-phase2_))*s.cap/
                   (int64_t(32767)*32767));
    }
    static int decay(int value,int divisor) {
        // Toward zero without sign crossing or quantized residual sticking.
        return value > 0 ? value-maximum(1,value/divisor) : value < 0 ? value+maximum(1,-value/divisor) : 0;
    }
    static int follow(int from,int to) {
        const int d=to-from;
        return from + (d > 0 ? maximum(1,d/35) : d < 0 ? -maximum(1,-d/35) : 0);
    }
    bool owned(uint32_t token) const { return token && surface_.held && token == surface_.touch_token; }
    void clear_owner() {
        surface_.held = false; surface_.touch_token = 0; surface_.face = 0;
        for (auto& b:surface_.bary) b=0;
        input_x_ = input_y_ = input_depth_ = 0;
    }
    void capture_pose(MaterialPose& p) const {
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            p.vertices[i]=surface_.vertices[i]; p.dent[i]=dent_[i]; p.relief[i]=relief_[i];
        }
    }
    static bool valid(const MaterialPose& p) {
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            const Vertex v=p.vertices[i];
            if (v.x < -135*128 || v.x > 131*128 || v.z < 0 || v.z > 120*128) return false;
            const int floor=int32_t(v.y)*94, projected=floor-int32_t(v.z)*36;
            if (floor < -83*12800 || floor > 71*12800 || projected < -83*12800 || projected > 71*12800) return false;
            if (p.dent[i] > 0 || p.dent[i] < -120*128 || p.relief[i] < -MAX_RELIEF_Q7 || p.relief[i] > MAX_RELIEF_Q7) return false;
            const int undented = int(v.z)-p.relief[i]-p.dent[i], no_wave=int(v.z)-p.relief[i];
            if (undented < 0 || undented > 120*128 || no_wave < 0 || no_wave > 120*128) return false;
            if (i < SURFACE_TOP) { if (no_wave < 128) return false; }
            else {
                const Vertex rim=p.vertices[i-SURFACE_SIDES];
                if (v.z || p.dent[i] || p.relief[i] || v.x != rim.x || v.y != rim.y) return false;
            }
        }
        for (unsigned k=0; k<SURFACE_FACES; ++k) {
            const Face f=faces()[k]; const Vertex a=p.vertices[f.a],b=p.vertices[f.b],c=p.vertices[f.c];
            if (k < SURFACE_FACES-2*SURFACE_SIDES) {
                const int64_t ar=cross2(int(b.x)-a.x,int(b.y)-a.y,int(c.x)-a.x,int(c.y)-a.y);
                if (ar*1000 <= int64_t(surface_data::top_area[k])*205) return false;
            } else {
                // Vertical skirt has zero XY area. Its 3D outward winding and
                // side volume are guarded against the fixed rest side normal.
                const Vertex ra=surface_data::rest[f.a],rb=surface_data::rest[f.b],rc=surface_data::rest[f.c];
                const int64_t nx=cross2(int(b.y)-a.y,int(b.z)-a.z,int(c.y)-a.y,int(c.z)-a.z);
                const int64_t ny=cross2(int(b.z)-a.z,int(b.x)-a.x,int(c.z)-a.z,int(c.x)-a.x);
                const int64_t rx=cross2(int(rb.y)-ra.y,int(rb.z)-ra.z,int(rc.y)-ra.y,int(rc.z)-ra.z);
                const int64_t ry=cross2(int(rb.z)-ra.z,int(rb.x)-ra.x,int(rc.z)-ra.z,int(rc.x)-ra.x);
                if ((nx*rx+ny*ry)*1000 <= (rx*rx+ry*ry)*205) return false;
            }
        }
        return true;
    }
    void measure() {
        int pull=0,compress=0;
        int64_t bend_sum=0,bend_weight=0,wave_sum=0,wave_weight=0;
        for (unsigned i=0; i<SURFACE_TOP; ++i) {
            const auto& s=surface_data::sensors[i];
            const int dx=int(surface_.vertices[i].x)-surface_data::rest[i].x;
            const int dy=int(surface_.vertices[i].y)-surface_data::rest[i].y;
            const int64_t radial_numerator=int64_t(dx)*s.nx+int64_t(dy)*s.ny;
            const int radial=int(radial_numerator/32767);
            const int tangent=int((-int64_t(dx)*s.ny+int64_t(dy)*s.nx)/32767);
            // Keep sub-Q7 radial weights for the selected bend law: discarding
            // them would falsely report zero for a nearly tangential old pose.
            const int radial_weight=maximum(0,int(radial_numerator/128));
            pull=maximum(pull,radial); compress=maximum(compress,-radial*14/16);
            if (s.cap > 11000) compress=maximum(compress,-dent_[i]);
            if (radial_weight) { bend_sum += int64_t(radial_weight)*tangent; bend_weight += radial_weight; }
            wave_sum += int64_t(s.wave_weight)*relief_[i]; wave_weight += s.wave_weight;
        }
        // Only these final observations are floats, at the mechanics/control rate.
        // They keep the selected radial/dent/tangential/sensed-relief laws; none
        // is a raw cursor, height/width shortcut or implicit audio mode switch.
        Features f;
        f.pull = minimum(pull,50*128)/float(50*128);
        f.compress = minimum(compress,14*128)/float(14*128);
        f.bend = bend_weight ? float(bend_sum)/float(bend_weight*32*128) : 0;
        if (f.bend > 1) f.bend=1;
        if (f.bend < -1) f.bend=-1;
        f.wave = wave_weight ? float(wave_sum)/float(wave_weight*32*128) : 0;
        surface_.features=f;
    }
    void install(const MaterialPose& p) {
        bool changed=false;
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            const Vertex a=surface_.vertices[i],b=p.vertices[i];
            changed |= a.x!=b.x || a.y!=b.y || a.z!=b.z || dent_[i]!=p.dent[i] || relief_[i]!=p.relief[i];
            surface_.vertices[i]=b; dent_[i]=p.dent[i]; relief_[i]=p.relief[i];
        }
        if (changed) { ++surface_.revision; measure(); }
    }
    void rest_runtime() {
        for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
            surface_.vertices[i]=surface_data::rest[i]; dent_[i]=relief_[i]=0;
        }
        phase1_=phase2_=0; surface_.features={}; ++surface_.revision;
    }
    void step() {
        phase1_ += surface_data::phase_step1; phase2_ += surface_data::phase_step2;
        int64_t sum=0;
        for (unsigned i=0; i<SURFACE_TOP; ++i) sum += int64_t(surface_data::support[i])*raw_relief(i);
        const int mean=int(sum*32767/int64_t(surface_data::wave_mass));
        for (unsigned t=0; t<MAX_TRIALS; ++t) {
            const int fraction=1<<t;
            for (unsigned i=0; i<SURFACE_VERTICES; ++i) {
                const Vertex v=surface_.vertices[i],r=surface_data::rest[i];
                const int x=surface_.held ? v.x : r.x+decay(int(v.x)-r.x,78);
                const int y=surface_.held ? v.y : r.y+decay(int(v.y)-r.y,78);
                const int old_base=int(v.z)-dent_[i]-relief_[i];
                const int base=surface_.held ? old_base : r.z+decay(old_base-r.z,78);
                const int dent=surface_.held ? dent_[i] : decay(dent_[i],105);
                const int target=raw_relief(i)-scale(mean,surface_data::sensors[i].cap);
                const int wave=follow(relief_[i],target);
                trial_.dent[i]=int16_t(dent_[i]+(dent-dent_[i])/fraction);
                trial_.relief[i]=int16_t(relief_[i]+(wave-relief_[i])/fraction);
                trial_.vertices[i]={int16_t(v.x+(x-v.x)/fraction),int16_t(v.y+(y-v.y)/fraction),
                    int16_t(v.z+(base-old_base)/fraction+(dent-dent_[i])/fraction+(wave-relief_[i])/fraction)};
            }
            if (!valid(trial_)) continue;
            install(trial_); return;
        }
        // Rare fully blocked relaxation retains the LAST valid pose, not rest.
        // Phase still advances on the engine timeline; hold freezes it separately.
    }
};
static_assert(sizeof(Model) <= 8192, "bounded inline material owner");
static_assert(sizeof(Persistent) <= 2304, "bounded portable material payload");
} // namespace seance::relic
