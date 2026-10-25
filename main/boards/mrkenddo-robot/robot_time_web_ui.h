#pragma once

inline constexpr char kRobotTimePage[] = R"TIME(<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#0a1020">
  <title>Nhịp sống · MrKenDDo</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: #edf2ff;
      background: #0a1020;
      font-synthesis: none;
      --muted: #97a6c2;
      --line: rgba(154, 174, 216, .14);
      --panel: rgba(18, 28, 49, .88);
      --panel-light: rgba(25, 38, 64, .82);
      --purple: #b7a1ff;
      --cyan: #72d9ed;
      --amber: #ffcc7a;
      --green: #75dfb1;
      --red: #ff8f9e;
    }
    * { box-sizing: border-box; }
    body {
      min-width: 320px;
      max-width: 1180px;
      margin: 0 auto;
      padding: 24px 22px 42px;
      background: radial-gradient(ellipse at 78% -12%, rgba(95, 77, 170, .22), transparent 40%),
                  radial-gradient(ellipse at 0 25%, rgba(27, 119, 145, .11), transparent 32%), #0a1020;
    }
    a { color: inherit; text-decoration: none; }
    button, input { font: inherit; }
    button { color: inherit; }
    .topbar, .topbar nav, .live-pill, .section-heading, .event-top, .event-actions,
    .button-row, .session-controls, .filters, .days, .clock-heading { display: flex; align-items: center; }
    .topbar { justify-content: space-between; gap: 16px; margin-bottom: 30px; }
    .back-link { display: inline-flex; align-items: center; gap: 9px; color: #bbc8df; font-size: 14px; font-weight: 650; }
    .back-link:hover { color: #fff; }
    .back-mark { display: grid; width: 32px; height: 32px; place-items: center; border: 1px solid var(--line); border-radius: 11px; background: rgba(255,255,255,.04); }
    .live-pill { gap: 8px; padding: 8px 12px; border: 1px solid var(--line); border-radius: 999px; background: rgba(17, 29, 49, .8); color: #b7c5de; font-size: 12px; }
    .live-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--green); box-shadow: 0 0 12px rgba(117,223,177,.62); }
    .live-pill.offline .live-dot { background: var(--amber); box-shadow: 0 0 12px rgba(255,204,122,.45); }
    .page-head { display: grid; grid-template-columns: minmax(0, 1fr) minmax(270px, 350px); align-items: end; gap: 28px; margin-bottom: 24px; }
    .eyebrow { margin: 0 0 9px; color: var(--purple); font-size: 11px; font-weight: 800; letter-spacing: .18em; text-transform: uppercase; }
    h1 { margin: 0; font-size: clamp(32px, 5vw, 48px); line-height: 1.08; letter-spacing: -.045em; }
    .intro { max-width: 600px; margin: 13px 0 0; color: var(--muted); font-size: 15px; line-height: 1.65; }
    .clock-card { padding: 17px 19px; border: 1px solid rgba(136, 154, 197, .2); border-radius: 19px; background: linear-gradient(145deg, rgba(29,42,70,.94), rgba(16,26,45,.93)); box-shadow: 0 18px 50px rgba(0,0,0,.17); }
    .clock-heading { justify-content: space-between; gap: 12px; color: #9dacc8; font-size: 10px; font-weight: 800; letter-spacing: .12em; }
    #clock { margin-top: 8px; font-size: clamp(18px, 2.3vw, 24px); font-weight: 750; font-variant-numeric: tabular-nums; letter-spacing: -.03em; }
    .timezone { display: block; margin-top: 5px; color: #8090ad; font-size: 11px; }
    #msg { min-height: 20px; margin: 0 0 10px; color: var(--green); font-size: 13px; }
    .panel { border: 1px solid var(--line); border-radius: 22px; background: linear-gradient(145deg, var(--panel-light), var(--panel)); box-shadow: 0 20px 55px rgba(0,0,0,.14); }
    #alert { margin-bottom: 18px; padding: 18px 20px; border-color: rgba(255,204,122,.45); background: linear-gradient(110deg, rgba(92,58,27,.9), rgba(35,31,39,.95)); }
    #alert[hidden] { display: none; }
    .alert-content { display: flex; align-items: center; justify-content: space-between; gap: 18px; }
    .alert-copy strong { display: block; margin-bottom: 4px; color: #ffe0a7; font-size: 16px; }
    .alert-copy p { margin: 0; color: #e8c99d; }
    .session { position: relative; overflow: hidden; margin-bottom: 22px; padding: 26px; background: radial-gradient(ellipse at 50% -20%, rgba(132,95,218,.35), transparent 58%), linear-gradient(125deg, #1a2441, #111c31 65%, #102331); }
    .session::after { position: absolute; top: -88px; right: -52px; width: 230px; height: 230px; border: 1px solid rgba(183,161,255,.12); border-radius: 50%; box-shadow: 0 0 0 24px rgba(183,161,255,.025), 0 0 0 48px rgba(183,161,255,.02); content: ""; pointer-events: none; }
    .session-inner { position: relative; z-index: 1; display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 20px; }
    .session-kicker { color: #b9a5ff; font-size: 10px; font-weight: 850; letter-spacing: .17em; }
    #mode { display: inline-flex; align-items: center; gap: 8px; }
    #mode::before { width: 7px; height: 7px; border-radius: 50%; background: currentColor; box-shadow: 0 0 12px currentColor; content: ""; }
    #digits { margin: 8px 0 5px; font-size: clamp(48px, 8vw, 76px); font-weight: 750; line-height: 1; letter-spacing: -.055em; font-variant-numeric: tabular-nums; }
    #task { color: #dbe4f7; font-size: 15px; font-weight: 650; }
    #phase { margin: 7px 0 0; color: #93a4c2; font-size: 12px; }
    .session-controls { flex-wrap: wrap; justify-content: flex-end; gap: 8px; max-width: 340px; }
    .section { padding: 22px; }
    .section-heading { justify-content: space-between; gap: 16px; margin-bottom: 18px; }
    .section-heading h2 { margin: 0; font-size: 18px; letter-spacing: -.02em; }
    .section-heading p { margin: 5px 0 0; color: var(--muted); font-size: 12px; }
    .section-icon { display: grid; width: 38px; height: 38px; flex: 0 0 38px; place-items: center; border: 1px solid var(--line); border-radius: 13px; background: rgba(255,255,255,.04); font-size: 17px; }
    .form-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 14px; margin-bottom: 22px; }
    .form-card { min-width: 0; padding: 20px; }
    .form-card.alarm { --form-accent: var(--amber); }
    .form-card.reminder { --form-accent: var(--cyan); }
    .form-card.pomodoro { --form-accent: var(--purple); }
    .form-card.countdown { --form-accent: #83b7ff; }
    .form-card .section-icon { color: var(--form-accent); }
    .form-card h2 { font-size: 16px; }
    .form-card .section-heading { justify-content: flex-start; margin-bottom: 15px; }
    label.field { display: block; min-width: 0; margin: 12px 0; color: #b8c5dc; font-size: 12px; font-weight: 650; }
    .field input { display: block; width: 100%; min-height: 44px; margin-top: 7px; padding: 10px 12px; border: 1px solid rgba(145,165,207,.2); border-radius: 12px; outline: none; background: rgba(7,14,27,.68); color: #f0f4ff; }
    .field input:focus { border-color: var(--form-accent, var(--purple)); box-shadow: 0 0 0 3px color-mix(in srgb, var(--form-accent, var(--purple)) 17%, transparent); }
    .field input::placeholder { color: #667692; }
    .field-row { display: flex; gap: 12px; }
    .field-row > .field { flex: 1; }
    .days { flex-wrap: wrap; gap: 7px; margin: 13px 0 8px; }
    .day-option { position: relative; cursor: pointer; }
    .day-option input { position: absolute; width: 1px; height: 1px; opacity: 0; }
    .day-option span { display: grid; min-width: 39px; height: 34px; place-items: center; padding: 0 8px; border: 1px solid rgba(145,165,207,.18); border-radius: 10px; background: rgba(7,14,27,.52); color: #9caac4; font-size: 11px; font-weight: 750; transition: .16s ease; }
    .day-option input:checked + span { border-color: color-mix(in srgb, var(--form-accent, var(--purple)) 65%, transparent); background: color-mix(in srgb, var(--form-accent, var(--purple)) 18%, #111a2d); color: var(--form-accent, var(--purple)); }
    .day-option input:focus-visible + span { outline: 2px solid var(--form-accent, var(--purple)); outline-offset: 2px; }
    .hint { display: block; margin: 9px 0 13px; color: #8190aa; font-size: 11px; line-height: 1.55; }
    .button-row { flex-wrap: wrap; gap: 8px; }
    button { min-height: 38px; padding: 9px 13px; border: 1px solid rgba(150,170,212,.18); border-radius: 11px; background: rgba(50,67,103,.65); color: #e5ecfa; font-size: 12px; font-weight: 700; cursor: pointer; transition: transform .15s ease, background .15s ease, border-color .15s ease; }
    button:hover:not(:disabled) { transform: translateY(-1px); border-color: rgba(174,192,232,.38); background: rgba(66,86,129,.82); }
    button:focus-visible { outline: 2px solid #b7a1ff; outline-offset: 2px; }
    button:disabled { opacity: .45; cursor: wait; }
    button.primary { border-color: transparent; background: linear-gradient(120deg, #9675f1, #7c67dd); color: #fff; box-shadow: 0 6px 18px rgba(117,88,213,.22); }
    button.primary:hover:not(:disabled) { background: linear-gradient(120deg, #a385ff, #8b75ec); }
    button.subtle { background: rgba(255,255,255,.035); }
    button.danger { color: #ffb1ba; }
    button.compact { min-height: 34px; padding: 7px 10px; font-size: 11px; }
    .schedules { padding: 22px; }
    .schedule-head { align-items: flex-start; margin-bottom: 16px; }
    .schedule-title { display: flex; align-items: center; gap: 12px; }
    .schedule-title .section-icon { color: var(--green); }
    .schedule-count { padding: 5px 9px; border: 1px solid var(--line); border-radius: 999px; color: #a8b7d1; font-size: 10px; font-weight: 750; }
    .filters { flex-wrap: wrap; gap: 7px; margin: 0 0 14px; }
    .filter { min-height: 32px; padding: 6px 11px; border-radius: 999px; color: #9fadc5; font-size: 11px; }
    .filter[aria-pressed="true"] { border-color: rgba(183,161,255,.42); background: rgba(145,116,232,.16); color: #d5c9ff; }
    #list { display: grid; gap: 10px; }
    .event-card { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 12px 18px; padding: 16px; border: 1px solid rgba(145,165,207,.13); border-radius: 16px; background: rgba(9,16,30,.43); transition: border-color .16s ease, background .16s ease; }
    .event-card:hover { border-color: rgba(145,165,207,.27); background: rgba(14,23,41,.74); }
    .event-card.next { border-color: rgba(117,223,177,.34); background: linear-gradient(100deg, rgba(35,80,73,.18), rgba(9,16,30,.48)); }
    .event-card.disabled { opacity: .63; }
    .event-top { min-width: 0; align-items: flex-start; gap: 12px; }
    .event-icon { display: grid; width: 38px; height: 38px; flex: 0 0 38px; place-items: center; border-radius: 12px; background: rgba(255,204,122,.12); color: var(--amber); font-size: 17px; }
    .event-card.reminder .event-icon { background: rgba(114,217,237,.12); color: var(--cyan); }
    .event-main { min-width: 0; }
    .event-title-line { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; }
    .event-title { overflow-wrap: anywhere; color: #eef3ff; font-size: 14px; font-weight: 760; }
    .event-kind { color: #8d9bb5; font-size: 10px; }
    .event-date { margin-top: 9px; color: #9dabc5; font-size: 11px; }
    .event-time { margin-top: 5px; color: #f0f4ff; font-size: 23px; font-weight: 780; letter-spacing: -.04em; font-variant-numeric: tabular-nums; }
    .event-meta { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 10px; }
    .chip { display: inline-flex; align-items: center; min-height: 23px; padding: 4px 8px; border: 1px solid rgba(145,165,207,.12); border-radius: 999px; background: rgba(255,255,255,.035); color: #a9b6ce; font-size: 10px; }
    .chip.status-on { border-color: rgba(117,223,177,.2); background: rgba(117,223,177,.08); color: #8ce4bd; }
    .chip.status-off { border-color: rgba(255,204,122,.17); background: rgba(255,204,122,.06); color: #edc67e; }
    .chip.next-chip { border-color: rgba(117,223,177,.3); color: #9aebc8; }
    .event-actions { flex-wrap: wrap; justify-content: flex-end; align-content: center; gap: 6px; }
    .empty-state { padding: 34px 18px; border: 1px dashed rgba(145,165,207,.2); border-radius: 15px; color: #9aa8c1; text-align: center; }
    .empty-icon { display: block; margin-bottom: 9px; color: #a897f0; font-size: 24px; }
    footer { padding: 18px 4px 0; color: #70809c; font-size: 11px; text-align: center; }
    @media (max-width: 760px) {
      body { padding: 18px 14px 30px; }
      .page-head { grid-template-columns: 1fr; gap: 17px; }
      .clock-card { max-width: none; }
      .session-inner { grid-template-columns: 1fr; }
      .session-controls { justify-content: flex-start; max-width: none; }
      .form-grid { gap: 11px; }
    }
    @media (max-width: 560px) {
      .topbar { margin-bottom: 24px; }
      .live-pill { max-width: 52%; font-size: 10px; }
      .form-grid { grid-template-columns: 1fr; }
      .section, .schedules, .session { padding: 18px; }
      .event-card { grid-template-columns: 1fr; }
      .event-actions { justify-content: flex-start; padding-left: 50px; }
      .alert-content { align-items: flex-start; flex-direction: column; }
      .alert-content .button-row { width: 100%; }
      .alert-content button { flex: 1; }
      .field-row { gap: 8px; }
      .schedule-head { align-items: flex-start; flex-direction: column; }
    }
    @media (prefers-reduced-motion: reduce) { *, *::before, *::after { scroll-behavior: auto !important; transition-duration: .01ms !important; } }
  </style>
</head>
<body>
  <nav class="topbar" aria-label="Điều hướng">
    <a class="back-link" href="/"><span class="back-mark" aria-hidden="true">←</span> Bảng điều khiển</a>
    <div id="connectionPill" class="live-pill"><span class="live-dot" aria-hidden="true"></span><span id="connection">Đang kết nối robot…</span></div>
  </nav>

  <header class="page-head">
    <div>
      <p class="eyebrow">MRKENDDO · DAILY COMPANION</p>
      <h1>Nhịp sống của bạn</h1>
      <p class="intro">Báo thức, nhắc việc và phiên tập trung được gom vào một nơi. Lên lịch nhanh, xem việc sắp tới và quản lý ngay trên robot.</p>
    </div>
    <div class="clock-card">
      <div class="clock-heading"><span>GIỜ TRÊN ROBOT</span><span id="clockState">ĐANG ĐỒNG BỘ</span></div>
      <div id="clock">Đang kết nối…</div>
      <small class="timezone">Múi giờ thiết bị · Asia/Ho_Chi_Minh (UTC+7)</small>
    </div>
  </header>

  <p id="msg" role="status" aria-live="polite"></p>

  <section id="alert" class="panel" hidden aria-live="assertive">
    <div class="alert-content">
      <div class="alert-copy"><strong>Đến giờ rồi</strong><p id="alertText"></p></div>
      <div class="button-row"><button class="primary" data-op="dismiss">Đã xong</button><button class="subtle" data-op="snooze">Nhắc lại sau 5 phút</button></div>
    </div>
  </section>

  <section class="panel session" aria-label="Phiên hiện tại">
    <div class="session-inner">
      <div>
        <div class="session-kicker"><span id="mode">SẴN SÀNG</span></div>
        <div id="digits">00:00</div>
        <div id="task">Chọn một việc để bắt đầu</div>
        <p id="phase"></p>
      </div>
      <div class="session-controls">
        <button class="subtle" data-op="pause">Tạm dừng</button>
        <button class="subtle" data-op="resume">Tiếp tục</button>
        <button class="subtle" data-op="stop">Kết thúc</button>
        <button class="primary" id="visibility" data-op="hide">Ẩn trên TFT</button>
      </div>
    </div>
  </section>

  <div class="form-grid">
    <section class="panel form-card alarm">
      <div class="section-heading"><span class="section-icon" aria-hidden="true">◷</span><div><h2>Báo thức</h2><p>Đặt giờ thức dậy hoặc nhắc theo tuần.</p></div></div>
      <form id="alarmForm">
        <label class="field">Tên báo thức<input name="title" maxlength="60" placeholder="Chào buổi sáng"></label>
        <label class="field">Giờ Việt Nam<input name="time" type="time" value="07:00" required></label>
        <div class="days" id="days" aria-label="Ngày lặp báo thức"></div>
        <small class="hint">Không chọn ngày để báo một lần vào giờ kế tiếp.</small>
        <button class="primary" type="submit">Tạo báo thức</button>
      </form>
    </section>

    <section class="panel form-card reminder">
      <div class="section-heading"><span class="section-icon" aria-hidden="true">✓</span><div><h2>Nhắc việc</h2><p>Ghi nhớ việc cần làm vào đúng thời điểm.</p></div></div>
      <form id="reminderForm">
        <label class="field">Việc cần nhớ<input name="title" maxlength="60" placeholder="Uống nước, gọi điện…" required></label>
        <label class="field">Ngày giờ bắt đầu · Việt Nam<input name="at" type="datetime-local" required></label>
        <div class="days" id="reminderDays" aria-label="Ngày lặp nhắc việc"></div>
        <small class="hint">Không chọn ngày để nhắc một lần. Chọn ngày để lặp hằng tuần.</small>
        <button class="primary" type="submit">Tạo nhắc việc</button>
      </form>
    </section>

    <section class="panel form-card pomodoro">
      <div class="section-heading"><span class="section-icon" aria-hidden="true">◎</span><div><h2>Pomodoro</h2><p>Chia công việc thành các phiên tập trung.</p></div></div>
      <form id="pomoForm">
        <label class="field">Việc cần tập trung<input name="title" maxlength="60" placeholder="Hoàn thành công việc"></label>
        <div class="field-row">
          <label class="field">Tập trung · phút<input name="focus_minutes" type="number" min="1" max="180" value="25" required></label>
          <label class="field">Nghỉ ngắn · phút<input name="break_minutes" type="number" min="1" max="60" value="5" required></label>
        </div>
        <div class="field-row">
          <label class="field">Nghỉ dài · phút<input name="long_break_minutes" type="number" min="1" max="120" value="15" required></label>
          <label class="field">Số vòng<input name="rounds" type="number" min="1" max="12" value="4" required></label>
        </div>
        <small class="hint">Bắt đầu phiên tập trung mới theo cấu hình trên.</small>
        <button class="primary" type="submit">Bắt đầu tập trung</button>
      </form>
    </section>

    <section class="panel form-card countdown">
      <div class="section-heading"><span class="section-icon" aria-hidden="true">⌛</span><div><h2>Đếm ngược</h2><p>Hẹn giờ nhanh cho một công việc ngắn.</p></div></div>
      <form id="timerForm">
        <label class="field">Nội dung<input name="title" maxlength="60" placeholder="Pha trà, nghỉ giải lao…"></label>
        <div class="field-row">
          <label class="field">Phút<input name="minutes" type="number" min="0" max="1440" value="5" required></label>
          <label class="field">Giây<input name="seconds" type="number" min="0" max="59" value="0" required></label>
        </div>
        <small class="hint">Đếm ngược tạm dừng khi robot mất nguồn; lịch vẫn được lưu riêng.</small>
        <button class="primary" type="submit">Bắt đầu đếm</button>
      </form>
    </section>
  </div>

  <section class="panel schedules">
    <div class="section-heading schedule-head">
      <div class="schedule-title"><span class="section-icon" aria-hidden="true">▦</span><div><h2>Lịch của bạn</h2><p>Quản lý tối đa 8 báo thức và nhắc việc.</p></div></div>
      <span class="schedule-count" id="scheduleCount">0 / 8 lịch</span>
    </div>
    <div class="filters" role="group" aria-label="Lọc lịch">
      <button class="filter" type="button" data-filter="all" aria-pressed="true">Tất cả</button>
      <button class="filter" type="button" data-filter="alarm" aria-pressed="false">Báo thức</button>
      <button class="filter" type="button" data-filter="reminder" aria-pressed="false">Nhắc việc</button>
      <button class="filter" type="button" data-filter="disabled" aria-pressed="false">Đã tắt</button>
    </div>
    <div id="list" aria-live="polite"></div>
  </section>

  <footer>Giờ robot theo múi giờ Việt Nam · Chạm robot để xác nhận báo thức · Âm báo dùng âm lượng loa hiện tại</footer>

  <script>
    const $ = selector => document.querySelector(selector);
    const dayNames = ['CN', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7'];
    let busy = false;
    let polling = false;
    let currentFilter = 'all';
    let latestState = null;

    function addWeekdays(selector) {
      dayNames.forEach((name, index) => {
        const label = document.createElement('label');
        const input = document.createElement('input');
        const text = document.createElement('span');
        label.className = 'day-option';
        input.type = 'checkbox';
        input.value = 1 << index;
        text.textContent = name;
        label.append(input, text);
        $(selector).append(label);
      });
    }
    addWeekdays('#days');
    addWeekdays('#reminderDays');

    function setText(selector, value) { $(selector).textContent = value; }
    function makeChip(text, className) {
      const chip = document.createElement('span');
      chip.className = 'chip' + (className ? ' ' + className : '');
      chip.textContent = text;
      return chip;
    }
    function sendAction(button, data, confirmText) {
      button.onclick = () => {
        if (!confirmText || confirm(confirmText)) send(data);
      };
    }
    function formatTime(timestamp) {
      return new Intl.DateTimeFormat('vi-VN', {
        timeZone: 'Asia/Ho_Chi_Minh', hour: '2-digit', minute: '2-digit', hour12: false
      }).format(new Date(timestamp * 1000));
    }
    function formatDate(timestamp) {
      return new Intl.DateTimeFormat('vi-VN', {
        timeZone: 'Asia/Ho_Chi_Minh', weekday: 'long', day: '2-digit', month: 'long', year: 'numeric'
      }).format(new Date(timestamp * 1000));
    }
    function formatDays(mask) {
      return dayNames.filter((_, index) => Number(mask) & (1 << index)).join(' · ');
    }
    function renderSchedule(entries) {
      const list = $('#list');
      list.replaceChildren();
      const enabled = entries.filter(entry => entry.enabled);
      const next = enabled.reduce((best, entry) => !best || Number(entry.due) < Number(best.due) ? entry : best, null);
      const visible = entries.filter(entry => {
        if (currentFilter === 'alarm' || currentFilter === 'reminder') return entry.kind === currentFilter;
        if (currentFilter === 'disabled') return !entry.enabled;
        return true;
      });
      document.querySelectorAll('[data-filter]').forEach(button => {
        button.setAttribute('aria-pressed', String(button.dataset.filter === currentFilter));
      });

      if (!visible.length) {
        const empty = document.createElement('div');
        empty.className = 'empty-state';
        const icon = document.createElement('span');
        icon.className = 'empty-icon';
        icon.textContent = currentFilter === 'all' ? '✦' : '⌕';
        const message = document.createElement('div');
        message.textContent = currentFilter === 'all' ? 'Chưa có lịch nào. Tạo báo thức hoặc nhắc việc ở phía trên nhé.' : 'Không có mục nào trong bộ lọc này.';
        empty.append(icon, message);
        list.append(empty);
        return;
      }

      visible.forEach(entry => {
        const recurring = Number(entry.days) !== 0;
        const isNext = Boolean(next && Number(next.id) === Number(entry.id));
        const card = document.createElement('article');
        const left = document.createElement('div');
        const top = document.createElement('div');
        const icon = document.createElement('span');
        const main = document.createElement('div');
        const titleLine = document.createElement('div');
        const title = document.createElement('strong');
        const kind = document.createElement('span');
        const date = document.createElement('div');
        const time = document.createElement('div');
        const meta = document.createElement('div');
        const actions = document.createElement('div');
        const toggle = document.createElement('button');
        const remove = document.createElement('button');
        const timestamp = Number(entry.due) || 0;

        card.className = 'event-card ' + (entry.kind === 'alarm' ? 'alarm' : 'reminder') + (entry.enabled ? '' : ' disabled') + (isNext ? ' next' : '');
        top.className = 'event-top';
        icon.className = 'event-icon';
        icon.setAttribute('aria-hidden', 'true');
        icon.textContent = entry.kind === 'alarm' ? '◷' : '✓';
        main.className = 'event-main';
        titleLine.className = 'event-title-line';
        title.className = 'event-title';
        title.textContent = entry.title;
        kind.className = 'event-kind';
        kind.textContent = entry.kind === 'alarm' ? 'BÁO THỨC' : 'NHẮC VIỆC';
        date.className = 'event-date';
        date.textContent = formatDate(timestamp);
        time.className = 'event-time';
        time.textContent = formatTime(timestamp);
        meta.className = 'event-meta';
        meta.append(makeChip(entry.enabled ? 'Đang bật' : 'Đã tắt', entry.enabled ? 'status-on' : 'status-off'));
        meta.append(makeChip(recurring ? 'Lặp · ' + formatDays(entry.days) : 'Một lần'));
        if (isNext) meta.append(makeChip('Sắp tới', 'next-chip'));

        titleLine.append(title, kind);
        main.append(titleLine, date, time, meta);
        top.append(icon, main);
        left.append(top);
        actions.className = 'event-actions';
        toggle.type = 'button';
        toggle.className = 'compact subtle';
        toggle.textContent = entry.enabled ? 'Tắt lịch' : 'Bật lại';
        sendAction(toggle, { op: entry.enabled ? 'disable' : 'enable', id: entry.id });
        remove.type = 'button';
        remove.className = 'compact subtle danger';
        remove.textContent = 'Xóa';
        sendAction(remove, { op: 'delete', id: entry.id }, 'Xóa lịch “' + entry.title + '”?');
        actions.append(toggle);
        if (entry.enabled && recurring) {
          const skip = document.createElement('button');
          skip.type = 'button';
          skip.className = 'compact';
          skip.textContent = 'Bỏ lần tới';
          sendAction(skip, { op: 'skip', id: entry.id }, 'Bỏ qua lần tới của “' + entry.title + '”?');
          actions.append(skip);
        }
        actions.append(remove);
        card.append(left, actions);
        list.append(card);
      });
    }

    function render(state) {
      latestState = state;
      const connected = Boolean(state.available);
      $('#connection').textContent = connected ? 'Đã kết nối robot' : 'Lịch chưa sẵn sàng';
      $('#connectionPill').classList.toggle('offline', !connected);
      $('#clockState').textContent = !state.available ? 'LỊCH CHƯA SẴN SÀNG' :
        !state.storage_ok ? 'LỖI LƯU LỊCH' : state.clock_ready ? 'ĐỒNG BỘ' : 'CHỜ ĐỒNG BỘ';
      setText('#clock', state.clock_ready ? state.now : 'Đang chờ đồng bộ giờ mạng');
      $('#alert').hidden = !state.alert;
      setText('#alertText', state.alert || '');

      const remaining = Math.max(0, Number(state.remaining) || 0);
      setText('#digits', (remaining >= 3600 ? String(Math.floor(remaining / 3600)).padStart(2, '0') + ':' : '') +
        String(Math.floor(remaining / 60) % 60).padStart(2, '0') + ':' + String(remaining % 60).padStart(2, '0'));
      setText('#mode', state.running ? ({ focus: 'TẬP TRUNG', break: 'NGHỈ NGƠI', countdown: 'ĐẾM NGƯỢC' }[state.mode] || 'ĐANG CHẠY') : 'SẴN SÀNG');
      setText('#task', state.running ? state.title : 'Chọn một việc để bắt đầu');
      setText('#phase', state.running ? (state.paused ? 'Đang tạm dừng' : state.mode === 'countdown' ? 'Đang đếm ngược' : 'Vòng ' + state.round + ' / ' + state.rounds) : '');
      $('#visibility').dataset.op = state.visible ? 'hide' : 'show';
      setText('#visibility', state.visible ? 'Ẩn trên TFT' : 'Hiện trên TFT');

      const entries = Array.isArray(state.entries) ? state.entries : [];
      setText('#scheduleCount', entries.length + ' / 8 lịch');
      renderSchedule(entries);
    }

    async function send(data) {
      if (busy) return;
      busy = true;
      document.querySelectorAll('button').forEach(button => button.disabled = true);
      setText('#msg', 'Đang cập nhật…');
      try {
        const response = await fetch('/api/time', {
          method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data)
        });
        const state = await response.json();
        if (!response.ok || !state.ok) throw new Error(state.error || 'Không thực hiện được thao tác.');
        render(state);
        setText('#msg', 'Đã cập nhật lịch trên robot.');
      } catch (error) {
        setText('#msg', error.message || 'Robot chưa phản hồi.');
      } finally {
        busy = false;
        document.querySelectorAll('button').forEach(button => button.disabled = false);
      }
    }

    document.querySelectorAll('[data-op]').forEach(button => {
      button.onclick = () => send({ op: button.dataset.op });
    });
    document.querySelectorAll('[data-filter]').forEach(button => {
      button.onclick = () => {
        currentFilter = button.dataset.filter;
        if (latestState) renderSchedule(Array.isArray(latestState.entries) ? latestState.entries : []);
      };
    });

    function form(selector, convert) {
      $(selector).onsubmit = event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.target));
        send(convert(values));
      };
    }
    function selectedDays(selector) {
      return [...$(selector).querySelectorAll('input:checked')].reduce((mask, input) => mask + Number(input.value), 0);
    }
    form('#alarmForm', values => ({
      op: 'add', kind: 'alarm', title: values.title,
      hour: Number(values.time.split(':')[0]), minute: Number(values.time.split(':')[1]), days: selectedDays('#days')
    }));
    form('#reminderForm', values => ({
      op: 'add', kind: 'reminder', title: values.title, at: values.at, days: selectedDays('#reminderDays')
    }));
    form('#timerForm', values => ({
      op: 'countdown', title: values.title, seconds: Number(values.minutes) * 60 + Number(values.seconds)
    }));
    form('#pomoForm', values => ({
      op: 'pomodoro', title: values.title, focus_minutes: Number(values.focus_minutes),
      break_minutes: Number(values.break_minutes), long_break_minutes: Number(values.long_break_minutes), rounds: Number(values.rounds)
    }));

    async function poll() {
      if (polling || busy || document.hidden) return;
      polling = true;
      try {
        const response = await fetch('/api/time', { cache: 'no-store' });
        if (!response.ok) throw new Error('Robot chưa phản hồi.');
        render(await response.json());
      } catch (error) {
        $('#connection').textContent = 'Mất kết nối · đang thử lại';
        $('#connectionPill').classList.add('offline');
        setText('#clockState', 'MẤT KẾT NỐI');
      } finally {
        polling = false;
      }
    }
    poll();
    setInterval(poll, 2500);
  </script>
</body>
</html>)TIME";
