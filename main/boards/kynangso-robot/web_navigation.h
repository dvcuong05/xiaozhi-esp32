#pragma once

// Compile-time fragments keep every embedded page self-contained, with no
// extra HTTP request or runtime allocation for the shared navigation.
#define ROBOT_WEB_NAV_CURRENT " aria-current=\"page\""

#define ROBOT_WEB_NAV_STYLE                                                                        \
    ".robot-topbar{display:flex;align-items:center;justify-content:space-between;gap:20px;"        \
    "padding:25px 0;margin:0 0 28px;border-bottom:1px solid #ffffff13;font:12px/1.5 "              \
    "system-ui,-apple-system,Segoe UI,sans-serif;color:#f5f2ff}"                                   \
    ".robot-topbar a{text-decoration:none;color:inherit}.robot-topbar "                            \
    ".robot-brand{display:flex;align-items:center;gap:12px;flex-shrink:0;font-size:14px;font-"     \
    "weight:750;letter-spacing:.2px}"                                                              \
    ".robot-brandmark{display:grid;place-items:center;width:36px;height:36px;border-radius:12px;"  \
    "background:var(--nav-accent-soft);color:var(--nav-accent)}.robot-brandmark "                  \
    "svg{display:block;width:23px;height:23px;fill:none;stroke:currentColor;stroke-width:1.8;"     \
    "stroke-linecap:round;stroke-linejoin:round}"                                                  \
    ".robot-brandmark .brand-icon{display:none}.robot-topbar[data-theme=dashboard] "               \
    ".brand-icon-dashboard,.robot-topbar[data-theme=storage] .brand-icon-storage,"                 \
    ".robot-topbar[data-theme=time] .brand-icon-time,.robot-topbar[data-theme=tv] "                \
    ".brand-icon-tv{display:block}"                                                                \
    ".robot-brand "                                                                                \
    "small{display:block;color:#a8a2bc;font-size:10px;font-weight:500;letter-spacing:1.7px;text-"  \
    "transform:uppercase}"                                                                         \
    ".robot-topbar "                                                                               \
    ".robot-nav{display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin:0;padding:0}.robot-" \
    "nav a{display:flex;align-items:center;gap:7px;padding:9px "                                   \
    "13px;border:0;border-radius:10px;background:transparent;color:#a8a2bc;font-size:16px;font-"   \
    "weight:550;white-space:nowrap;transition:background .16s,color .16s}"                         \
    ".robot-nav a[aria-current=page]{background:var(--nav-accent-soft);color:var(--nav-accent)}"   \
    ".robot-nav a:hover{background:#ffffff0a;color:#f5f2ff}.robot-nav .nav-icon{width:15px;"       \
    "height:15px;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;"             \
    "stroke-linejoin:round;flex:none}.robot-topbar a:focus-visible{outline:2px solid "             \
    "var(--nav-accent);outline-offset:4px}"                                                        \
    ".robot-nav-status{display:flex;align-items:center;gap:7px;flex-wrap:wrap;font-size:11px;"     \
    "color:#a8a2bc}.robot-nav-status "                                                             \
    "small{flex-basis:100%;text-align:right;color:#a8a2bc;font-size:10px}"                         \
    ".robot-topbar "                                                                               \
    ".connection{display:flex;align-items:center;gap:7px;padding:0;border:0;border-radius:0;min-"  \
    "width:0;width:auto;background:transparent;color:#a8a2bc;font-size:11px;white-space:nowrap}"   \
    ".robot-topbar "                                                                               \
    ".dot{display:inline-block;flex-shrink:0;width:6px;height:6px;margin:0;border-radius:50%;"     \
    "background:#6d687a;box-shadow:none}.robot-topbar .dot.ok,.robot-topbar .online "              \
    ".dot{background:#d7fa78;box-shadow:0 0 13px #d7fa7855}"                                       \
    ".robot-topbar "                                                                               \
    ".live-pill{max-width:none;padding:0;border:0;border-radius:0;background:none;color:#a8a2bc;"  \
    "font-size:11px}"                                                                              \
    ".robot-topbar[data-theme=dashboard]{--nav-accent:#52b9ff;--nav-accent-soft:#52b9ff20}"        \
    ".robot-topbar[data-theme=storage]{--nav-accent:#55dbb2;--nav-accent-soft:#55dbb21f}"          \
    ".robot-topbar[data-theme=time]{--nav-accent:#b7a1ff;--nav-accent-soft:#b7a1ff22}"             \
    ".robot-topbar[data-theme=tv]{--nav-accent:#ff8cba;--nav-accent-soft:#ff8cba20}"               \
    "@media(max-width:850px){.robot-topbar{flex-wrap:wrap;gap:14px}.robot-nav-status,.robot-"      \
    "topbar>.connection,.robot-topbar>.live-pill{margin-left:auto}.robot-topbar "                  \
    ".robot-nav{order:3;flex-basis:100%}}"                                                         \
    "@media(max-width:520px){.robot-topbar{padding:16px "                                          \
    "0;margin-bottom:22px;gap:12px}.robot-topbar .robot-brand{font-size:13px}.robot-brand "        \
    "small{font-size:8px;letter-spacing:1.4px}.robot-topbar "                                      \
    ".robot-nav{gap:4px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr))}.robot-nav "    \
    "a{justify-content:center;padding:10px "                                                       \
    "4px;text-align:center;font-size:11px}.robot-nav-status,.robot-topbar>.connection,.robot-"     \
    "topbar>.live-pill{font-size:10px}.robot-nav-status small{font-size:9px}}"

#define ROBOT_WEB_NAV(DASHBOARD, STORAGE, TIME, TV, SUBTITLE, THEME, STATUS)                       \
    "<header class=\"robot-topbar\" data-theme=\"" THEME                                           \
    "\">"                                                                                          \
    "<a href=\"/\" class=\"robot-brand\"><span class=\"robot-brandmark\" "                         \
    "aria-hidden=\"true\"><svg class=\"brand-icon brand-icon-dashboard\" viewBox=\"0 0 24 24\">"   \
    "<rect x=\"5\" y=\"7\" width=\"14\" height=\"13\" rx=\"4\"/><path d=\"M12 3v4m-3 "             \
    "6h.01M15 13h.01M9 17h6\"/></svg><svg class=\"brand-icon brand-icon-storage\" "                \
    "viewBox=\"0 0 24 24\"><path d=\"M5 3h10l4 4v14H5z\"/><path d=\"M8 3v5h8M8 13h8M8 "            \
    "17h8\"/></svg><svg class=\"brand-icon brand-icon-time\" viewBox=\"0 0 24 24\"><rect "         \
    "x=\"3\" y=\"5\" width=\"18\" height=\"16\" rx=\"3\"/><path d=\"M16 3v4M8 3v4M3 "              \
    "10h18m-9 4v3l2 1\"/></svg><svg class=\"brand-icon brand-icon-tv\" viewBox=\"0 0 24 24\">"     \
    "<rect x=\"3\" y=\"6\" width=\"18\" height=\"13\" rx=\"3\"/><path d=\"m8 2 4 4 4-4M9 "         \
    "22h6\"/></svg></span><span>KyNangSo "                                                         \
    "Robot<small>" SUBTITLE                                                                        \
    "</small></span></a>"                                                                          \
    "<nav class=\"robot-nav\" aria-label=\"Điều hướng chính\">"                                    \
    "<a href=\"/\"" DASHBOARD                                                                      \
    "><svg class=\"nav-icon\" viewBox=\"0 0 24 24\"><rect x=\"5\" y=\"7\" width=\"14\" "           \
    "height=\"13\" rx=\"4\"/><path d=\"M12 3v4m-3 6h.01M15 13h.01M9 17h6\"/></svg><span>Dashboard" \
    "</span></a>"                                                                                  \
    "<a href=\"/storage\"" STORAGE                                                                 \
    "><svg class=\"nav-icon\" viewBox=\"0 0 24 24\"><path d=\"M5 3h10l4 4v14H5z\"/><path "         \
    "d=\"M8 3v5h8M8 13h8M8 17h8\"/></svg><span>Thẻ nhớ</span></a>"                                 \
    "<a href=\"/time\"" TIME                                                                       \
    "><svg class=\"nav-icon\" viewBox=\"0 0 24 24\"><rect x=\"3\" y=\"5\" width=\"18\" "           \
    "height=\"16\" rx=\"3\"/><path d=\"M16 3v4M8 3v4M3 10h18m-9 4v3l2 1\"/></svg><span>Lịch "      \
    "cá nhân</span></a>"                                                                           \
    "<a href=\"/tv\"" TV                                                                           \
    "><svg class=\"nav-icon\" viewBox=\"0 0 24 24\"><rect x=\"3\" y=\"6\" width=\"18\" "           \
    "height=\"13\" rx=\"3\"/><path d=\"m8 2 4 4 4-4M9 22h6\"/></svg><span>Live TV</span></a>"      \
    "</nav>" STATUS "</header>"
