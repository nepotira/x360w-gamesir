// bridge.js — Injetado no contexto ISOLATED da página
// Conecta via Port ao background service worker e repassa eventos para o inject.js (world: MAIN)

let port = null;
let reconnectTimer = null;

function connectToBackground() {
  if (port) return;
  try {
    port = chrome.runtime.connect({ name: 'x360-page-bridge' });

    port.onMessage.addListener((message) => {
      if (message && message.type === 'X360_STATE_UPDATE') {
        window.postMessage({
          source: 'x360w-webusb-bridge',
          pad: message.pad,
          packetCount: message.packetCount
        }, '*');
      }
    });

    port.onDisconnect.addListener(() => {
      port = null;
      if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => {
          reconnectTimer = null;
          connectToBackground();
        }, 1500);
      }
    });
  } catch (err) {
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectToBackground();
      }, 2000);
    }
  }
}

connectToBackground();
