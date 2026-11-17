#pragma once

#define ROBOT_FACE_THEME_CSS \
    R"THEME(.face-theme-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:16px 0}.face-theme-card{border:1px solid var(--line);border-radius:16px;padding:14px;background:#0b1728}.face-theme-card.active{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}.face-theme-preview{height:86px;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:16px;margin-bottom:10px}.face-theme-eye{width:35px;height:36px;border-radius:9px}.face-theme-preview.soft .face-theme-eye{border-radius:50%}.face-theme-preview.pixel .face-theme-eye{border-radius:0}.face-theme-card strong{display:block}.face-theme-card small{display:block;color:var(--muted);margin:5px 0 10px}.face-theme-card button{width:100%})THEME"

#define ROBOT_FACE_THEME_HTML \
    R"THEME(<article class="card wide"><div class="card-head"><div><h2>Kho theme khuôn mặt</h2><p>Chọn phong cách cho tất cả biểu cảm. Hình bên dưới minh họa màu và dáng mắt.</p></div><span class="tag">FACE THEMES</span></div>)THEME" \
    R"THEME(<div id="faceThemeActive" class="hint">Đang đọc theme hiện tại…</div><div id="faceThemeGrid" class="face-theme-grid"></div>)THEME" \
    R"THEME(<div class="row"><button class="ghost" onclick="faceThemeAction('refresh')">Tải lại danh sách</button><button class="ghost" onclick="downloadFaceTheme()">Tải gói mẫu</button><label class="ghost" for="faceThemeFile">Nhập theme vào SD</label><input id="faceThemeFile" type="file" accept=".json,application/json" onchange="importFaceTheme(this)" style="max-width:220px"></div>)THEME" \
    R"THEME(<p class="mini-note">Bốn theme có sẵn dùng được không cần thẻ. Gói tùy chỉnh lưu ở /robot/themes, tối đa 2 KiB/gói. Theme đã chọn tiếp tục dùng được khi tháo thẻ. TV, nhạc và các màn hình đang mở vẫn giữ ưu tiên.</p><div id="faceThemeStatus" class="statusline" role="status" aria-live="polite"></div></article>)THEME"

#define ROBOT_FACE_THEME_SCRIPT \
    R"THEME(let faceThemeCatalog=[], faceThemePending=false, faceThemePoll=0;)THEME" \
    R"THEME(function renderFaceThemes(d){)THEME" \
    R"THEME( faceThemeCatalog=d.themes||[];)THEME" \
    R"THEME( $('faceThemeActive').textContent='Đang chọn: '+d.active_name;)THEME" \
    R"THEME( const grid=$('faceThemeGrid');grid.replaceChildren();)THEME" \
    R"THEME( for(const t of faceThemeCatalog){)THEME" \
    R"THEME(  const card=document.createElement('div');card.className='face-theme-card'+(t.id===d.active_id?' active':'');)THEME" \
    R"THEME(  const preview=document.createElement('div');preview.className='face-theme-preview '+t.style;preview.style.backgroundColor=t.background;preview.setAttribute('aria-hidden','true');)THEME" \
    R"THEME(  for(let i=0;i<2;i++){const eye=document.createElement('span');eye.className='face-theme-eye';eye.style.backgroundColor=t.primary;preview.append(eye)})THEME" \
    R"THEME(  const title=document.createElement('strong');title.textContent=t.name;)THEME" \
    R"THEME(  const note=document.createElement('small');note.textContent=(t.builtin?'Có sẵn':'Thẻ nhớ')+' · '+t.style;)THEME" \
    R"THEME(  const button=document.createElement('button');button.textContent=t.id===d.active_id?'Đang dùng':'Chọn theme';button.className=t.id===d.active_id?'ghost':'primary';button.disabled=d.busy||faceThemePending;button.onclick=()=>faceThemeAction('select',t.id);)THEME" \
    R"THEME(  card.append(preview,title,note,button);grid.append(card);)THEME" \
    R"THEME( })THEME" \
    R"THEME( const warning=(d.skipped?' · Bỏ qua '+d.skipped+' gói lỗi':'')+(d.truncated?' · Danh sách đã đạt giới hạn':'');)THEME" \
    R"THEME( say('faceThemeStatus',(d.error||d.message||(d.sd_available?'Thẻ nhớ sẵn sàng.':'Đang dùng kho theme có sẵn.'))+warning,!d.error);)THEME" \
    R"THEME(})THEME" \
    R"THEME(async function loadFaceThemes(){try{const d=await json('/api/face_themes');renderFaceThemes(d);return d}catch(e){say('faceThemeStatus',e.message);throw e}})THEME" \
    R"THEME(async function faceThemeAction(action,id='',manifest=''){)THEME" \
    R"THEME( if(faceThemePending)return;faceThemePending=true;let failure='';clearTimeout(faceThemePoll);)THEME" \
    R"THEME( try{let d=await json('/api/face_themes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,id,manifest})});renderFaceThemes(d);)THEME" \
    R"THEME(  const operation=d.operation;)THEME" \
    R"THEME(  for(let i=0;i<60&&d.busy&&d.operation===operation;i++){await new Promise(r=>{faceThemePoll=setTimeout(r,500)});d=await loadFaceThemes()})THEME" \
    R"THEME(  if(d.busy)say('faceThemeStatus','Robot vẫn đang xử lý. Bấm tải lại sau ít giây.');)THEME" \
    R"THEME( }catch(e){failure=e.message}finally{faceThemePending=false;await loadFaceThemes().catch(()=>{});if(failure)say('faceThemeStatus',failure)})THEME" \
    R"THEME(})THEME" \
    R"THEME(async function importFaceTheme(input){const file=input.files[0];if(!file)return;try{if(file.size>2048)throw new Error('Gói theme tối đa 2 KiB.');const text=await file.text();JSON.parse(text);await faceThemeAction('install','',text)}catch(e){say('faceThemeStatus',e.message)}finally{input.value=''}})THEME" \
    R"THEME(function downloadFaceTheme(){const t={version:1,id:'my-theme',name:'My Theme',style:'soft',primary:'#FF8AC6',accent:'#FFD166',tear:'#92E7FF',background:'#140D22'};const url=URL.createObjectURL(new Blob([JSON.stringify(t,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download=t.id+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)})THEME" \
    R"THEME(loadFaceThemes().catch(()=>{});)THEME"
