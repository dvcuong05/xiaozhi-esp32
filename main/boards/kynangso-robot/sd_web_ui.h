#ifndef KYNANGSO_SD_WEB_UI_H_
#define KYNANGSO_SD_WEB_UI_H_

#include "web_navigation.h"

namespace kynangso::web_ui {
inline constexpr char kStorageHtml[] = R"SDUI(<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#0a1020">
  <title>Quản lý microSD · KyNangSo</title>
  <style>
    :root{color-scheme:dark;font:14px/1.5 Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#0a1020;color:#edf3ff;--muted:#97a6c2;--line:rgba(154,174,216,.14);--panel:#121c31;--mint:#75dfb1;--cyan:#72d9ed;--amber:#ffcc7a;--red:#ff91a0}
    *{box-sizing:border-box}body{margin:0;min-width:320px;min-height:100vh;background:radial-gradient(ellipse at 80% -10%,rgba(52,111,131,.2),transparent 43%),radial-gradient(ellipse at 0 20%,rgba(95,77,170,.15),transparent 36%),#0a1020}main{max-width:1240px;margin:auto;padding:22px 20px 40px}a{color:inherit;text-decoration:none}button,input,select{font:inherit;color:inherit}
    .storage-page-head{display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:26px}.brand{display:flex;align-items:center;gap:13px}.brand-mark{display:grid;width:44px;height:44px;place-items:center;border:1px solid rgba(117,223,177,.27);border-radius:15px;background:linear-gradient(145deg,rgba(117,223,177,.2),rgba(114,217,237,.08));color:var(--mint);font-size:13px;font-weight:850;letter-spacing:.04em}.eyebrow{color:var(--mint);font-size:10px;font-weight:800;letter-spacing:.15em}.title{margin:2px 0;font-size:26px;font-weight:800;letter-spacing:-.035em}.subtitle{color:var(--muted);font-size:12px}
    .overview{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(270px,.8fr);gap:14px}.panel{min-width:0;margin-top:14px;padding:19px;border:1px solid var(--line);border-radius:19px;background:linear-gradient(145deg,rgba(25,38,62,.9),rgba(15,25,43,.94));box-shadow:0 18px 50px rgba(0,0,0,.16)}.overview .panel{margin-top:0}.panel-head{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;margin-bottom:15px}.panel-head h2{margin:0;font-size:16px;letter-spacing:-.02em}.panel-head p{margin:5px 0 0;color:var(--muted);font-size:11px}.tag{color:var(--mint);font-size:9px;font-weight:800;letter-spacing:.13em;white-space:nowrap}
    .stats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px}.stat{min-width:0;padding:12px;border:1px solid rgba(154,174,216,.12);border-radius:13px;background:rgba(6,14,27,.42)}.stats small{display:block;color:#8f9eb7;font-size:10px}.stats strong{display:block;overflow:hidden;margin-top:4px;color:#edf3ff;font-size:17px;text-overflow:ellipsis;white-space:nowrap}.stats div:first-child strong{color:var(--mint)}#detail{margin:12px 0 0;color:#aab8ce;font-size:11px;line-height:1.6}.status-actions,.toolbar,.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.storage-actions{display:flex;flex-direction:column;justify-content:center}.storage-actions .row{align-items:stretch}.storage-actions button{flex:1}
    button,.button{min-height:37px;padding:8px 12px;border:1px solid rgba(154,174,216,.16);border-radius:11px;background:rgba(48,66,99,.58);color:#e6edfb;cursor:pointer;font-size:11px;font-weight:700;transition:transform .15s ease,background .15s ease,border-color .15s ease;text-decoration:none}button:hover:not(:disabled),.button:hover{transform:translateY(-1px);border-color:rgba(154,174,216,.34);background:rgba(61,82,120,.78)}button:disabled{opacity:.45;cursor:wait;transform:none}button.primary{border-color:transparent;background:linear-gradient(120deg,#4fd4b0,#4ab3db);color:#062019}button.safe{color:#ffbdc6;border-color:rgba(255,145,160,.22);background:rgba(104,38,55,.28)}button.danger{color:#ffabb5}.panel p,.hint,small{color:var(--muted)}
    .browser-head{display:flex;justify-content:space-between;align-items:flex-start;gap:14px}.path{overflow-wrap:anywhere;margin:12px 0;padding:9px 11px;border:1px solid rgba(114,217,237,.16);border-radius:10px;background:rgba(5,14,26,.55);color:#b8e8ee;font:12px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace}.toolbar{margin:12px 0}.toolbar .grow{flex:1}.album-options{display:flex;gap:12px;flex-wrap:wrap;margin:12px 0;padding:12px;border:1px solid rgba(183,161,255,.16);border-radius:13px;background:rgba(116,91,186,.07)}.album-options[hidden]{display:none!important}.album-options label{display:flex;align-items:center;gap:8px;color:#c0b2f2;font-size:11px}.album-options select{min-width:130px}.browser-note{margin:10px 0 0;font-size:11px;line-height:1.6}.media-actions{display:flex;justify-content:flex-end;gap:8px;flex-wrap:wrap;margin:10px 0}
    .table-wrap{overflow:auto;border:1px solid rgba(154,174,216,.11);border-radius:13px}table{width:100%;border-collapse:collapse}th,td{padding:11px 10px;border-bottom:1px solid rgba(154,174,216,.1);text-align:left}th{color:#91a2bd;font-size:10px;font-weight:750}td{color:#dbe5f5;font-size:12px}td:first-child{min-width:180px;overflow-wrap:anywhere}td:last-child{min-width:114px}.name{padding:3px;border:0;background:none;color:#9dddf3;text-align:left;font-size:12px}.name:hover{transform:none;background:transparent;color:#d0f2ff}.table-wrap tr:last-child td{border-bottom:0}.icon-action{display:inline-flex;width:34px;height:34px;align-items:center;justify-content:center;padding:7px;border-radius:10px;vertical-align:middle}.icon-action svg{display:block;width:17px;height:17px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.icon-action svg .filled{fill:currentColor;stroke:none}.icon-action:focus-visible{outline:2px solid var(--cyan);outline-offset:2px}
    #previous,#next{min-width:120px}.pagination{justify-content:space-between;margin-top:13px}.notice{min-height:22px;margin-top:12px;overflow-wrap:anywhere;white-space:pre-wrap;color:#c3d2e7;font-size:11px}.error{color:var(--red)!important}input:not([type=file]),select{max-width:100%;min-height:37px;padding:8px 10px;border:1px solid rgba(154,174,216,.18);border-radius:10px;outline:none;background:#0a1425;color:#edf3ff}input:focus,select:focus{border-color:rgba(114,217,237,.62);box-shadow:0 0 0 3px rgba(114,217,237,.1)}input[type=file]{max-width:100%;margin:9px 0;color:#c2cee1;font-size:11px}progress{width:100%;height:11px;accent-color:var(--mint)}.upload-help{margin:0 0 12px;font-size:11px;line-height:1.7}.upload-path{display:inline-block;margin:3px 0 9px;padding:5px 8px;border:1px solid rgba(114,217,237,.15);border-radius:8px;background:rgba(6,14,27,.42);color:#a9e2ec;font:10px ui-monospace,SFMono-Regular,Consolas,monospace}
    .music-status{display:inline-block;padding:7px 10px;border:1px solid rgba(117,223,177,.16);border-radius:10px;background:rgba(117,223,177,.06);color:#b8e8d2;font-size:11px}.music-shortcuts{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.music-intro{margin:12px 0!important;font-size:11px;line-height:1.65}.music-player{margin-top:14px!important;padding:14px!important;border-color:rgba(114,217,237,.17)!important;background:rgba(5,14,26,.5)!important}.music-player[hidden]{display:none!important}#musicPlayerTitle{font-size:13px}#musicPlayerArtist{margin:4px 0 8px!important;font-size:11px}#musicPlayer progress{display:block;margin:8px 0}#musicPlayerTime{color:#aab8ce;font-size:11px;font-variant-numeric:tabular-nums}
    .music-form{margin-top:14px}.music-fields{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.music-field{display:flex;flex-direction:column;gap:5px;min-width:0;color:#a7b7d0;font-size:10px;font-weight:650}.music-field input,.music-field select{width:100%;min-width:0}.music-query-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:9px;margin-top:10px}.music-query-row button{white-space:nowrap}#musicResults{margin-top:10px;font-size:12px}#musicResults td{vertical-align:top;overflow-wrap:anywhere}#musicResults td:last-child{min-width:0;white-space:nowrap}#musicResults .music-table{min-width:920px;table-layout:fixed}#musicResults .music-table th:nth-child(1){width:19%}#musicResults .music-table th:nth-child(2){width:15%}#musicResults .music-table th:nth-child(3){width:16%}#musicResults .music-table th:nth-child(4){width:15%}#musicResults .music-table th:nth-child(5){width:9%}#musicResults .music-table th:nth-child(6){width:26%}#musicResults .music-edit{width:100%;min-width:100px;padding:7px 8px;font-size:11px}.music-sort{min-height:0;padding:0;border:0;background:none;color:inherit;font:inherit;cursor:pointer}.music-sort:hover:not(:disabled){transform:none;background:none;color:#fff}#musicPagination{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;margin-top:12px}#musicPageInfo{flex:1;color:#9cabc3;text-align:center;font-size:10px}.footer{margin:22px 0 0;color:#7889a3;font-size:10px;line-height:1.6;text-align:center}
    @media(max-width:760px){main{padding:17px 14px 32px}.overview{grid-template-columns:1fr}.storage-actions .row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr))}.music-fields{grid-template-columns:repeat(2,minmax(0,1fr))}.browser-head{flex-direction:column}.panel{padding:16px}}
    @media(max-width:520px){.storage-page-head{align-items:flex-start}.title{font-size:22px}.stats{gap:6px}.stat{padding:9px}.stats strong{font-size:14px}.stats small{font-size:9px}.music-fields{grid-template-columns:1fr}.music-query-row{grid-template-columns:1fr}.music-query-row button{width:100%}.toolbar button{flex:1}.media-actions{justify-content:flex-start}.media-actions button{flex:1}.pagination button{min-width:0;flex:1}td{padding:9px 7px}td:first-child{min-width:145px}}
    @media(prefers-reduced-motion:reduce){*,*::before,*::after{scroll-behavior:auto!important;transition-duration:.01ms!important}}
  )SDUI" ROBOT_WEB_NAV_STYLE
                                       R"SDUI(</style>
</head>
<body>
<main>
  )SDUI" ROBOT_WEB_NAV("", ROBOT_WEB_NAV_CURRENT, "", "", "Quản lý microSD", "storage", "")
                                           R"SDUI(<header class="storage-page-head">
    <div class="brand"><div class="brand-mark" aria-hidden="true">SD</div><div><div class="eyebrow">KYNANGSO · LOCAL STORAGE</div><h1 class="title">Thẻ nhớ microSD</h1><div class="subtitle">Quản lý file, album ảnh và nhạc offline</div></div></div>
  </header>

  <section class="overview" aria-label="Tình trạng thẻ nhớ">
    <article class="panel">
      <div class="panel-head"><div><h2>Dung lượng & trạng thái</h2><p id="detail">Đang đọc thông tin thẻ…</p></div><span class="tag">THIẾT BỊ</span></div>
      <div class="stats"><div class="stat"><small>Trạng thái</small><strong id="state">Đang kiểm tra…</strong></div><div class="stat"><small>Tổng dung lượng</small><strong id="total">—</strong></div><div class="stat"><small>Còn trống</small><strong id="free">—</strong></div></div>
    </article>
    <article class="panel storage-actions">
      <div class="panel-head"><div><h2>Kết nối thẻ</h2><p>Tháo thẻ an toàn trước khi rút nguồn.</p></div><span class="tag">SD CARD</span></div>
      <div class="row"><button data-action onclick="operation('mount')">Kết nối lại</button><button data-action onclick="operation('refresh')">Cập nhật dung lượng</button><button class="safe" data-action onclick="operation('unmount')">Tháo thẻ an toàn</button><button data-action onclick="reload()">Làm mới</button></div>
    </article>
  </section>

  <section class="panel">
    <div class="panel-head browser-head"><div><h2>Duyệt file</h2><p>Mở thư mục để xem, tải lên, phát nhạc hoặc trình chiếu ảnh.</p></div><span class="tag">FILE MANAGER</span></div>
    <div id="path" class="path">/</div>
    <div class="toolbar"><button data-action onclick="openDirectory('/')">Thư mục gốc</button><button data-action onclick="up()">↑ Thư mục cha</button><button data-action onclick="mkdir()">＋ Tạo thư mục</button></div>
    <div class="album-options" id="albumSettings" hidden><label>Thứ tự ảnh<select id="albumMode"><option value="sequential">Tuần tự, không lặp</option><option value="random">Ngẫu nhiên, không lặp</option></select></label><label>Thời gian mỗi ảnh<select id="albumInterval"><option value="5">5 giây</option><option value="10">10 giây</option><option value="15" selected>15 giây</option><option value="20">20 giây</option><option value="30">30 giây</option><option value="45">45 giây</option><option value="60">60 giây</option></select></label><span class="hint">Các tùy chọn áp dụng cho album đang mở.</span></div>
    <div class="media-actions"><button class="safe" onclick="stopPlayback()">Dừng phát nhạc</button><button class="safe" data-action onclick="stopSlideshow()">Dừng trình chiếu</button></div>
    <p class="browser-note">Phát offline hỗ trợ MP3 và Ogg/Opus (.mp3, .ogg, .opus). Ảnh trong <code>/robot/images</code> có thể xem trên TFT hoặc chạy thành album.</p>
    <div class="table-wrap"><table><thead><tr><th>Tên file / thư mục</th><th>Dung lượng</th><th>Thao tác</th></tr></thead><tbody id="files"></tbody></table></div>
    <div class="row pagination"><button id="previous" data-action onclick="page(-1)">← Trang trước</button><button id="next" data-action onclick="page(1)">Trang tiếp →</button></div>
    <div id="message" class="notice" role="status" aria-live="polite"></div>
  </section>

  <section class="panel">
    <div class="panel-head"><div><h2>Tải file lên thẻ</h2><p>Chọn thư mục đích trước khi bắt đầu tải lên.</p></div><span class="tag">UPLOAD</span></div>
    <p class="upload-help">File được tải vào thư mục đang mở. Nhạc chỉ được lập chỉ mục khi tải trực tiếp vào <code>/robot/music</code>. File tối đa 1 GiB; ảnh tối đa 5 MiB đầu vào sẽ được trình duyệt xoay đúng chiều, thu cạnh dài về 480 px và nén thành JPEG tối đa 512 KiB. Tên file và đường dẫn hỗ trợ tiếng Việt.</p>
    <span class="upload-path">Thư mục tải lên: <code id="uploadPathValue">/</code></span><br>
    <input id="input" data-action type="file" multiple aria-label="Chọn file để tải lên">
    <div class="row"><button class="primary" data-action onclick="upload()">Tải file đã chọn</button><button class="safe" id="cancel" onclick="cancelUpload()">Hủy upload đang dở</button></div>
    <progress id="progress" max="100" value="0" aria-label="Tiến trình tải file"></progress>
  </section>

  <section class="panel">
    <div class="panel-head"><div><h2>Thư viện nhạc</h2><p>Tìm, phát và chỉnh metadata nhạc lưu trên thẻ.</p></div><span class="tag">OFFLINE MUSIC</span></div>
    <div id="musicState" class="music-status" role="status">Đang kiểm tra thư viện…</div>
    <p class="music-intro">Nhạc nằm trong <code>/robot/music</code>. Firmware ưu tiên ID3; nếu thiếu, tên file có thể theo mẫu <code>Ca sĩ - Tên bài - Album - Thể loại.mp3</code>. Bạn có thể sửa ca sĩ, album và thể loại rồi lưu metadata trên thẻ.</p>
    <div class="music-shortcuts"><button data-action onclick="openDirectory('/robot/music')">Mở thư mục nhạc</button><button data-action onclick="reindexMusic()">Cập nhật thư viện</button></div>
    <div id="musicPlayer" class="panel music-player" hidden><strong id="musicPlayerTitle">Chưa phát nhạc từ thẻ SD</strong><p id="musicPlayerArtist"></p><progress id="musicPlayerProgress" max="100" value="0"></progress><div class="row"><span id="musicPlayerTime">0:00 / --:--</span><button id="musicNext" type="button" onclick="nextMusicTrack()" disabled>Bài tiếp theo</button></div></div>
    <form id="musicForm" class="music-form">
      <div class="music-fields">
        <label class="music-field">Tên bài / từ khóa<input id="musicQuery" aria-label="Tên bài" placeholder="Tìm theo tên bài" maxlength="96"></label>
        <label class="music-field">Ca sĩ<input id="musicArtist" aria-label="Ca sĩ" placeholder="Lọc theo ca sĩ" maxlength="96"></label>
        <label class="music-field">Album<input id="musicAlbum" aria-label="Album" placeholder="Lọc theo album" maxlength="96"></label>
        <label class="music-field">Thể loại<input id="musicGenre" aria-label="Thể loại" placeholder="Trữ tình, remix, pop…" maxlength="96"></label>
        <label class="music-field">Metadata thiếu<select id="musicMissing" aria-label="Lọc theo metadata bị thiếu"><option value="">Tất cả bài</option><option value="any">Thiếu ít nhất một trường</option><option value="artist">Thiếu ca sĩ</option><option value="album">Thiếu album</option><option value="genre">Thiếu thể loại</option></select></label>
        <label class="music-field">Số bài mỗi trang<select id="musicPageSize" aria-label="Số bài mỗi trang"><option value="25" selected>25 bài</option><option value="50">50 bài</option></select></label>
      </div>
      <div class="music-query-row"><span class="hint">Có thể kết hợp tên bài, ca sĩ, album và thể loại.</span><button class="primary" data-action type="submit">Tìm trong thư viện</button></div>
    </form>
    <p id="musicSummary" role="status" class="music-intro"></p>
    <div id="musicResults"></div>
    <div id="musicPagination" class="row"><button id="musicPrevious" data-action type="button" onclick="musicPage(-1)" disabled>← Trang trước</button><span id="musicPageInfo" aria-live="polite"></span><button id="musicNextPage" data-action type="button" onclick="musicPage(1)" disabled>Trang tiếp →</button></div>
  </section>

  <footer class="footer">Chỉ dùng trong mạng LAN tin cậy. Chờ xác nhận “Đã ngắt kết nối” trước khi tháo thẻ. Firmware không tự format thẻ; thao tác xóa thư mục không thể hoàn tác.</footer>
</main><script>
const $=id=>document.getElementById(id);let current='/',offset=0,hasMore=false,busy=false,cancelled=false,activePath='',serverUpload='',maxFileBytes=1073741824,chunkBytes=1048576;
const sizes=n=>n>=1073741824?(n/1073741824).toFixed(2)+' GiB':n>=1048576?(n/1048576).toFixed(1)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';
const url=(op,path=current,extra={})=>'/api/storage?'+new URLSearchParams({op,path,...extra});
const message=(text,error=false)=>{$('message').textContent=text;$('message').className='notice'+(error?' error':'')};
const storageIcons={play:'<svg viewBox="0 0 24 24" aria-hidden="true"><path class="filled" d="M8 5v14l11-7z"/></svg>',save:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 3h12l4 4v14H3V3zM7 3v6h10V3M7 21v-8h10v8"/></svg>',restore:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 11a9 9 0 1 1 2.6 6.4M3 4v7h7"/></svg>',view:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/></svg>',download:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12m0 0 5-5m-5 5-5-5M5 17v4h14v-4"/></svg>',delete:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M10 11v6m4-6v6M5 7l1 14h12l1-14M9 7V4h6v3"/></svg>',slideshow:'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 4h18v13H3zM8 21h8M12 17v4"/><path class="filled" d="M10 8v6l5-3z"/></svg>'};
function iconButton(icon,label,className=''){const button=document.createElement('button');button.type='button';button.className='icon-action'+(className?' '+className:'');button.title=label;button.setAttribute('aria-label',label);button.innerHTML=storageIcons[icon];return button}
function iconLink(icon,label){const link=document.createElement('a');link.className='button icon-action';link.title=label;link.setAttribute('aria-label',label);link.innerHTML=storageIcons[icon];return link}
function lock(value){busy=value;document.querySelectorAll('[data-action]').forEach(x=>x.disabled=value);$('cancel').disabled=false}
async function api(address,options={}){for(let attempt=0;;attempt++){const r=await fetch(address,options);const data=await r.json();if(r.status===503&&attempt<3){await new Promise(resolve=>setTimeout(resolve,100*(attempt+1)));continue}if(!r.ok)throw Error(data.error||'HTTP '+r.status);return data}}
async function status(){const d=await api(url('status'));const labels={mounted:'Đã kết nối',mounting:'Đang kết nối…',unmounted:'Đã ngắt kết nối',error:'Không đọc được thẻ'};$('state').textContent=labels[d.state]||d.state;$('detail').textContent=d.message;$('total').textContent=d.total_bytes?sizes(d.total_bytes):'—';$('free').textContent=d.space_valid?sizes(d.free_bytes):'Chưa cập nhật';serverUpload=d.upload_path||'';maxFileBytes=d.max_file_bytes||maxFileBytes;chunkBytes=d.chunk_bytes||chunkBytes;const labelsMusic={ready:'Sẵn sàng',indexing:'Đang đọc metadata',dirty:'Chờ cập nhật (dừng phát / hoàn tất upload để tiếp tục)',error:'Lỗi index',unavailable:'Chưa sẵn sàng'};$('musicState').textContent=(labelsMusic[d.music_index_state]||'Chưa sẵn sàng')+' · '+(d.music_tracks||0)+' bài'+(d.music_truncated?' · Vượt giới hạn':'')+' · '+(d.music_index_message||'');return d}
const join=name=>(current==='/'?'':current)+'/'+name;
const imageInputMaxBytes=5*1024*1024,imageOutputMaxBytes=512*1024,imageMaxEdge=480;
const isImageFile=file=>file.type.startsWith('image/')||/\.(jpe?g|png|webp|gif|bmp|avif)$/i.test(file.name);
const canvasJpeg=(canvas,quality)=>new Promise((resolve,reject)=>canvas.toBlob(blob=>blob?resolve(blob):reject(Error('Trình duyệt không tạo được ảnh JPEG.')),'image/jpeg',quality));
async function decodeUploadImage(file){
  if(window.createImageBitmap){try{return await createImageBitmap(file)}catch(_){}}
  const objectUrl=URL.createObjectURL(file);
  try{return await new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=()=>reject(Error('Trình duyệt không đọc được ảnh này. Hãy thử JPEG, PNG hoặc WebP.'));image.src=objectUrl})}
  finally{URL.revokeObjectURL(objectUrl)}
}
async function optimizeImage(file){
  if(!isImageFile(file))return file;
  if(file.size>imageInputMaxBytes)throw Error('Ảnh '+file.name+' vượt giới hạn đầu vào 5 MiB.');
  const source=await decodeUploadImage(file),sourceWidth=source.width||source.naturalWidth,sourceHeight=source.height||source.naturalHeight;
  if(!sourceWidth||!sourceHeight)throw Error('Không xác định được kích thước ảnh '+file.name+'.');
  const canvas=document.createElement('canvas'),context=canvas.getContext('2d');
  if(!context){if(source.close)source.close();throw Error('Trình duyệt không hỗ trợ xử lý ảnh trên thiết bị.')}
  const qualities=[0.88,0.82,0.76,0.70,0.64,0.58];let edge=imageMaxEdge,blob=null;
  try{
    while(edge>=160){
      const scale=Math.min(1,edge/Math.max(sourceWidth,sourceHeight));
      canvas.width=Math.max(1,Math.round(sourceWidth*scale));canvas.height=Math.max(1,Math.round(sourceHeight*scale));
      context.fillStyle='#fff';context.fillRect(0,0,canvas.width,canvas.height);context.drawImage(source,0,0,canvas.width,canvas.height);
      for(const quality of qualities){blob=await canvasJpeg(canvas,quality);if(blob.size<=imageOutputMaxBytes){
        const name=file.name.replace(/\.[^.]*$/,'')+'.jpg';
        return new File([blob],name,{type:'image/jpeg',lastModified:file.lastModified});
      }}
      edge=Math.floor(edge*0.8);
    }
    throw Error('Không thể nén ảnh xuống dưới 512 KiB; hãy chọn ảnh nhỏ hơn.');
  }finally{canvas.width=canvas.height=0;if(source.close)source.close()}
}
async function list(){const d=await api(url('list',current,{offset}));hasMore=d.more;$('path').textContent=current;$('uploadPathValue').textContent=current;$('albumSettings').hidden=current!=='/robot/images';$('files').replaceChildren();for(const item of d.entries){const tr=document.createElement('tr'),name=document.createElement('td'),size=document.createElement('td'),actions=document.createElement('td');const label=document.createElement(item.directory?'button':'span');label.textContent=(item.directory?'📁 ':'')+item.name;if(item.directory){label.className='name';label.onclick=()=>openDirectory(join(item.name))}name.append(label);size.textContent=item.directory?'-':sizes(item.size);if(item.directory&&current==='/robot/images'){const slideshow=iconButton('slideshow','Trình chiếu album');slideshow.onclick=()=>startSlideshow(join(item.name));actions.append(slideshow)}if(!item.directory){if(/\.(mp3|ogg|opus)$/i.test(item.name)){const play=iconButton('play','Phát bài này');play.onclick=()=>playItem(join(item.name));actions.append(play)}if(/\.(jpe?g)$/i.test(item.name)){const view=iconButton('view','Xem ảnh trên TFT');view.onclick=()=>showItem(join(item.name));actions.append(view)}const download=iconLink('download','Tải file về');download.href='/api/storage/download?'+new URLSearchParams({path:join(item.name)});actions.append(download)}const remove=iconButton('delete',item.directory?'Xóa thư mục':'Xóa file','danger');remove.onclick=()=>removeItem(item.name,item.directory);actions.append(remove);tr.append(name,size,actions);$('files').append(tr)}if(!d.entries.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=3;cell.textContent='Thư mục trống';row.append(cell);$('files').append(row)}$('previous').disabled=offset===0;$('next').disabled=!hasMore}
async function showItem(path){try{const d=await api(url('show',path),{method:'POST'});message(d.message||'Đang chuyển ảnh lên TFT.')}catch(e){message(e.message,true)}}
async function startSlideshow(path){const mode=$('albumMode').value,interval_seconds=$('albumInterval').value;try{const d=await api(url('slideshow',path,{mode,interval_seconds}),{method:'POST'});message(d.message||'Đang trình chiếu album.')}catch(e){message(e.message,true)}}
async function stopSlideshow(){try{const d=await api(url('slideshow_stop','/'),{method:'POST'});message(d.message||'Đã dừng trình chiếu album.')}catch(e){message(e.message,true)}}
async function playItem(path){try{const d=await api(url('play',path),{method:'POST'});message(d.message||'Đã gửi yêu cầu phát.');watchStartedMusicPlayer()}catch(e){message(e.message,true)}}
async function stopPlayback(){try{const d=await api(url('stop','/'),{method:'POST'});message(d.message||'Đã gửi yêu cầu dừng.');pollMusicPlayer()}catch(e){message(e.message,true)}}
async function reload(){if(busy)return;try{const d=await status();if(d.state==='mounted')await list();else $('files').replaceChildren();if(d.music_index_state==='ready')await searchMusic();else if(d.state==='mounted'||d.state==='mounting')watchMusicIndex()}catch(e){message(e.message,true)}}
async function openDirectory(path){if(busy)return;const old=current;current=path;offset=0;try{await list()}catch(e){current=old;message(e.message,true)}}
function up(){if(current!=='/')openDirectory(current.substring(0,current.lastIndexOf('/'))||'/')}
async function page(direction){if(busy||direction>0&&!hasMore)return;offset=Math.max(0,offset+direction*24);try{await list()}catch(e){message(e.message,true)}}
async function operation(op){if(busy)return;lock(true);try{await api(url(op),{method:'POST'});let done=false;for(let i=0;i<40;i++){await new Promise(r=>setTimeout(r,500));const d=await status();done=op==='unmount'?d.state==='unmounted':op==='refresh'?d.space_valid:d.state==='mounted';if(done)break;if(i>2&&d.state==='error')throw Error(d.message)}message(done?'Đã hoàn tất.':'Đang xử lý thẻ, hãy bấm Làm mới sau.');}catch(e){message(e.message,true)}finally{lock(false);await reload()}}
async function mkdir(){if(busy)return;const name=prompt('Tên thư mục mới:');if(!name)return;try{await api(url('mkdir',join(name)),{method:'POST'});await reload();message('Đã tạo thư mục.')}catch(e){message(e.message,true)}}
async function removeItem(name,isDirectory){if(busy)return;const question=isDirectory?'Xóa thư mục "'+name+'" cùng toàn bộ file và thư mục bên trong? Thao tác không thể hoàn tác.':'Xóa file "'+name+'" khỏi thẻ nhớ? Thao tác không thể hoàn tác.';if(!confirm(question))return;try{lock(true);await api(url('delete',join(name)),{method:'POST'});message('Đã xóa.')}catch(e){message(e.message,true)}finally{lock(false);await reload()}}
async function cancelUpload(){cancelled=true;if(busy)return;const path=serverUpload||activePath;if(!path){message('Không có upload dang dở trong phiên này.');return}try{await api(url('abort_upload',path),{method:'POST'});activePath='';await reload();message('Đã hủy upload và xóa file tạm.')}catch(e){message(e.message,true)}}
async function upload(){if(busy)return;const files=Array.from($('input').files);if(!files.length){message('Hãy chọn file trước.');return}lock(true);cancelled=false;const uploadDirectory=current;let musicUploaded=false;try{for(const file of files){if(file.size>maxFileBytes&&!isImageFile(file))throw Error('File '+file.name+' vượt giới hạn '+sizes(maxFileBytes)+'.');message(isImageFile(file)?'Đang tối ưu ảnh trên thiết bị của bạn…':'Đang chuẩn bị '+file.name+'…');const uploadFile=await optimizeImage(file);if(uploadFile.size>maxFileBytes)throw Error('File '+uploadFile.name+' vượt giới hạn '+sizes(maxFileBytes)+'.');activePath=(uploadDirectory==='/'?'':uploadDirectory)+'/'+uploadFile.name;if(uploadDirectory==='/robot/music'&&/\.(mp3|ogg|opus)$/i.test(uploadFile.name))musicUploaded=true;let position=0;do{if(cancelled)throw Error('Đã hủy upload.');const body=uploadFile.slice(position,position+chunkBytes);await api('/api/storage/upload?'+new URLSearchParams({path:activePath,offset:position,total:uploadFile.size}),{method:'POST',headers:{'Content-Type':'application/octet-stream'},body});position+=body.size;$('progress').value=uploadFile.size?Math.round(position/uploadFile.size*100):100;message(uploadFile.name+' · '+sizes(position)+' / '+sizes(uploadFile.size)+(uploadFile!==file?' (đã tối ưu từ '+sizes(file.size)+')':''))}while(position<uploadFile.size);activePath=''}message('Đã tải xong '+files.length+' file.');if(musicUploaded)watchMusicIndex();$('input').value=''}catch(e){message(e.message,true);if(activePath){try{await api(url('abort_upload',activePath),{method:'POST'});activePath=''}catch(_){message(e.message+' Hãy dùng Hủy upload khi kết nối ổn định.',true)}}}finally{lock(false);await reload()}}
let musicPoll=null;
function watchMusicIndex(previousGeneration=null){clearTimeout(musicPoll);let attempts=0;const poll=async()=>{try{const d=await status();if(d.music_index_state==='ready'&&(previousGeneration===null||d.music_generation>previousGeneration)){await searchMusic();return}if(d.music_index_state==='error'||d.state==='error'||d.state==='unmounted')return;if(++attempts<120)musicPoll=setTimeout(poll,1000)}catch(e){message(e.message,true)}};musicPoll=setTimeout(poll,500)}
async function reindexMusic(){if(busy)return;try{const previous=await status();await api(url('music_reindex','/'),{method:'POST'});message('Đã yêu cầu cập nhật thư viện.');watchMusicIndex(previous.music_generation)}catch(e){message(e.message,true)}}
let musicPlayerPoll=null,musicPlayerAwaitingStart=false,musicPlayerStartPolls=0;
const formatTime=ms=>{const seconds=Math.floor(Math.max(0,ms)/1000);return Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0')};
function scheduleMusicPlayerPoll(delay=1000){clearTimeout(musicPlayerPoll);musicPlayerPoll=setTimeout(pollMusicPlayer,delay)}
function watchStartedMusicPlayer(){musicPlayerAwaitingStart=true;musicPlayerStartPolls=0;pollMusicPlayer()}
async function pollMusicPlayer(){try{const d=await api(url('music_player','/'));const player=$('musicPlayer'),progress=$('musicPlayerProgress');player.hidden=false;$('musicPlayerTitle').textContent=d.title||'Chưa phát nhạc từ thẻ SD';$('musicPlayerArtist').textContent=d.artist||'';$('musicPlayerTime').textContent=formatTime(d.position_ms)+' / '+(d.duration_ms?formatTime(d.duration_ms):'--:--');if(d.duration_ms){progress.max=100;progress.value=Math.min(100,Math.round(d.position_ms*100/d.duration_ms))}else{progress.max=100;progress.removeAttribute('value')}progress.hidden=!d.duration_ms&&!d.active;$('musicNext').disabled=!d.active;if(d.active){musicPlayerAwaitingStart=false;scheduleMusicPlayerPoll()}else if(musicPlayerAwaitingStart&&++musicPlayerStartPolls<20)scheduleMusicPlayerPoll(500);else musicPlayerAwaitingStart=false}catch(e){scheduleMusicPlayerPoll(3000)}}
async function nextMusicTrack(){const button=$('musicNext');if(button.disabled)return;button.disabled=true;try{const d=await api(url('music_next','/'),{method:'POST'});message(d.message||'Đang chuyển bài.');watchStartedMusicPlayer()}catch(e){message(e.message,true);button.disabled=false}}
let musicOffset=0,musicLimit=25,musicSort='relevance',musicDescending=false,musicLastFilter=null,musicSearchRequest=0;
function musicPage(delta){const next=musicOffset+delta*musicLimit;if(next<0)return;musicOffset=next;searchMusic()}
function musicSortBy(field){if(musicSort===field)musicDescending=!musicDescending;else{musicSort=field;musicDescending=false}musicOffset=0;searchMusic()}
async function searchMusic(){const requestId=++musicSearchRequest;$('musicPrevious').disabled=true;$('musicNextPage').disabled=true;try{
  musicLimit=Number($('musicPageSize').value)||25;
  const filterKey=JSON.stringify([$('musicQuery').value,$('musicArtist').value,$('musicAlbum').value,$('musicGenre').value,$('musicMissing').value]);
  if(musicLastFilter!==null&&filterKey!==musicLastFilter)musicOffset=0;
  musicLastFilter=filterKey;
  const search=()=>api(url('music_search','/',{query:$('musicQuery').value,artist:$('musicArtist').value,album:$('musicAlbum').value,genre:$('musicGenre').value,offset:musicOffset,limit:musicLimit,sort:musicSort,direction:musicDescending?'desc':'asc',missing:$('musicMissing').value}));
  let d=await search();if(requestId!==musicSearchRequest)return;
  if(musicOffset>0&&musicOffset>=d.total){musicOffset=d.total?Math.floor((d.total-1)/musicLimit)*musicLimit:0;d=await search();if(requestId!==musicSearchRequest)return}
  const container=$('musicResults');container.replaceChildren();
  const first=d.total?d.offset+1:0,last=d.offset+d.results.length;
  const sortLabels={relevance:'phù hợp nhất',title:'tên bài',artist:'ca sĩ',album:'album',genre:'thể loại',duration:'thời lượng'};
  $('musicSummary').textContent='Hiển thị '+first+'–'+last+' / '+d.total+' bài · Sắp xếp: '+sortLabels[musicSort]+(musicSort!=='relevance'?(musicDescending?' giảm dần':' tăng dần'):'')+(d.catalog_truncated?' · Thư viện vượt giới hạn 512 bài; tổng chỉ tính mục đã index.':d.total===0?' · Không có bài phù hợp với bộ lọc.':'');
  const pages=Math.ceil(d.total/musicLimit);
  $('musicPageInfo').textContent=pages?'Trang '+(Math.floor(d.offset/musicLimit)+1)+' / '+pages:'Chưa có kết quả';
  $('musicPrevious').disabled=d.offset===0;
  $('musicNextPage').disabled=!d.has_more;
  const table=document.createElement('table');table.className='music-table';
  const head=document.createElement('thead'),headerRow=document.createElement('tr');
  for(const [title,key] of [['Tên bài','title'],['Ca sĩ','artist'],['Album','album'],['Thể loại','genre'],['Thời lượng','duration']]){
    const th=document.createElement('th');th.scope='col';th.setAttribute('aria-sort',musicSort===key?(musicDescending?'descending':'ascending'):'none');
    const sortButton=document.createElement('button');sortButton.type='button';sortButton.className='music-sort';sortButton.textContent=title+(musicSort===key?(musicDescending?' ↓':' ↑'):'');sortButton.onclick=()=>musicSortBy(key);th.append(sortButton);headerRow.append(th)
  }
  const actionHeader=document.createElement('th');actionHeader.scope='col';actionHeader.textContent='Thao tác';headerRow.append(actionHeader);head.append(headerRow);
  const body=document.createElement('tbody');
  for(const track of d.results){
    const row=document.createElement('tr'),titleCell=document.createElement('td');titleCell.textContent=track.title||'—';row.append(titleCell);const editFields={};
    for(const [key,label,value] of [['artist','Ca sĩ',track.artist],['album','Album',track.album],['genre','Thể loại',track.genre]]){
      const cell=document.createElement('td'),input=document.createElement('input');input.type='text';input.className='music-edit';input.maxLength=96;input.setAttribute('aria-label',label+' · '+track.title);input.value=value||'';cell.append(input);row.append(cell);editFields[key]=input
    }
    const durationCell=document.createElement('td');durationCell.textContent=track.duration_ms?formatTime(track.duration_ms):'—';row.append(durationCell);
    const actions=document.createElement('td'),actionButtons=document.createElement('div'),play=iconButton('play','Phát bài này'),save=iconButton('save','Lưu metadata'),reset=iconButton('restore','Khôi phục metadata');actionButtons.className='music-actions';play.onclick=()=>playItem(track.path);save.onclick=()=>saveMusicMetadata(track.path,editFields,save);reset.onclick=()=>resetMusicMetadata(track.path,reset);actionButtons.append(play,save,reset);actions.append(actionButtons);row.append(actions);body.append(row)
  }
  if(!d.results.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=6;cell.textContent='Không có bài hát ở trang này.';row.append(cell);body.append(row)}
  table.append(head,body);const wrapper=document.createElement('div');wrapper.className='table-wrap';wrapper.append(table);container.append(wrapper)
}catch(e){if(requestId!==musicSearchRequest)return;$('musicSummary').textContent=e.message;$('musicPageInfo').textContent='';$('musicPrevious').disabled=true;$('musicNextPage').disabled=true}}async function saveMusicMetadata(path,fields,button){if(button.disabled)return;button.disabled=true;try{const params=new URLSearchParams({op:'music_metadata',path,artist:fields.artist.value,album:fields.album.value,genre:fields.genre.value});const d=await api('/api/storage?'+params,{method:'POST'});message(d.message||'Đã lưu metadata.');await searchMusic()}catch(e){message(e.message,true)}finally{button.disabled=false}}
async function resetMusicMetadata(path,button){if(button.disabled)return;button.disabled=true;try{const before=await status(),params=new URLSearchParams({op:'music_metadata_reset',path});const d=await api('/api/storage?'+params,{method:'POST'});message(d.message||'Đang khôi phục metadata.');watchMusicIndex(before.music_generation)}catch(e){message(e.message,true)}finally{button.disabled=false}}$('musicForm').addEventListener('submit',event=>{event.preventDefault();searchMusic()});
$('musicMissing').addEventListener('change',()=>{musicOffset=0;searchMusic()});
$('musicPageSize').addEventListener('change',()=>{musicOffset=0;searchMusic()});
pollMusicPlayer();
reload();
</script></body></html>)SDUI";
}  // namespace kynangso::web_ui
#endif
