// popup.js — Interface do usuário da extensão
// Abre diálogo de permissão WebUSB e conecta ao background service worker

const FILTERS = [
  { vendorId: 0x3537, productId: 0x1040 }, // GameSir Nova Lite 2.4G (Xbox 360 mode)
  { vendorId: 0x045e, productId: 0x028e }, // Xbox 360 Wired
  { vendorId: 0x045e, productId: 0x0291 }, // Xbox 360 Wireless Receiver
  { vendorId: 0x045e, productId: 0x0719 },
  { vendorId: 0x045e, productId: 0x02a9 }
];

let isConnected = false;
let port = null;

const elBadge = document.getElementById('status-badge');
const elBtn = document.getElementById('btn-connect');
const elDevName = document.getElementById('dev-name');
const elPackets = document.getElementById('packet-count');
const elTriggers = document.getElementById('triggers-val');
const elSticks = document.getElementById('sticks-val');
const elButtons = document.getElementById('buttons-val');

function updatePadUI(st, packetCount) {
  if (packetCount !== undefined) elPackets.textContent = packetCount;
  if (!st) return;

  elTriggers.textContent = `LT: ${st.lt} | RT: ${st.rt}`;
  elSticks.textContent = `E: ${st.lx}, ${st.ly} | D: ${st.rx}, ${st.ry}`;

  const btnList = [];
  const b = st.buttons;
  if (b & (1 << 0)) btnList.push('A');
  if (b & (1 << 1)) btnList.push('B');
  if (b & (1 << 2)) btnList.push('X');
  if (b & (1 << 3)) btnList.push('Y');
  if (b & (1 << 4)) btnList.push('LB');
  if (b & (1 << 5)) btnList.push('RB');
  if (b & (1 << 6)) btnList.push('Back');
  if (b & (1 << 7)) btnList.push('Start');
  if (b & (1 << 8)) btnList.push('L3');
  if (b & (1 << 9)) btnList.push('R3');
  if (b & (1 << 10)) btnList.push('Guide');
  if (st.dpad_up) btnList.push('Up');
  if (st.dpad_down) btnList.push('Down');
  if (st.dpad_left) btnList.push('Left');
  if (st.dpad_right) btnList.push('Right');

  elButtons.textContent = btnList.length ? btnList.join(' ') : '-';
}

function setConnectedState(connected, devName) {
  isConnected = connected;
  if (connected) {
    elBadge.textContent = 'Conectado';
    elBadge.className = 'badge connected';
    elDevName.textContent = devName || 'GameSir Nova Lite / Xbox 360';
    elBtn.textContent = 'Desconectar';
    elBtn.className = 'btn';
  } else {
    elBadge.textContent = 'Desconectado';
    elBadge.className = 'badge';
    elDevName.textContent = 'Nenhum';
    elBtn.textContent = 'Conectar GameSir (Dongle 2.4G)';
    elBtn.className = 'btn primary';
    elButtons.textContent = '-';
  }
}

// Conexão com background service worker
try {
  port = chrome.runtime.connect({ name: 'popup-monitor' });
  port.onMessage.addListener((msg) => {
    if (msg.type === 'STATUS_UPDATE') {
      setConnectedState(msg.connected, msg.device);
      if (msg.pad) updatePadUI(msg.pad, msg.packetCount);
    } else if (msg.type === 'X360_STATE_UPDATE') {
      setConnectedState(true);
      updatePadUI(msg.pad, msg.packetCount);
    }
  });
} catch (e) {
  console.warn('[Popup] Erro ao conectar ao background:', e);
}

// Botão de conectar / desconectar
elBtn.addEventListener('click', async () => {
  if (isConnected) {
    chrome.runtime.sendMessage({ type: 'DISCONNECT_DEVICE' }, () => {
      setConnectedState(false);
    });
    return;
  }

  try {
    // navigator.usb.requestDevice exige gesto do usuário (clique no popup)
    const device = await navigator.usb.requestDevice({ filters: FILTERS });
    if (device) {
      elBadge.textContent = 'Conectando...';
      chrome.runtime.sendMessage({ type: 'DEVICE_PAIRED' }, (res) => {
        if (res && res.connected) {
          setConnectedState(true, device.productName);
        }
      });
    }
  } catch (err) {
    console.warn('[Popup] Cancelado ou erro ao selecionar USB:', err);
  }
});

// Consulta status inicial
chrome.runtime.sendMessage({ type: 'GET_STATUS' }, (res) => {
  if (chrome.runtime.lastError || !res) return;
  setConnectedState(res.connected, res.device);
  if (res.pad) updatePadUI(res.pad, res.packetCount);
});
