#!/usr/bin/env python3
"""30s/192bpm editorial promo from FRESH current C++ captures only.

Native LCD images are never redrawn or recolored. Motion, crop, typography,
logo retiming and audio-responsive accents belong to the surrounding edit.
Requires Pillow and ffmpeg; no API, device work, old promo footage or UI mockups.
"""
from __future__ import annotations
import argparse
from array import array
import bisect
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time
import wave
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = Path(__file__).resolve().parents[1]
W, H, FPS, SECONDS = 1920, 1080, 60, 30
BEAT, BAR = .3125, 1.25
NEAR = Image.Resampling.NEAREST
DARK = (22, 21, 26)
INK = (37, 27, 39)
WHITE = (240, 232, 237)
PINK = (221, 183, 203)
VIOLET = (181, 154, 232)
MUTED = (157, 144, 164)
FONTDIR = Path('/usr/share/fonts/TTF')
LOGO = ROOT / 'ui_mockups/gothic_wordmark_morph/gothic-morph-native.gif'
MODULE_NAMES = ['osc','noise','fm','motor','play','gran','mic','rec','filter','vca','delay','fold','bit','chorus','strn','reverb','ghost','ring','tilt','strain','shift','bird','fract','comp','mouth','arp','glitch']
MODULE_DETAIL = {
    'osc':'morphing oscillator', 'noise':'noise + percussion', 'fm':'frequency modulation',
    'motor':'motor synthesis', 'bird':'whistles + trills', 'arp':'note arpeggiator',
    'play':'varispeed / cuts / reverse', 'gran':'granular sampling',
    'mic':'microphone source', 'rec':'record incoming chain audio',
    'filter':'resonance / drive', 'vca':'envelope / gain / duck', 'delay':'tempo-synced echoes',
    'fold':'wavefolding', 'bit':'bit + sample-rate reduction', 'chorus':'doubling + motion',
    'strn':'live granular processing', 'reverb':'space + freeze', 'ghost':'resonant drift',
    'ring':'ring modulation', 'tilt':'spectral balance', 'strain':'nonlinear feedback',
    'shift':'frequency shifting', 'fract':'live buffer fracture', 'comp':'compression',
    'mouth':'formant shaping', 'glitch':'rhythmic rearrangement',
}
# All current 27 visible modules occur, always on the actual musical grid.
VOICES = [('osc',2),('fm',2),('noise',1),('motor',1),('bird',1),('arp',1)]
SAMPLES = [('play',4),('gran',4),('mic',2),('rec',2)]
FX = [(s,1) for s in ('filter','fold','fract','glitch','ghost','strn','reverb')]
FX += [(s,.5) for s in ('delay','vca','bit','chorus','ring','tilt','strain','shift','comp','mouth')]
SCENES = [
    (0,2,'intro','sound in your hands',False),
    (2,4,'chains','build',False),
    (4,6,'voices','synthesize',False),
    (6,9,'samples','sample',True),
    (9,12,'sequence','sequence',False),
    (12,15,'sigils','draw',False),
    (15,18,'fx','mutate',True),
    (18,21,'perform','perform',False),
    (21,22,'bounce','bounce',True),
    (22,24,'outro','sound in your hands',False),
]
assert set(s for group in (VOICES,SAMPLES,FX) for s,_ in group) == set(MODULE_NAMES)
assert sum(n for _,n in VOICES)==8 and sum(n for _,n in SAMPLES)==12 and sum(n for _,n in FX)==12


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def ease(x):
    x=max(0.,min(1.,x));return 1-(1-x)**3


def blend(a,b,t):
    return tuple(round(x+(y-x)*t) for x,y in zip(a,b))


@lru_cache(maxsize=100)
def font(size,kind='mono'):
    names={'mono':'IBMPlexMono-Regular.ttf','sans':'IBMPlexSansCondensed-Medium.ttf','bold':'IBMPlexSansCondensed-Bold.ttf'}
    return ImageFont.truetype(str(FONTDIR/names[kind]),size)


@lru_cache(maxsize=300)
def text_layer(text,size,color,kind='mono'):
    f=font(size,kind);b=f.getbbox(text)
    im=Image.new('RGBA',(max(1,b[2]-b[0]),max(1,b[3]-b[1])),(0,0,0,0))
    ImageDraw.Draw(im).text((-b[0],-b[1]),text,font=f,fill=color)
    return im


@lru_cache(maxsize=100)
def gothic(text,color=WHITE,scale=5,max_width=790):
    f=ImageFont.truetype(str(ROOT/'assets/fonts/Gossip-MedPixel.otf'),28)
    b=f.getbbox(text);m=Image.new('L',(max(1,b[2]-b[0]),max(1,b[3]-b[1])))
    ImageDraw.Draw(m).text((-b[0],-b[1]),text,font=f,fill=255)
    m=m.point(lambda v:255 if v>=128 else 0)
    scale=min(scale,max(1,max_width//m.width),max(1,132//m.height));m=m.resize((m.width*scale,m.height*scale),NEAR)
    im=Image.new('RGBA',m.size,color);im.putalpha(m);return im


def put(im,layer,x,y,opacity=1.):
    if opacity<1:
        layer=layer.copy();layer.putalpha(layer.getchannel('A').point(lambda v:round(v*max(0,opacity))))
    im.paste(layer,(round(x),round(y)),layer)


def txt(im,text,x,y,size=24,color=WHITE,kind='mono',right=False):
    layer=text_layer(text,size,color,kind);put(im,layer,x-layer.width if right else x,y)
    return layer.width


class NativeVideo:
    def __init__(self,path,w,frames):
        self.path=path;self.w=w;self.stride=w*240*3;self.frames=frames
        self.data=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-an','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','rgb24','pipe:1'])
        assert len(self.data)==self.stride*frames,(str(path),len(self.data),self.stride*frames)
    @lru_cache(maxsize=24)
    def at(self,n):
        n=max(0,min(self.frames-1,int(n)));at=n*self.stride
        return Image.frombytes('RGB',(self.w,240),self.data[at:at+self.stride])


class Morph:
    def __init__(self):
        self.frames=[];self.ends=[];end=0
        with Image.open(LOGO) as gif:
            for n in range(gif.n_frames):
                gif.seek(n);g=gif.convert('L').crop((8,48,392,184))
                # Extract actual contour coverage; retain exact native shapes/AA.
                mask=g.point(lambda v:round(max(0,min(1,(v-36)/208))*255))
                self.frames.append(mask);end+=gif.info.get('duration',100)/1000;self.ends.append(end)
        assert abs(end-48)<.001
    def at(self,seconds,scale,color):
        index=min(len(self.frames)-1,bisect.bisect_right(self.ends,seconds%48))
        mask=self.frames[index].resize((384*scale,136*scale),NEAR)
        im=Image.new('RGBA',mask.size,color);im.putalpha(mask);return im


@lru_cache(maxsize=24)
def background(light,phase):
    # Editorial paper field only, never pasted inside an LCD. No optical strobe.
    im=Image.new('RGB',(160,90));p=im.load()
    a,b=((202,167,187),(228,194,211)) if light else ((15,15,20),(43,36,50))
    for y in range(90):
        for x in range(160):
            v=.44+.13*math.sin(x/32+phase*.5)+.14*math.cos(y/27-x/50)+.11*math.sin((x+y)/50)
            p[x,y]=blend(a,b,max(0,min(1,v)))
    return im.resize((W,H),Image.Resampling.BICUBIC)


class Promo:
    def __init__(self,capture,audio):
        self.capture=capture;self.audio=audio
        self.sync=json.loads((audio/'sync.json').read_text())
        self.frames=self.sync['frames'];assert len(self.frames)==1800
        self.hits=self.sync['kick_hits'];self.hit_times=[e['time'] for e in self.hits]
        self.video={}
        for category,n in [('modules',1080),('workflows',720)]:
            for screen,w in [('top',400),('bottom',320)]:
                self.video[(category,screen)]=NativeVideo(capture/f'{category}-{screen}.mkv',w,n)
        self.logo=Morph()
        with wave.open(str(audio/'seance-tek-192.wav'),'rb') as f:
            assert (f.getnframes(),f.getframerate(),f.getnchannels())==(1440000,48000,2)
            self.pcm=array('h',f.readframes(f.getnframes()))
        self.seen=set()

    def pulse(self,t):
        i=bisect.bisect_right(self.hit_times,t)-1
        return 0 if i<0 else self.hits[i]['strength']*math.exp(-(t-self.hit_times[i])/.095)

    def lcd(self,im,category,frame,screen,x,y,scale=3,pulse=0,enter=1,light=False):
        native=self.video[(category,screen)].at(frame)
        panel=native.resize((native.width*scale,240*scale),NEAR)
        dx=round((1-enter)*70)+(round(pulse*4) if screen=='bottom' else 0)
        x+=dx
        d=ImageDraw.Draw(im);edge=(70,47,69) if light else (73,63,82)
        d.rectangle((x+10,y+12,x+panel.width+10,y+panel.height+12),fill=(18,15,23))
        im.paste(panel,(int(x),int(y)))
        d.rectangle((x-1,y-1,x+panel.width,y+panel.height),outline=edge,width=1)
        # Local impact marks stay outside the native image.
        accent=INK if light else VIOLET
        extent=round(20+20*pulse)
        for xx,sign in ((x-7,-1),(x+panel.width+7,1)):
            d.line((xx,y,xx,y+extent),fill=accent,width=2)
            d.line((xx,y+panel.height-extent,xx,y+panel.height),fill=accent,width=2)

    def detail(self,im,category,frame,box,x,y,scale,light,enter=1):
        native=self.video[(category,'bottom')].at(frame).crop(box)
        panel=native.resize((native.width*scale,native.height*scale),NEAR)
        x+=round((1-enter)*90)
        d=ImageDraw.Draw(im)
        d.rectangle((x+10,y+12,x+panel.width+10,y+panel.height+12),fill=(18,15,23))
        im.paste(panel,(x,y))
        d.rectangle((x-1,y-1,x+panel.width,y+panel.height),outline=(89,65,84) if light else (84,69,97),width=2)

    def chrome(self,im,n,index,light):
        t=n/FPS;p=self.pulse(t);fg=INK if light else WHITE;sub=(91,67,88) if light else MUTED
        txt(im,'seance /',64,45,28,fg,'sans')
        txt(im,'a gestural sound computer',220,50,20,sub)
        txt(im,'nintendo 3ds  /  homebrew',1856,50,20,fg,right=True)
        d=ImageDraw.Draw(im)
        d.line((64,94,1856,94),fill=(170,134,157) if light else (55,46,63),width=1)
        for b in range(24):
            x=64+b*75;active=int(t/BAR)==b;done=t>=(b+1)*BAR
            d.rectangle((x,1024,x+52,1027+(3 if active else 0)),fill=fg if active else (sub if done else (165,134,154) if light else (52,44,61)))
        txt(im,'192 bpm',64,984,18,fg)
        txt(im,f'{index:02d} / 08',1856,984,18,fg,right=True)
        txt(im,'current ui renders / custom soundtrack',958,1043,16,sub,right=True)
        txt(im,'4 chains / 27 modules / 8 sigils',1856,1043,16,sub,right=True)
        # Actual mastered soundtrack energy; editorial, not a fake app meter.
        a=self.frames[n];cx=264
        for j in range(32):
            f=self.frames[max(0,n-31+j)];h=round(3+22*f['low_energy'])
            d.rectangle((cx+j*8,1009-h,cx+j*8+3,1009),fill=fg if j==31 else sub)
        d.rectangle((24,490,29,490+round(70*p)),fill=INK if light else VIOLET)
        d.rectangle((1890,490,1895,490+round(70*p)),fill=INK if light else VIOLET)

    def wave(self,im,t,y,color,height=46):
        # A literal sampled excerpt of this soundtrack, not a decorative sine.
        points=[];start=int(t*48000);N=1440000
        for x in range(0,1792,4):
            k=min(N-1,start+x*7)
            value=(self.pcm[k*2]+self.pcm[k*2+1])/65536
            points.append((64+x,y-round(value*height)))
        ImageDraw.Draw(im).line(points,fill=color,width=2)

    def identity(self,im,n,end=False):
        t=n/FPS;lt=t-27.5 if end else t;p=self.pulse(t);d=ImageDraw.Draw(im)
        # Re-time the CURRENT 48s contour animation, no substituted lettering.
        source=(24+24*min(lt/1.8,1)) if end else 48*min(lt/2.45,1)
        if end and lt>=1.8:source=0
        logo=self.logo.at(source,4,WHITE)
        y=224-round(p*5)
        put(im,logo,(W-logo.width)//2,y)
        txt(im,'sound in your hands.',960-text_layer('sound in your hands.',58,WHITE,'sans').width//2,780,58,WHITE,'sans')
        self.wave(im,t,904,VIOLET,70)
        if end:
            txt(im,'sampler + synth + sequencer',960-text_layer('sampler + synth + sequencer',25,WHITE).width//2,161,25,WHITE)
            labels=('27 modules','4 chains','8 sigils','64 steps')
            for i,label in enumerate(labels):
                x=160+i*432;txt(im,label,x,966,26,WHITE,'sans')
            txt(im,'open-source homebrew for nintendo 3ds',960-text_layer('open-source homebrew for nintendo 3ds',20,MUTED).width//2,1040,20,MUTED)
        else:
            txt(im,'sampler / synthesis / sequencing / performance',960-text_layer('sampler / synthesis / sequencing / performance',24,WHITE).width//2,170,24,WHITE)
            for j,word in enumerate(('draw','sequence','perform','seance')):
                color=WHITE if min(3,int(lt/(BAR/2)))==j else (100,86,113)
                layer=text_layer(word,26,color,'mono');put(im,layer,260+j*380,974)
        txt(im,'seance / 192',64,54,23,MUTED)
        txt(im,'nintendo 3ds',1856,54,23,MUTED,right=True)
        if end and lt>2.3:
            im=Image.blend(im,Image.new('RGB',im.size,DARK),.45*ease((lt-2.3)/.2))
        return im

    def module_at(self,group,beats):
        start=0
        for name,count in group:
            if beats<start+count-1e-7:return name,beats-start,count
            start+=count
        return group[-1][0],group[-1][1]-.001,group[-1][1]

    def module_scene(self,im,n,start,key,light,index):
        t=n/FPS;local=t-start;group=VOICES if key=='voices' else SAMPLES if key=='samples' else FX
        name,age,count=self.module_at(group,local/BEAT);self.seen.add(name)
        color=INK if light else WHITE;sub=(94,66,88) if light else MUTED
        frame=MODULE_NAMES.index(name)*40+min(39,4+int(age*BEAT*32))
        enter=ease(age*BEAT/.1)
        txt(im,{'voices':'synthesis / six sound + note sources','samples':'sampling / from sound to instrument','fx':'processing / seventeen signal shapers'}[key],64,134,29,color,'sans')
        closeup=(key=='voices' and name in ('fm','bird','arp')) or (key=='samples' and name=='gran') or (key=='fx' and [a for a,_ in group].index(name)%2==1)
        layer=gothic(name,color,7,520 if closeup else 780);put(im,layer,64,208+(1-enter)*16)
        txt(im,MODULE_DETAIL[name],66,363,28,color,'sans')
        if closeup:
            self.lcd(im,'modules',frame,'top',112,526,1,pulse=0,enter=1,light=light)
            self.detail(im,'modules',frame,(6,38,314,222),624,183,4,light,enter)
            txt(im,'upper / context',112,797,18,sub)
            txt(im,'instrument detail / native ui',624,936,18,sub)
        else:
            self.lcd(im,'modules',frame,'top',64,425,2,pulse=0,enter=1,light=light)
            self.lcd(im,'modules',frame,'bottom',896,183,3,pulse=self.pulse(t),enter=enter,light=light)
            txt(im,'upper / context',64,936,18,sub)
            txt(im,'lower / instrument',896,936,18,sub)
        txt(im,f'{MODULE_NAMES.index(name)+1:02d} / 27',1856,936,18,color,right=True)
        self.chrome(im,n,index,light)
        return im

    def feature(self,im,n,start,key,light,index):
        t=n/FPS;local=t-start;color=INK if light else WHITE;sub=(94,66,88) if light else MUTED
        enter=ease(local/.16);pulse=self.pulse(t)
        if key=='chains':
            frame=min(79,int(local*32));title='build';kicker='01 / your own signal path'
            line1='4 independent audio chains';line2='6 modules per chain / reorder by touch'
        elif key=='sequence':
            frame=80+min(119,int(local*32));title=('sequence','lock','chance')[min(2,int(local/BAR))];kicker='04 / a different kind of score'
            line1='64 steps per track / pattern banks';line2='locks / chance / retrig / microtiming'
        elif key=='sigils':
            frame=200+min(119,int(local*32));title='draw' if frame<300 else 'connect';kicker='05 / shape the movement'
            line1='8 drawable modulation sources';line2='modules / tempo / swing / sigil rates'
        elif key=='perform':
            if local<2.5:
                frame=340+int(local*32);title='perform';kicker='07 / play it like an instrument'
                line1='live mixer / a-b scene morph';line2='triggers / mute / held-l fader return'
            else:
                frame=450+min(39,int((local-2.5)*32));title='morph';kicker='07 / take it somewhere else'
                line1='blend rhythm + sound';line2='banks / live filter / echo / roll'
        else:
            frame=680+min(39,int(local*32));title='bounce';kicker='08 / make it a sample'
            line1='record the mix or a single chain';line2='listen / save wav / use in a new chain'
        txt(im,kicker,64,134,26,color,'sans')
        title_im=gothic(title,color,6,788);put(im,title_im,64,216+(1-enter)*20)
        txt(im,line1,64,366,30,color,'sans')
        if key=='sigils' and local<2.5:
            self.lcd(im,'workflows',frame,'top',64,538,1,enter=enter,light=light)
            self.detail(im,'workflows',frame,(8,40,312,163),560,420,4,light,enter)
            txt(im,'a / b / c / d / e / f / g / h',560,252,34,VIOLET,'mono')
            txt(im,'your gesture becomes modulation.',560,312,40,WHITE,'sans')
            txt(im,'canvas detail / real stroke',560,935,20,sub)
        else:
            self.lcd(im,'workflows',frame,'top',64,426,2,enter=enter,light=light)
            self.lcd(im,'workflows',frame,'bottom',896,183,3,pulse=pulse,enter=enter,light=light)
        txt(im,line2,64,936,25,color,'sans')
        # Sound-reactive accent connects the real screens without covering controls.
        d=ImageDraw.Draw(im);length=round(110+80*self.frames[n]['energy'])
        d.line((896,934,896+length,934),fill=INK if light else VIOLET,width=3)
        self.chrome(im,n,index,light)
        return im

    def compose(self,n):
        t=n/FPS
        a,b,key,title,light=next(s for s in SCENES if s[0]*BAR<=t<s[1]*BAR)
        im=background(light,int(a)%12).copy()
        if key in ('intro','outro'):return self.identity(im,n,key=='outro')
        index=['chains','voices','samples','sequence','sigils','fx','perform','bounce'].index(key)+1
        if key in ('voices','samples','fx'):return self.module_scene(im,n,a*BAR,key,light,index)
        return self.feature(im,n,a*BAR,key,light,index)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture',type=Path,default=ROOT/'promo/tek-01/capture-final')
    ap.add_argument('--audio',type=Path,default=ROOT/'promo/tek-01/audio')
    ap.add_argument('--output',type=Path,default=ROOT/'promo/tek-01/seance-tek-01.mp4')
    ap.add_argument('--review-only',action='store_true')
    args=ap.parse_args();out=args.output.resolve();out.parent.mkdir(parents=True,exist_ok=True)
    if not args.review_only and any(out.with_suffix(s).exists() for s in ('.mp4','.json','.render.log')):
        ap.error('preserving prior output; choose another basename')
    capture_meta=json.loads((args.capture/'capture.json').read_text())
    assert capture_meta['modules']==27 and capture_meta['native_fps']==32
    started=time.monotonic();p=Promo(args.capture,args.audio)
    review_times=[.4,1.75,3.25,5.3,7.9,9.4,11.8,13.5,15.9,18.1,19.8,21.7,23.5,25.5,26.8,29.6]
    sheet=Image.new('RGB',(1920,4*(270+38)),DARK);sd=ImageDraw.Draw(sheet)
    for i,t in enumerate(review_times):
        frame=p.compose(round(t*FPS));thumb=frame.resize((480,270),Image.Resampling.LANCZOS)
        x,y=(i%4)*480,(i//4)*308;sheet.paste(thumb,(x,y+28));sd.text((x+12,y+5),f'{t:.2f}s',font=font(16),fill=WHITE)
    review=out.with_name(out.stem+'-review.png');sheet.save(review)
    cover=p.compose(round(3.45*FPS));cover.save(out.with_name(out.stem+'-poster.jpg'),quality=94)
    if args.review_only:
        print(review);return
    cmd=['ffmpeg','-nostdin','-n','-v','warning','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(FPS),'-i','pipe:0',
         '-i',str(args.audio/'seance-tek-192.wav'),'-map','0:v:0','-map','1:a:0','-c:v','libx264','-preset','fast','-crf','18',
         '-threads','6','-pix_fmt','yuv420p','-c:a','aac','-b:a','256k','-t','30','-movflags','+faststart',
         '-metadata','title=seance / sound in your hands / 192 bpm',str(out)]
    with out.with_suffix('.render.log').open('xb') as log:
        process=subprocess.Popen(cmd,stdin=subprocess.PIPE,stderr=log)
        try:
            for n in range(1800):
                process.stdin.write(p.compose(n).tobytes())
                if n%300==0:print(f'{n}/1800',flush=True)
            process.stdin.close()
            if process.wait():raise RuntimeError('encode failed: '+str(out.with_suffix('.render.log')))
        finally:
            if process.poll() is None:process.terminate();process.wait()
    assert p.seen==set(MODULE_NAMES),(p.seen,set(MODULE_NAMES)-p.seen)
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_entries',
        'format=duration,size:stream=codec_type,codec_name,width,height,r_frame_rate,nb_read_frames,sample_rate,channels','-of','json',str(out)],text=True))
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert video['nb_read_frames']=='1800' and float(probe['format']['duration'])==30
    assert (video['width'],video['height'],video['r_frame_rate'])==(1920,1080,'60/1')
    paths=[args.capture/f'{kind}-{lcd}.mkv' for kind in ('modules','workflows') for lcd in ('top','bottom')]
    paths += [LOGO,args.audio/'seance-tek-192.wav',args.audio/'sync.json',args.capture/'sources.json']
    out.with_suffix('.json').write_text(json.dumps({
        'output':str(out.relative_to(ROOT)), 'sha256':digest(out), 'probe':probe,
        'bpm':192,'bars':24,'fps':60,'duration':30,'scenes_bars':SCENES,
        'all_visible_modules':MODULE_NAMES,'module_timing_beats':{'voices':VOICES,'samples':SAMPLES,'fx':FX},
        'input_sha256':{str(f.relative_to(ROOT)):digest(f) for f in paths},
        'logo':'current native48s contour GIF, editorial retiming; full four-language pass in intro, German/Spanish/English end',
        'visual_boundary':'fresh current C++ host UI. Native LCD pixels integer-scaled/cropped; software raster is not a PICA capture or device fps claim. Surround and motion are editorial.',
        'audio_boundary':'separately composed hardtek from both user kicks + original synthesis, NOT app/engine output or per-module demos',
        'sync':'cuts on 192bpm beat subdivisions; per-frame impact/energy uses soundtrack sync.json; never replace native app telemetry',
        'generation_credits_spent':0,'render_seconds':round(time.monotonic()-started,2),
    },indent=2)+'\n')
    print(out,flush=True)


if __name__=='__main__':main()
