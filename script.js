/* ═══════════════════════════════════════════════
   CONSTANTS & STATE
═══════════════════════════════════════════════ */
const CLR = {
  'FCFS' :'#3b9eff','SSTF':'#b06eff',
  'SCAN' :'#0ec6b8','C-SCAN':'#f97316','LOOK':'#ef4444'
};
const ALGO_MAP = {
  'FCFS':'fcfs','SSTF':'sstf','SCAN':'scan','C-SCAN':'cscan','LOOK':'look'
};

let state = {
  results:[], config:{},
  active: new Set(['FCFS','SSTF','SCAN','C-SCAN','LOOK']),
  animFrame:null, playing:true,
  stepMode:false, stepAlgo:null, stepIdx:0, stepTimer:null
};

/* ═══════════════════════════════════════════════
   ALGO TOGGLE
═══════════════════════════════════════════════ */
function togAlgo(el){
  const a=el.dataset.a;
  if(state.active.has(a)){state.active.delete(a);el.classList.remove('on')}
  else{state.active.add(a);el.classList.add('on')}
}

/* ═══════════════════════════════════════════════
   RANDOM
═══════════════════════════════════════════════ */
function genRandom(){
  const sz=+document.getElementById('diskSize').value||200;
  const n=6+Math.floor(Math.random()*7);
  const s=new Set();while(s.size<n)s.add(Math.floor(Math.random()*sz));
  document.getElementById('requests').value=[...s].join(',');
  document.getElementById('headPos').value=Math.floor(Math.random()*sz);
  document.querySelector('.hint').textContent='// random requests generated';
}

/* ═══════════════════════════════════════════════
   RUN SIMULATION
═══════════════════════════════════════════════ */
async function runSim(){
  const btn=document.getElementById('runBtn');
  btn.disabled=true;btn.innerHTML='<div class="spin"></div> Simulating...';
  setMsg('// running simulation...');

  const diskSize=+document.getElementById('diskSize').value||200;
  const headRaw=document.getElementById('headPos').value.trim();
  const head=headRaw===''?53:+headRaw;
  const dir=document.getElementById('direction').value;
  const rawReqs=document.getElementById('requests').value
    .split(',').map(x=>+x.trim()).filter(x=>!isNaN(x)&&x>=0&&x<diskSize);

  if(!rawReqs.length){
    alert('No valid requests. Check input.'); btn.disabled=false;
    btn.innerHTML='▶ Run Simulation'; return;
  }

  let results;
  try{
    const resp=await fetch('/api/simulate',{
      method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({
        disk_size:diskSize,head,requests:rawReqs,direction:dir,
        algorithms:[...state.active].map(a=>ALGO_MAP[a])
      })
    });
    const data=await resp.json();
    if(data.status!=='ok') throw new Error(data.message||'Unknown error');
    results=data.results;
    state.config=data.config;
  } catch(_){
    // JS fallback engine
    results=jsEngine(diskSize,head,rawReqs,dir,[...state.active]);
    state.config={disk_size:diskSize,head,requests:rawReqs,direction:dir};
  }

  state.results=results;
  state.playing=true;
  document.getElementById('playBtn').textContent='⏸';

  renderMetrics(results);
  renderCanvas(results,diskSize,head);
  renderSeqs(results);
  renderCmp(results);
  renderTable(results);
  renderHeatmap(results,diskSize);
  populateStepSel(results);
  setMsg(`// done — ${rawReqs.length} req · ${results.length} algo · disk=${diskSize}`);

  btn.disabled=false;btn.innerHTML='▶ Run Simulation';
}

/* ═══════════════════════════════════════════════
   JS FALLBACK ENGINE (runs when Python server absent)
═══════════════════════════════════════════════ */
function jsEngine(sz,head,reqs,dir,algos){
  const out=[];
  if(algos.includes('FCFS'))   out.push(jsFCFS(sz,head,reqs));
  if(algos.includes('SSTF'))   out.push(jsSST(sz,head,reqs));
  if(algos.includes('SCAN'))   out.push(jsSCAN(sz,head,reqs,dir));
  if(algos.includes('C-SCAN')) out.push(jsCSCAN(sz,head,reqs,dir));
  if(algos.includes('LOOK'))   out.push(jsLOOK(sz,head,reqs,dir));

  const ds=out.map(r=>r.total_seek_distance);
  const mn=Math.min(...ds),mx=Math.max(...ds);
  const freq=new Array(20).fill(0);
  const bw=Math.max(1,Math.floor(sz/20));
  reqs.forEach(c=>freq[Math.min(Math.floor(c/bw),19)]++);

  out.sort((a,b)=>a.total_seek_distance-b.total_seek_distance);
  out.forEach((r,i)=>{
    r.rank=i+1;r.is_best=(r.total_seek_distance===mn);
    r.efficiency_pct=mx===mn?100:+((mx-r.total_seek_distance)/(mx-mn)*100).toFixed(1);
    r.normalised_score=mx===mn?0:+((r.total_seek_distance-mn)/(mx-mn)).toFixed(4);
    r.avg_seek_distance=+(r.total_seek_distance/reqs.length).toFixed(2);
    r.throughput=r.total_seek_distance>0?+(reqs.length/r.total_seek_distance).toFixed(8):0;
    const sd=r.steps.map(s=>s.dist).sort((a,b)=>a-b);
    r.p50_seek=sd[Math.floor(sd.length*.5)]||0;
    r.p95_seek=sd[Math.min(Math.floor(sd.length*.95),sd.length-1)]||0;
    r.std_deviation=+stdDev(r.steps.map(s=>s.dist)).toFixed(2);
    r.starvation_count=0;
    r.access_heatmap=freq;r.band_width=bw;
  });
  return out;
}

function stdDev(arr){
  const n=arr.length;if(!n)return 0;
  const m=arr.reduce((a,b)=>a+b,0)/n;
  return Math.sqrt(arr.reduce((s,v)=>s+(v-m)**2,0)/n);
}

function mkSteps(seq){
  return seq.slice(1).map((to,i)=>({from:seq[i],to,dist:Math.abs(to-seq[i]),req_id:-1,aging:0}));
}

function jsFCFS(sz,head,reqs){
  const seq=[head,...reqs];
  const steps=mkSteps(seq);
  const d=steps.reduce((s,x)=>s+x.dist,0);
  return{algorithm:'FCFS',seek_sequence:seq,steps,total_seek_distance:d,seq_length:seq.length};
}
function jsSST(sz,head,reqs){
  const rem=[...reqs],seq=[head],steps=[];let cur=head;
  while(rem.length){
    let bi=rem.reduce((b,v,i)=>Math.abs(v-cur)<Math.abs(rem[b]-cur)?i:b,0);
    const d=Math.abs(rem[bi]-cur);
    steps.push({from:cur,to:rem[bi],dist:d,req_id:bi,aging:0});
    cur=rem[bi];seq.push(cur);rem.splice(bi,1);
  }
  return{algorithm:'SSTF',seek_sequence:seq,steps,total_seek_distance:steps.reduce((s,x)=>s+x.dist,0),seq_length:seq.length};
}
function jsSCAN(sz,head,reqs,dir){
  const sorted=[...reqs].sort((a,b)=>a-b);
  const left=sorted.filter(x=>x<=head),right=sorted.filter(x=>x>head);
  const seq=[head],steps=[];let cur=head;
  const mv=to=>{steps.push({from:cur,to,dist:Math.abs(to-cur),req_id:-1,aging:0});cur=to;seq.push(to);};
  if(dir==='right'){right.forEach(mv);mv(sz-1);[...left].reverse().forEach(mv);}
  else{[...left].reverse().forEach(mv);mv(0);right.forEach(mv);}
  return{algorithm:'SCAN',seek_sequence:seq,steps,total_seek_distance:steps.reduce((s,x)=>s+x.dist,0),seq_length:seq.length};
}
function jsCSCAN(sz,head,reqs,dir){
  const sorted=[...reqs].sort((a,b)=>a-b);
  const left=sorted.filter(x=>x<=head),right=sorted.filter(x=>x>head);
  const seq=[head],steps=[];let cur=head;
  const mv=to=>{steps.push({from:cur,to,dist:Math.abs(to-cur),req_id:-1,aging:0});cur=to;seq.push(to);};
  if(dir==='right'){right.forEach(mv);mv(sz-1);mv(0);left.forEach(mv);}
  else{[...left].reverse().forEach(mv);mv(0);mv(sz-1);[...right].reverse().forEach(mv);}
  return{algorithm:'C-SCAN',seek_sequence:seq,steps,total_seek_distance:steps.reduce((s,x)=>s+x.dist,0),seq_length:seq.length};
}
function jsLOOK(sz,head,reqs,dir){
  const sorted=[...reqs].sort((a,b)=>a-b);
  const left=sorted.filter(x=>x<=head),right=sorted.filter(x=>x>head);
  const seq=[head],steps=[];let cur=head;
  const mv=to=>{steps.push({from:cur,to,dist:Math.abs(to-cur),req_id:-1,aging:0});cur=to;seq.push(to);};
  if(dir==='right'){right.forEach(mv);[...left].reverse().forEach(mv);}
  else{[...left].reverse().forEach(mv);right.forEach(mv);}
  return{algorithm:'LOOK',seek_sequence:seq,steps,total_seek_distance:steps.reduce((s,x)=>s+x.dist,0),seq_length:seq.length};
}

/* ═══════════════════════════════════════════════
   METRICS
═══════════════════════════════════════════════ */
function renderMetrics(results){
  const all=['FCFS','SSTF','SCAN','C-SCAN','LOOK'];
  all.forEach(a=>{
    const r=results.find(x=>x.algorithm===a);
    const mc=document.getElementById('mc-'+a);
    if(r){
      document.getElementById('mv-'+a).textContent=r.total_seek_distance;
      document.getElementById('ms-'+a).textContent=`avg ${(+r.avg_seek_distance).toFixed(1)} · eff ${r.efficiency_pct}%`;
      document.getElementById('mr-'+a).textContent=`#${r.rank}`;
      mc.classList.toggle('best',r.is_best);
      mc.classList.add('lit');
    } else {
      document.getElementById('mv-'+a).textContent='—';
      document.getElementById('ms-'+a).textContent='not selected';
      document.getElementById('mr-'+a).textContent='';
      mc.classList.remove('best','lit');
    }
  });
}

/* ═══════════════════════════════════════════════
   CANVAS VISUALIZATION
═══════════════════════════════════════════════ */
function renderCanvas(results,diskSize,head){
  const canvas=document.getElementById('seekCanvas');
  const dpr=window.devicePixelRatio||1;
  const W=canvas.offsetWidth,H=300;
  canvas.width=W*dpr;canvas.height=H*dpr;
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);

  const P={t:30,r:16,b:36,l:52};
  const pw=W-P.l-P.r,ph=H-P.t-P.b;
  const toY=v=>P.t+ph-(v/diskSize)*ph;
  const maxSt=Math.max(...results.map(r=>r.seq_length));
  const toX=(i,tot)=>P.l+(i/(tot-1||1))*pw;

  // Legend
  const lr=document.getElementById('legend-row');
  lr.innerHTML=results.map(r=>`<div class="leg">
    <div class="leg-line" style="background:${CLR[r.algorithm]}"></div>${r.algorithm}
  </div>`).join('');

  if(state.animFrame) cancelAnimationFrame(state.animFrame);
  let prog=0;
  const spd=+document.getElementById('speed').value;

  function draw(){
    ctx.clearRect(0,0,W,H);
    ctx.fillStyle='#030610';ctx.fillRect(0,0,W,H);

    // Grid
    ctx.strokeStyle='rgba(26,40,64,.8)';ctx.lineWidth=.5;
    for(let i=0;i<=8;i++){
      const y=P.t+(ph*i/8);
      ctx.beginPath();ctx.moveTo(P.l,y);ctx.lineTo(P.l+pw,y);ctx.stroke();
    }
    for(let i=0;i<=6;i++){
      const x=P.l+(pw*i/6);
      ctx.beginPath();ctx.moveTo(x,P.t);ctx.lineTo(x,P.t+ph);ctx.stroke();
    }

    // Y-axis labels
    ctx.fillStyle='#2d4470';ctx.font=`10px 'JetBrains Mono',monospace`;ctx.textAlign='right';
    for(let i=0;i<=5;i++){
      const val=Math.round(diskSize*i/5);
      const y=P.t+ph-(ph*i/5);
      ctx.fillText(val,P.l-7,y+4);
    }
    ctx.save();ctx.translate(13,H/2);ctx.rotate(-Math.PI/2);
    ctx.textAlign='center';ctx.fillText('cylinder #',0,0);ctx.restore();
    ctx.textAlign='center';ctx.fillText('time step →',P.l+pw/2,H-5);

    // Head reference line
    ctx.setLineDash([3,5]);ctx.strokeStyle='rgba(255,255,255,.06)';ctx.lineWidth=1;
    const hy=toY(head);
    ctx.beginPath();ctx.moveTo(P.l,hy);ctx.lineTo(P.l+pw,hy);ctx.stroke();
    ctx.setLineDash([]);

    const curProg=prog;
    results.forEach(r=>{
      const col=CLR[r.algorithm]||'#fff';
      const seq=r.seek_sequence;
      const nSteps=Math.max(2,Math.floor(curProg*(seq.length-1))+1);

      // Glow trail
      ctx.beginPath();
      ctx.strokeStyle=col;ctx.lineWidth=4;ctx.globalAlpha=.07;
      for(let i=0;i<nSteps&&i<seq.length;i++){
        const x=toX(i*(maxSt-1)/(seq.length-1||1),maxSt);
        const y=toY(seq[i]);
        i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
      }
      ctx.stroke();ctx.globalAlpha=1;

      // Main line
      ctx.beginPath();
      ctx.strokeStyle=col;ctx.lineWidth=1.5;
      for(let i=0;i<nSteps&&i<seq.length;i++){
        const x=toX(i*(maxSt-1)/(seq.length-1||1),maxSt);
        const y=toY(seq[i]);
        i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
      }
      ctx.stroke();

      // Dots
      for(let i=0;i<nSteps&&i<seq.length;i++){
        const x=toX(i*(maxSt-1)/(seq.length-1||1),maxSt);
        const y=toY(seq[i]);
        ctx.beginPath();ctx.arc(x,y,i===0?4:2.5,0,Math.PI*2);
        ctx.fillStyle=i===0?'#ffffff':col;ctx.fill();
      }
    });

    if(state.playing&&prog<1){
      prog=Math.min(1,prog+spd*.014);
      state.animFrame=requestAnimationFrame(draw);
    } else if(prog>=1){
      document.getElementById('playBtn').textContent='▶';
      state.playing=false;
    }
  }
  draw();
}

function togglePlay(){
  state.playing=!state.playing;
  document.getElementById('playBtn').textContent=state.playing?'⏸':'▶';
  if(state.playing&&state.results.length){
    renderCanvas(state.results,state.config.disk_size,state.config.head);
  }
}
function replay(){
  state.playing=true;
  document.getElementById('playBtn').textContent='⏸';
  if(state.results.length) renderCanvas(state.results,state.config.disk_size,state.config.head);
}

/* ═══════════════════════════════════════════════
   STEP MODE
═══════════════════════════════════════════════ */
function stepMode(){
  if(!state.results.length){alert('Run simulation first.');return;}
  const sp=document.getElementById('step-player');
  sp.classList.toggle('on');
  if(sp.classList.contains('on')){
    state.stepIdx=0;
    state.stepAlgo=state.results[0]?.algorithm;
    updateStep();
  }
}
function populateStepSel(results){
  const sel=document.getElementById('step-algo-sel');
  sel.innerHTML=results.map(r=>`<option value="${r.algorithm}">${r.algorithm}</option>`).join('');
  sel.onchange=()=>{state.stepAlgo=sel.value;state.stepIdx=0;updateStep()};
}
function updateStep(){
  const r=state.results.find(x=>x.algorithm===state.stepAlgo);
  if(!r) return;
  const steps=r.steps||[];
  const i=state.stepIdx;
  if(!steps.length) return;
  const s=steps[Math.min(i,steps.length-1)];
  const cum=steps.slice(0,i+1).reduce((a,x)=>a+x.dist,0);
  document.getElementById('sv-from').textContent=s.from;
  document.getElementById('sv-to').textContent=s.to;
  document.getElementById('sv-dist').textContent=s.dist;
  document.getElementById('sv-cum').textContent=cum;
  document.getElementById('sv-step').textContent=`${i+1}/${steps.length}`;
  document.getElementById('step-bar').style.width=`${(i+1)/steps.length*100}%`;

  // Draw single step on canvas
  drawStep(r,state.config.disk_size,state.config.head,i);
}
function stepNav(d){
  const r=state.results.find(x=>x.algorithm===state.stepAlgo);
  if(!r) return;
  state.stepIdx=Math.max(0,Math.min(r.steps.length-1,state.stepIdx+d));
  updateStep();
}
let stepAutoTimer=null;
function stepAuto(){
  if(stepAutoTimer){clearInterval(stepAutoTimer);stepAutoTimer=null;document.getElementById('stepAutoBtn').textContent='Auto ▶';return;}
  document.getElementById('stepAutoBtn').textContent='⏹ Stop';
  stepAutoTimer=setInterval(()=>{
    const r=state.results.find(x=>x.algorithm===state.stepAlgo);
    if(!r||state.stepIdx>=r.steps.length-1){clearInterval(stepAutoTimer);stepAutoTimer=null;document.getElementById('stepAutoBtn').textContent='Auto ▶';return;}
    stepNav(1);
  },600);
}
function drawStep(r,diskSize,head,stepIdx){
  const canvas=document.getElementById('seekCanvas');
  const dpr=window.devicePixelRatio||1;
  const W=canvas.offsetWidth,H=300;
  canvas.width=W*dpr;canvas.height=H*dpr;
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);

  const P={t:30,r:16,b:36,l:52};
  const pw=W-P.l-P.r,ph=H-P.t-P.b;
  const toY=v=>P.t+ph-(v/diskSize)*ph;
  const seq=r.seek_sequence;
  const toX=i=>P.l+(i/(seq.length-1||1))*pw;
  const col=CLR[r.algorithm]||'#fff';

  ctx.fillStyle='#030610';ctx.fillRect(0,0,W,H);
  ctx.strokeStyle='rgba(26,40,64,.8)';ctx.lineWidth=.5;
  for(let i=0;i<=8;i++){const y=P.t+(ph*i/8);ctx.beginPath();ctx.moveTo(P.l,y);ctx.lineTo(P.l+pw,y);ctx.stroke();}

  ctx.fillStyle='#2d4470';ctx.font=`10px 'JetBrains Mono',monospace`;ctx.textAlign='right';
  for(let i=0;i<=5;i++){const v=Math.round(diskSize*i/5);const y=P.t+ph-(ph*i/5);ctx.fillText(v,P.l-7,y+4);}

  // Draw full path dim
  ctx.beginPath();ctx.strokeStyle='rgba(255,255,255,.06)';ctx.lineWidth=1;
  for(let i=0;i<seq.length;i++){const x=toX(i);const y=toY(seq[i]);i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
  ctx.stroke();

  // Draw served path bright
  ctx.beginPath();ctx.strokeStyle=col;ctx.lineWidth=2;
  for(let i=0;i<=stepIdx+1&&i<seq.length;i++){const x=toX(i);const y=toY(seq[i]);i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
  ctx.stroke();

  // Active step highlight — clamp index so we never go past the last sequence point
  const activeIdx=Math.min(stepIdx+1,seq.length-1);
  const ax=toX(activeIdx),ay=toY(seq[activeIdx]);
  ctx.beginPath();ctx.arc(ax,ay,6,0,Math.PI*2);
  ctx.strokeStyle=col;ctx.lineWidth=2;ctx.globalAlpha=.4;ctx.stroke();
  ctx.globalAlpha=1;
  ctx.beginPath();ctx.arc(ax,ay,3,0,Math.PI*2);ctx.fillStyle='#fff';ctx.fill();

  // Head
  ctx.beginPath();ctx.arc(toX(0),toY(seq[0]),4,0,Math.PI*2);ctx.fillStyle='#ffffff';ctx.fill();

  // Label
  ctx.fillStyle=col;ctx.font=`bold 11px 'JetBrains Mono',monospace`;ctx.textAlign='left';
  ctx.fillText(`${r.algorithm}  step ${stepIdx+1}/${r.steps?.length||0}`,P.l+6,P.t+14);
}

/* ═══════════════════════════════════════════════
   SEEK SEQUENCES
═══════════════════════════════════════════════ */
function renderSeqs(results){
  document.getElementById('seq-content').innerHTML=`<div class="seq-wrap">${
    results.map(r=>{
      const col=CLR[r.algorithm]||'#fff';
      const tokens=r.seek_sequence.map((v,i)=>{
        if(i===0) return `<span class="seq-head-mark">[${v}]</span>`;
        return `<span class="seq-arr">→</span><span>${v}</span>`;
      }).join('');
      return `<div class="seq-row">
        <div class="seq-lbl">
          <div class="seq-pip" style="background:${col};box-shadow:0 0 5px ${col}"></div>
          <span style="color:${col}">${r.algorithm}</span>
          <span style="font-family:var(--mono);font-size:10px;color:var(--text4);margin-left:6px">// ${r.seek_sequence.length-1} moves · dist ${r.total_seek_distance}</span>
        </div>
        <div class="seq-box">${tokens}</div>
      </div>`;
    }).join('')
  }</div>`;
}

/* ═══════════════════════════════════════════════
   COMPARISON BARS
═══════════════════════════════════════════════ */
function renderCmp(results){
  const mx=Math.max(...results.map(r=>r.total_seek_distance),1);
  document.getElementById('cmp-content').innerHTML=`
    <div style="font-family:var(--mono);font-size:9px;color:var(--text4);margin-bottom:12px;letter-spacing:1px">// TOTAL SEEK DISTANCE  (lower = better)</div>
    <div class="bars">${results.map(r=>{
      const col=CLR[r.algorithm]||'#fff';
      const pct=(r.total_seek_distance/mx*100).toFixed(1);
      return `<div class="bar-r">
        <div class="bar-label" style="color:${col}">${r.algorithm}</div>
        <div class="bar-track">
          <div class="bar-fill" style="width:${pct}%;background:${col}">
            <span class="bar-fill-n">${r.total_seek_distance}</span>
          </div>
        </div>
        <div class="bar-eff">${r.efficiency_pct}%</div>
      </div>`;
    }).join('')}</div>

    <div style="font-family:var(--mono);font-size:9px;color:var(--text4);margin:16px 0 12px;letter-spacing:1px">// AVERAGE SEEK TIME  (lower = better)</div>
    <div class="bars">${(()=>{
      const mxa=Math.max(...results.map(r=>+r.avg_seek_distance),1);
      return results.map(r=>{
        const col=CLR[r.algorithm]||'#fff';
        const pct=(+r.avg_seek_distance/mxa*100).toFixed(1);
        return `<div class="bar-r">
          <div class="bar-label" style="color:${col}">${r.algorithm}</div>
          <div class="bar-track">
            <div class="bar-fill" style="width:${pct}%;background:${col}">
              <span class="bar-fill-n">${(+r.avg_seek_distance).toFixed(1)}</span>
            </div>
          </div>
          <div class="bar-eff">${(+r.std_deviation||0).toFixed(1)} σ</div>
        </div>`;
      }).join('');
    })()}</div>`;
}

/* ═══════════════════════════════════════════════
   DATA TABLE
═══════════════════════════════════════════════ */
function renderTable(results){
  document.getElementById('tbl-content').innerHTML=`<div class="tbl-scroll"><table>
    <thead><tr>
      <th>#</th><th>Algorithm</th><th>Total Seek</th>
      <th>Avg / Req</th><th>Std Dev</th>
      <th>P50 Seek</th><th>P95 Seek</th>
      <th>Throughput</th><th>Efficiency</th>
    </tr></thead>
    <tbody>${results.map(r=>{
      const col=CLR[r.algorithm]||'#fff';
      return `<tr class="${r.is_best?'best-tr':''}">
        <td style="font-family:var(--mono);color:var(--text3)">${r.rank}</td>
        <td>
          <span class="badge" style="color:${col};border-color:${col}40;background:${col}12">${r.algorithm}</span>
          ${r.is_best?'<span class="star-chip">★ BEST</span>':''}
        </td>
        <td style="font-family:var(--mono);color:${r.is_best?'var(--green)':''}">${r.total_seek_distance}</td>
        <td style="font-family:var(--mono)">${(+r.avg_seek_distance).toFixed(2)}</td>
        <td style="font-family:var(--mono)">${(+r.std_deviation||0).toFixed(2)}</td>
        <td style="font-family:var(--mono)">${r.p50_seek??'—'}</td>
        <td style="font-family:var(--mono)">${r.p95_seek??'—'}</td>
        <td style="font-family:var(--mono);font-size:10px">${(+r.throughput).toFixed(6)}</td>
        <td>
          <div style="display:flex;align-items:center;gap:6px">
            <div style="width:60px;height:4px;background:var(--bg4);border-radius:2px;overflow:hidden">
              <div style="width:${r.efficiency_pct}%;height:100%;background:${col};border-radius:2px"></div>
            </div>
            <span style="font-family:var(--mono);font-size:10px">${r.efficiency_pct}%</span>
          </div>
        </td>
      </tr>`;
    }).join('')}</tbody>
  </table></div>`;
}

/* ═══════════════════════════════════════════════
   ACCESS HEATMAP
═══════════════════════════════════════════════ */
function renderHeatmap(results,diskSize){
  const r0=results[0];
  if(!r0||!r0.access_heatmap){document.getElementById('hm-content').innerHTML='<div class="empty"><div class="ei">◌</div>No heatmap data</div>';return;}
  const freq=r0.access_heatmap;
  const bw=r0.band_width||10;
  const mf=Math.max(...freq,1);
  const bands=freq.length;

  document.getElementById('hm-content').innerHTML=`
    <div class="heatmap-wrap">
      <div class="heatmap-title">// cylinder access frequency  (${bands} bands of ~${bw} cylinders)</div>
      <div class="heatmap-grid">${freq.map((f,i)=>{
        const h=Math.max(4,(f/mf)*120);
        const intensity=f/mf;
        const r=Math.round(56+(192-56)*intensity);
        const g=Math.round(189+(52-189)*intensity);
        const b=Math.round(248+(152-248)*intensity);
        return `<div class="hmap-col" style="height:${h}px;background:rgb(${r},${g},${b})">
          <div class="hmap-tt">cyl ${i*bw}–${(i+1)*bw-1}<br>${f} req</div>
        </div>`;
      }).join('')}</div>
      <div class="heatmap-axis">
        <span>0</span><span>${Math.round(diskSize*.25)}</span>
        <span>${Math.round(diskSize*.5)}</span><span>${Math.round(diskSize*.75)}</span>
        <span>${diskSize-1}</span>
      </div>
      <div style="margin-top:12px;font-family:var(--mono);font-size:9px;color:var(--text4)">
        ↑ high access frequency &nbsp;&nbsp; ↓ low &nbsp;&nbsp; Colour: hot (cyan→purple) = more requests in that cylinder range
      </div>
    </div>`;
}

/* ═══════════════════════════════════════════════
   HELPERS
═══════════════════════════════════════════════ */
function setMsg(m){document.getElementById('top-msg').textContent=m}

function swTab(n,btn){
  document.querySelectorAll('.tpane').forEach(p=>p.classList.remove('on'));
  document.querySelectorAll('.tbn').forEach(b=>b.classList.remove('on'));
  document.getElementById('tp-'+n).classList.add('on');
  btn.classList.add('on');
}

function showInfo(n,btn){
  ['fcfs','sstf','scan','cscan','look'].forEach(a=>document.getElementById('info-'+a).style.display='none');
  document.getElementById('info-'+n).style.display='block';
  document.querySelectorAll('#info-tabs .btn').forEach(b=>{b.style.color='';b.style.borderColor='';});
  if(btn){btn.style.color='var(--text1)';btn.style.borderColor='var(--border2)';}
}

function resetAll(){
  if(state.animFrame) cancelAnimationFrame(state.animFrame);
  if(stepAutoTimer){clearInterval(stepAutoTimer);stepAutoTimer=null;}
  state={results:[],config:{},active:new Set(['FCFS','SSTF','SCAN','C-SCAN','LOOK']),
    animFrame:null,playing:true,stepMode:false,stepAlgo:null,stepIdx:0,stepTimer:null};
  const canvas=document.getElementById('seekCanvas');
  const ctx=canvas.getContext('2d');
  ctx.fillStyle='#020509';ctx.fillRect(0,0,canvas.width,canvas.height);
  ['FCFS','SSTF','SCAN','C-SCAN','LOOK'].forEach(a=>{
    document.getElementById('mv-'+a).textContent='—';
    document.getElementById('ms-'+a).textContent='seek distance';
    document.getElementById('mr-'+a).textContent='';
    document.getElementById('mc-'+a).classList.remove('best','lit');
  });
  document.getElementById('legend-row').innerHTML='';
  ['seq-content','cmp-content','tbl-content','hm-content'].forEach(id=>{
    document.getElementById(id).innerHTML='<div class="empty"><div class="ei">◌</div>Run simulation to see results</div>';
  });
  document.getElementById('step-player').classList.remove('on');
  setMsg('// reset — ready');
}

async function exportCSV(){
  if(!state.results.length){alert('Run simulation first.');return;}
  try{
    const r=await fetch('/api/export/csv');
    if(!r.ok) throw new Error();
    const b=await r.blob();
    const a=document.createElement('a');a.href=URL.createObjectURL(b);
    a.download='disk_results.csv';a.click();
  } catch(_){
    // Browser CSV fallback
    const rows=[['Rank','Algorithm','Total Seek','Avg Seek','Std Dev','P50','P95','Throughput','Efficiency%','Best?']];
    state.results.forEach(r=>rows.push([r.rank,r.algorithm,r.total_seek_distance,(+r.avg_seek_distance).toFixed(2),(+r.std_deviation||0).toFixed(2),r.p50_seek||'',r.p95_seek||'',(+r.throughput).toFixed(8),r.efficiency_pct+'%',r.is_best?'YES':'']));
    const csv=rows.map(r=>r.join(',')).join('\n');
    const a=document.createElement('a');a.href='data:text/csv;charset=utf-8,'+encodeURIComponent(csv);
    a.download='disk_results.csv';a.click();
  }
}

/* ═══════════════════════════════════════════════
   INIT
═══════════════════════════════════════════════ */
window.addEventListener('load',()=>{
  const cv=document.getElementById('seekCanvas');
  const ctx=cv.getContext('2d');
  cv.width=cv.offsetWidth;cv.height=420;
  ctx.fillStyle='#020509';ctx.fillRect(0,0,cv.width,420);
  ctx.fillStyle='rgba(46,79,120,.6)';
  ctx.font="11px 'JetBrains Mono',monospace";ctx.textAlign='center';
  ctx.fillText('// configure inputs → click ▶ Run Simulation',cv.offsetWidth/2,148);
  ctx.fillText('// 5 algorithms · real-time animation · step-by-step mode',cv.offsetWidth/2,166);
});
