/* Browser design prototype only: no app, filesystem, audio or project operations. */
(() => {
  'use strict';
  const D = window.CABINET;
  const canvas = document.querySelector('#screen');
  const ctx = canvas.getContext('2d', {alpha: false});
  const buttons = document.querySelector('#targets');
  const status = document.querySelector('#status');
  const reduce = matchMedia('(prefers-reduced-motion: reduce)');
  const ease = v => {v = Math.max(0, Math.min(1, v)); return v*v*(3-2*v);};
  const mix = (a,b,t) => a.map((v,i) => Math.floor(v+(b[i]-v)*t+.5));
  const css = c => `rgb(${c.join(',')})`;
  let theme = 'graphite', selected = 'recent-0', moved = 0;
  let from = Object.fromEntries(D.labels.map(l => [l.id, l.id===selected?1:0]));
  let held = null, pulse = null, release = 0, cancelled = false, releasedId = null, commits = 0;
  const backgrounds = {}, controls = {}, byId = Object.fromEntries(D.labels.map(l => [l.id,l]));
  const now = () => performance.now();
  function weight(id,t) {
    const p = reduce.matches ? 1 : ease((t-moved)/120);
    return from[id]*(1-p)+(id===selected?1:0)*p;
  }
  function choose(id,t=now()) {
    if (!byId[id] || id===selected) return;
    from = Object.fromEntries(D.labels.map(l => [l.id,weight(l.id,t)]));
    moved = t; selected = id;
  }
  function localX(event) {
    return (event.clientX-canvas.getBoundingClientRect().left)*400/canvas.getBoundingClientRect().width-40;
  }
  function inside(event,l) {
    const r=canvas.getBoundingClientRect(), x=(event.clientX-r.left)*400/r.width-40;
    const y=(event.clientY-r.top)*496/r.height-256, b=l.hit;
    return x>=b[0] && x<b[2] && y>=b[1] && y<b[3];
  }
  function begin(id,x,t=now()) {
    choose(id,t); const l=byId[id];
    pulse={id, x:Math.max(l.x,Math.min(l.x+l.w-1,x)), started:t}; release=0;
  }
  function cancel() {
    cancelled=true; held=null; releasedId=null; if(pulse && !release) release=now();
  }
  function commit(id) {
    ++commits;
    status.textContent=`prototype: ${byId[id].action}. files are not created or loaded.`;
    status.dataset.action=id;
  }
  function draw(t) {
    const bg=backgrounds[theme];
    if (!bg || !bg.complete || !bg.naturalWidth) return;
    ctx.imageSmoothingEnabled=false; ctx.drawImage(bg,0,0);
    const colors=D.colors[theme];
    for(const l of D.labels) {
      const f=weight(l.id,t);
      const active=pulse && pulse.id===l.id && !reduce.matches;
      const fade=active ? (release ? 1-ease((t-release)/220) : 1) : 0;
      const age=active ? Math.max(0,t-pulse.started) : 0;
      const origin=active ? pulse.x : l.x;
      const reach=Math.max(origin-l.x,l.x+l.w-origin)+24;
      const front=reach*ease(age/300);
      for(let x=0;x<l.w;++x) {
        const band=active ? Math.max(0,1-Math.abs(Math.abs(l.x+x-origin)-front)/21)*fade : 0;
        ctx.fillStyle=css(mix(colors.ink,colors.accent,Math.floor(f*(1-band)*31+.5)/31));
        for(const [y,h] of l.columns[x]) ctx.fillRect(40+l.x+x,256+l.y+y,1,h);
      }
      // Focus belongs to the word, not to a separate cursor or moving letter.
      if(f>.01) {
        ctx.fillStyle=css(mix(colors.rule,colors.accent,Math.floor(f*31+.5)/31));
        const width=Math.max(1,Math.floor((l.w+4)*f+.5));
        ctx.fillRect(40+l.x,256+l.underline,width,1);
      }
    }
  }
  function tick(t) { draw(t); requestAnimationFrame(tick); }
  for(const l of D.labels) {
    const b=document.createElement('button'); controls[l.id]=b;
    b.type='button'; b.dataset.id=l.id; b.setAttribute('aria-label',l.title);
    b.title=l.title; b.style.left=`${(40+l.hit[0])/4}%`;
    b.style.top=`${(256+l.hit[1])/4.96}%`;
    b.style.width=`${(l.hit[2]-l.hit[0])/4}%`;
    b.style.height=`${(l.hit[3]-l.hit[1])/4.96}%`;
    b.addEventListener('focus',()=>choose(l.id));
    b.addEventListener('pointerenter',()=>{if(!held)choose(l.id);});
    b.addEventListener('pointerdown',e=>{
      if(e.button!==0 || held) return;
      cancelled=false; releasedId=null; held={id:l.id,pointer:e.pointerId};
      begin(l.id,localX(e)); b.focus({preventScroll:true}); b.setPointerCapture(e.pointerId);
    });
    b.addEventListener('click',e=>{
      if(e.detail && (cancelled || releasedId!==l.id)) {releasedId=null;return;}
      releasedId=null;
      if(!e.detail) { begin(l.id,l.x+l.w/2); release=now()+130; }
      commit(l.id);
    });
    buttons.appendChild(b);
  }
  // Ownership is tracked at the document too: a browser may lose capture when
  // a pointer leaves an element. An out-and-back stroke must never become a tap.
  document.addEventListener('pointermove',e=>{
    if(!held || held.pointer!==e.pointerId) return;
    const l=byId[held.id], samples=[e,...(e.getCoalescedEvents?.() || [])];
    if(samples.some(point=>!inside(point,l))) cancelled=true;
  },true);
  document.addEventListener('pointerup',e=>{
    if(!held || held.pointer!==e.pointerId) return;
    const l=byId[held.id], b=controls[l.id];
    if(!inside(e,l)) cancelled=true;
    releasedId=cancelled ? null : held.id;
    held=null; release=now();
    if(b.hasPointerCapture(e.pointerId)) b.releasePointerCapture(e.pointerId);
  },true);
  document.addEventListener('pointercancel',cancel,true);
  document.addEventListener('keydown',e=>{
    // Do not steal theme controls, links, browser shortcuts or editable fields.
    if(e.altKey || e.ctrlKey || e.metaKey || (e.target!==document.body && !buttons.contains(e.target))) return;
    const key=e.key.toLowerCase(), current=byId[selected];
    if(!['arrowup','arrowdown','arrowleft','arrowright','a','b','escape'].includes(key)) return;
    e.preventDefault(); if(held)cancel();
    if(key==='a') {controls[selected].click();return;}
    if(key==='b' || key==='escape') {cancel();status.textContent='prototype: exit cancelled — this is not the running app.';return;}
    const col=current.column, row=current.row;
    let next;
    if(key==='arrowleft') next=D.labels.find(l=>l.column===0 && l.row===Math.min(row,2));
    if(key==='arrowright') next=D.labels.find(l=>l.column===1 && l.row===row);
    if(key==='arrowup' || key==='arrowdown') {
      const list=D.labels.filter(l=>l.column===col);
      const at=list.findIndex(l=>l.id===selected), delta=key==='arrowup'?-1:1;
      next=list[(at+delta+list.length)%list.length];
    }
    if(next) controls[next.id].focus({preventScroll:true});
  });
  document.querySelectorAll('[data-theme]').forEach(b=>b.addEventListener('click',()=>{
    cancel(); theme=b.dataset.theme;
    document.querySelectorAll('[data-theme]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));
  }));
  window.addEventListener('blur',cancel);
  Promise.all(Object.entries(D.backgrounds).map(([key,url])=>new Promise(resolve=>{
    const im=new Image(); backgrounds[key]=im; im.onload=resolve; im.src=url;
  }))).then(()=>{
    window.cabinetReady=true; requestAnimationFrame(tick);
  });
  // Read-only observability for a bounded browser smoke check, not an app API.
  window.cabinetState=()=>({selected,theme,commits,held:!!held,cancelled,reduced:reduce.matches});
})();
