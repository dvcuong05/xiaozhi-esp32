/**
 * app.js — Core initialization
 *
 * IMPORTANT: This file is loaded in <head> so it runs BEFORE the body is
 * rendered.  Only code that does NOT touch the DOM can run at the top level.
 * Anything that queries DOM elements must be inside DOMContentLoaded.
 */

// ── 1. Globals (no DOM needed) ────────────────────────────────────────────────
// Other scripts depend on AppState and socket being defined synchronously.
window.AppState = {
    ports: [],
    workingPorts: [],
};

// Socket.IO — CDN script is loaded before this file in <head>
const socket = io();

// ── 2. DOM-ready setup ────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {

    // ── Tab switching ──────────────────────────────────────────────────────
    document.querySelectorAll('.nav-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            // Deactivate all buttons and panels
            document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            // Activate clicked button and its target panel
            btn.classList.add('active');
            const targetId = btn.getAttribute('data-target');
            const panel = document.getElementById(targetId);
            if (panel) panel.classList.add('active');
        });
    });

    // ── WebSocket connection indicator ──────────────────────────────────────
    const connDot = document.getElementById('conn-indicator');

    socket.on('connect', () => {
        if (connDot) {
            connDot.classList.remove('disconnected');
            connDot.classList.add('connected');
            connDot.title = 'Server connected';
        }
    });

    socket.on('disconnect', () => {
        if (connDot) {
            connDot.classList.remove('connected');
            connDot.classList.add('disconnected');
            connDot.title = 'Server disconnected';
        }
    });
});
