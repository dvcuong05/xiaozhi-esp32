#pragma once

#include "web/ui/face_theme_gif_ui.h"

#define ROBOT_FACE_THEME_CSS \
    R"THEME(.face-theme-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:16px 0}.face-theme-card{border:1px solid var(--line);border-radius:16px;padding:14px;background:#0b1728}.face-theme-card.active{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}.face-theme-preview{height:86px;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:16px;margin-bottom:10px;overflow:hidden}.face-theme-eye{width:35px;height:36px;border-radius:9px;position:relative}.face-theme-preview.soft .face-theme-eye{border-radius:50%}.face-theme-preview.pixel .face-theme-eye{border-radius:0}.face-theme-card strong{display:block}.face-theme-card small{display:block;color:var(--muted);margin:5px 0 10px}.face-theme-card button{width:100%})THEME" \
    R"THEME(.face-theme-preview.holo .face-theme-eye{overflow:hidden;box-shadow:0 0 16px var(--theme-accent),inset 0 4px 5px #ffffff99,inset 0 -7px 10px #0008;animation:face-float 2s ease-in-out infinite alternate}.face-theme-preview.holo .face-theme-eye:after{content:"";position:absolute;top:5px;bottom:5px;left:-65%;width:30%;background:linear-gradient(90deg,transparent,#fff9,transparent);transform:skewX(-24deg);animation:face-sweep 2.4s linear infinite}.face-theme-preview.orbital .face-theme-eye{box-shadow:0 0 14px var(--theme-accent),inset 0 0 8px #fff8;transform:perspective(40px) rotateX(8deg)}.face-theme-preview.orbital .face-theme-eye:after{content:"";position:absolute;inset:-10px -8px;border:1px solid var(--theme-accent);border-left-color:transparent;border-radius:50%;animation:face-orbit 2.8s linear infinite}.face-theme-preview.plasma .face-theme-eye,.face-theme-preview.demon .face-theme-eye{box-shadow:0 0 18px var(--theme-accent),inset 0 -8px 10px #0008,inset 0 5px 8px #fff9;animation:face-pulse 1.8s ease-in-out infinite}.face-theme-preview.plasma .face-theme-eye:after,.face-theme-preview.demon .face-theme-eye:after{content:"";position:absolute;left:15%;right:15%;bottom:5px;height:4px;border-radius:50%;background:var(--theme-accent);filter:blur(2px)}.face-theme-preview.capybara .face-theme-eye,.face-theme-preview.mochi .face-theme-eye{border-radius:50%;box-shadow:0 0 12px var(--theme-accent),inset 0 3px 6px #fff8}.face-theme-preview.shiba .face-theme-eye{border-radius:45% 45% 30% 30%;box-shadow:0 0 12px var(--theme-accent)})THEME" \
    R"THEME(@keyframes face-sweep{to{left:140%}}@keyframes face-orbit{to{transform:rotate(360deg)}}@keyframes face-pulse{50%{transform:scale(1.08);filter:brightness(1.28)}}@keyframes face-float{to{transform:translateY(-3px)}}@media(prefers-reduced-motion:reduce){.face-theme-eye,.face-theme-eye:after{animation:none!important}})THEME" \
    ROBOT_FACE_THEME_GIF_CSS

#define ROBOT_FACE_THEME_HTML \
    R"THEME(<article class="card wide"><div class="card-head"><div><h2>Kho theme khuôn mặt</h2><p>Chọn phong cách cho tất cả biểu cảm. Hình bên dưới minh họa màu và dáng mắt.</p></div><span class="tag">FACE THEMES</span></div>)THEME" \
    R"THEME(<div id="faceThemeActive" class="hint">Đang đọc theme hiện tại…</div><div id="faceThemeGrid" class="face-theme-grid"></div>)THEME" \
    R"THEME(<div class="row"><button class="ghost" onclick="faceThemeAction('refresh')">Tải lại danh sách</button><button class="ghost" onclick="downloadFaceTheme()">Tải gói mẫu</button><label class="ghost" for="faceThemeFile">Nhập theme vào SD</label><input id="faceThemeFile" type="file" accept=".json,application/json" onchange="importFaceTheme(this)" style="max-width:220px"></div>)THEME" \
    R"THEME(<p class="mini-note">Theme vẽ sẵn dùng được khi không có thẻ. Gói JSON từ <a href="https://myrobot.kynangso.com/my/themes" target="_blank" rel="noopener">Kho theme KyNangSo</a> chỉ lưu kiểu và màu; GIF tùy chỉnh được chuẩn hóa trên trình duyệt rồi lưu trực tiếp vào microSD.</p><div id="faceThemeStatus" class="statusline" role="status" aria-live="polite"></div></article>)THEME" \
    ROBOT_FACE_THEME_GIF_HTML

#define ROBOT_FACE_THEME_SCRIPT \
    R"THEME(let faceThemeCatalog=[], faceThemePending=false, faceThemePoll=0;)THEME" \
    R"THEME(function renderFaceThemes(d){)THEME" \
    R"THEME( faceThemeCatalog=d.themes||[];)THEME" \
    R"THEME( $('faceThemeActive').textContent='Đang chọn: '+d.active_name;)THEME" \
    R"THEME( const grid=$('faceThemeGrid');grid.replaceChildren();)THEME" \
    R"THEME( for(const t of faceThemeCatalog){)THEME" \
    R"THEME(  const card=document.createElement('div');card.className='face-theme-card'+(t.id===d.active_id?' active':'');)THEME" \
    R"THEME(  const preview=document.createElement('div');preview.className='face-theme-preview '+t.style;preview.style.backgroundColor=t.background;preview.style.setProperty('--theme-accent',t.accent);preview.setAttribute('aria-hidden','true');)THEME" \
    R"THEME(  if(t.style==='gif-poc'){preview.textContent='GIF · SD'}else for(let i=0;i<2;i++){const eye=document.createElement('span');eye.className='face-theme-eye';eye.style.backgroundColor=t.primary;preview.append(eye)})THEME" \
    R"THEME(  const title=document.createElement('strong');title.textContent=t.name;)THEME" \
    R"THEME(  const note=document.createElement('small');note.textContent=t.style==='gif-poc'?'GIF cũ · 2 trạng thái':(t.expressions&&t.expressions.length?'GIF · '+t.expressions.length+' biểu cảm':(t.builtin?'Có sẵn':'Thẻ nhớ')+' · '+t.style);)THEME" \
    R"THEME(  const button=document.createElement('button');button.textContent=t.id===d.active_id?'Đang dùng':'Chọn theme';button.className=t.id===d.active_id?'ghost':'primary';button.disabled=d.busy||faceThemePending;button.onclick=()=>faceThemeAction('select',t.id);card.append(preview,title,note,button);)THEME" \
    R"THEME(  if(!t.builtin){const remove=document.createElement('button');remove.className='face-gif-delete';remove.textContent='Xóa theme';remove.disabled=d.busy||faceThemePending||t.id===d.active_id;remove.onclick=()=>{if(confirm('Xóa theme '+t.name+' cùng toàn bộ GIF trên thẻ nhớ?'))faceThemeAction('delete',t.id)};card.append(remove)}grid.append(card);)THEME" \
    R"THEME( })THEME" \
    R"THEME( const warning=(d.skipped?' · Bỏ qua '+d.skipped+' gói lỗi':'')+(d.truncated?' · Danh sách đã đạt giới hạn':'');)THEME" \
    R"THEME( say('faceThemeStatus',(d.error||d.message||(d.sd_available?'Thẻ nhớ sẵn sàng.':'Đang dùng kho theme có sẵn.'))+warning,!d.error);)THEME" \
    R"THEME(})THEME" \
    R"THEME(async function loadFaceThemes(){try{const d=await json('/api/face_themes');renderFaceThemes(d);return d}catch(e){say('faceThemeStatus',e.message);throw e}})THEME" \
    R"THEME(async function faceThemeAction(action,id='',manifest=''){)THEME" \
    R"THEME( if(faceThemePending)return;faceThemePending=true;let failure='',d=null;clearTimeout(faceThemePoll);)THEME" \
    R"THEME( try{d=await json('/api/face_themes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,id,manifest})});renderFaceThemes(d);const operation=d.operation;)THEME" \
    R"THEME(  for(let i=0;i<60&&d.busy&&d.operation===operation;i++){await new Promise(r=>{faceThemePoll=setTimeout(r,500)});d=await loadFaceThemes()}if(d.busy)throw Error('Robot vẫn đang xử lý. Đợi thêm rồi tải lại danh sách.');if(d.error)throw Error(d.error);)THEME" \
    R"THEME( }catch(e){failure=e.message}finally{faceThemePending=false;await loadFaceThemes().catch(()=>{})}if(failure){say('faceThemeStatus',failure);throw Error(failure)}return d)THEME" \
    R"THEME(})THEME" \
    R"THEME(async function importFaceTheme(input){const file=input.files[0];if(!file)return;try{if(file.size>2048)throw Error('Gói theme tối đa 2 KiB.');const text=await file.text();JSON.parse(text);await faceThemeAction('install','',text)}catch(e){say('faceThemeStatus',e.message)}finally{input.value=''}})THEME" \
    R"THEME(function downloadFaceTheme(){const t={version:1,id:'my-theme',name:'My Theme',style:'soft',primary:'#FF8AC6',accent:'#FFD166',tear:'#92E7FF',background:'#140D22'};const url=URL.createObjectURL(new Blob([JSON.stringify(t,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download=t.id+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)})THEME" \
    R"THEME(loadFaceThemes().catch(()=>{});)THEME" \
    ROBOT_FACE_THEME_GIF_SCRIPT
