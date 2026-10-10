// background.js — Service Worker em segundo plano (Manifest V3)
// Gerencia a conexão WebUSB contínua e distribui eventos para as abas do xCloud e tester

const FILTERS = [
  { vendorId: 0x3537, productId: 0x1040 }, // GameSir Nova Lite 2.4G (Xbox 360 mode)
  { vendorId: 0x045e, productId: 0x028e }, // Xbox 360 Wired
  { vendorId: 0x045e, productId: 0x0291 }, // Xbox 360 Wireless Receiver
  { vendorId: 0x045e, productId: 0x0719 },
  { vendorId: 0x045e, productId: 0x02a9 }
];

let usbDevice = null;
let isReading = false;
let packetCount = 0;
let lastState = null;
const clientPorts = new Set();

function le16(b0, b1) {
  let val = b0 | (b1 << 8);
  if (val >= 0x8000) val -= 0x10000;
  return val;
}

function broadcastStatus(connected) {
  const statusMsg = {
    type: 'STATUS_UPDATE',
    connected: connected,
    device: usbDevice ? (usbDevice.productName || 'GameSir Nova Lite / Xbox 360') : null,
    packetCount: packetCount,
    pad: lastState
  };
  for (const port of clientPorts) {
    try {
      port.postMessage(statusMsg);
    } catch (e) {
      clientPorts.delete(port);
    }
  }
}

function broadcastState(st) {
  const stateMsg = {
    type: 'X360_STATE_UPDATE',
    pad: st,
    packetCount: packetCount
  };
  for (const port of clientPorts) {
    try {
      port.postMessage(stateMsg);
    } catch (e) {
      clientPorts.delete(port);
    }
  }
}

async function startReading(dev, epIn) {
  if (isReading) return;
  isReading = true;
  broadcastStatus(true);
  console.log(`[WebUSB Background] Loop de leitura iniciado no endpoint ${epIn}`);

  while (isReading && dev && dev.opened) {
    try {
      const res = await dev.transferIn(epIn, 32);
      if (!res || !res.data) continue;
      const d = new Uint8Array(res.data.buffer);

      // Pacote padrão Xbox 360 Wired / GameSir Nova Lite 2.4G (20 bytes: 0x00, 0x14...)
      if (d.length >= 14 && d[0] === 0x00 && d[1] === 0x14) {
        packetCount++;
        const d0 = d[2], d1 = d[3];
        let b = 0;
        if (d0 & 0x10) b |= (1 << 7); // START
        if (d0 & 0x20) b |= (1 << 6); // BACK
        if (d0 & 0x40) b |= (1 << 8); // L3
        if (d0 & 0x80) b |= (1 << 9); // R3
        if (d1 & 0x01) b |= (1 << 4); // LB
        if (d1 & 0x02) b |= (1 << 5); // RB
        if (d1 & 0x04) b |= (1 << 10); // GUIDE
        if (d1 & 0x10) b |= (1 << 0); // A
        if (d1 & 0x20) b |= (1 << 1); // B
        if (d1 & 0x40) b |= (1 << 2); // X
        if (d1 & 0x80) b |= (1 << 3); // Y

        lastState = {
          dpad_up: !!(d0 & 0x01),
          dpad_down: !!(d0 & 0x02),
          dpad_left: !!(d0 & 0x04),
          dpad_right: !!(d0 & 0x08),
          buttons: b,
          lt: d[4],
          rt: d[5],
          lx: le16(d[6], d[7]),
          ly: le16(d[8], d[9]),
          rx: le16(d[10], d[11]),
          ry: le16(d[12], d[13])
        };

        broadcastState(lastState);
      }
      // Pacote de receptor sem fio Xbox 360 (18+ bytes)
      else if (d.length >= 18 && d[1] === 0x01 && d[4] === 0x00) {
        packetCount++;
        const p = d.subarray(4);
        const d0 = p[2], d1 = p[3];
        let b = 0;
        if (d0 & 0x10) b |= (1 << 7);
        if (d0 & 0x20) b |= (1 << 6);
        if (d0 & 0x40) b |= (1 << 8);
        if (d0 & 0x80) b |= (1 << 9);
        if (d1 & 0x01) b |= (1 << 4);
        if (d1 & 0x02) b |= (1 << 5);
        if (d1 & 0x04) b |= (1 << 10);
        if (d1 & 0x10) b |= (1 << 0);
        if (d1 & 0x20) b |= (1 << 1);
        if (d1 & 0x40) b |= (1 << 2);
        if (d1 & 0x80) b |= (1 << 3);

        lastState = {
          dpad_up: !!(d0 & 0x01),
          dpad_down: !!(d0 & 0x02),
          dpad_left: !!(d0 & 0x04),
          dpad_right: !!(d0 & 0x08),
          buttons: b,
          lt: p[4],
          rt: p[5],
          lx: le16(p[6], p[7]),
          ly: le16(p[8], p[9]),
          rx: le16(p[10], p[11]),
          ry: le16(p[12], p[13])
        };

        broadcastState(lastState);
      }
    } catch (err) {
      if (err.name === 'NetworkError' || !dev.opened) {
        console.warn('[WebUSB Background] Conexão encerrada ou dispositivo desconectado.');
        break;
      }
      await new Promise(r => setTimeout(r, 4));
    }
  }

  isReading = false;
  broadcastStatus(false);
}

async function connectUSB() {
  if (usbDevice && usbDevice.opened && isReading) {
    return true;
  }
  try {
    const devices = await navigator.usb.getDevices();
    const match = devices.find(d => FILTERS.some(f => f.vendorId === d.vendorId && f.productId === d.productId));
    if (!match) {
      console.log('[WebUSB Background] Nenhum dispositivo previamente autorizado encontrado.');
      return false;
    }

    usbDevice = match;
    if (!usbDevice.opened) {
      await usbDevice.open();
    }
    if (!usbDevice.configuration || usbDevice.configuration.configurationValue !== 1) {
      try {
        await usbDevice.selectConfiguration(1);
      } catch (e) {}
    }

    try {
      await usbDevice.claimInterface(0);
    } catch (e) {
      console.warn('[WebUSB Background] claimInterface avisou:', e);
    }

    let epIn = 1;
    if (usbDevice.configuration && usbDevice.configuration.interfaces) {
      const iface = usbDevice.configuration.interfaces.find(i => i.interfaceNumber === 0);
      if (iface && iface.alternates && iface.alternates.length > 0) {
        for (const ep of iface.alternates[0].endpoints) {
          if (ep.direction === 'in') {
            epIn = ep.endpointNumber;
            break;
          }
        }
      }
    }

    startReading(usbDevice, epIn);
    return true;
  } catch (err) {
    console.error('[WebUSB Background] Falha ao abrir dispositivo:', err);
    return false;
  }
}

async function disconnectUSB() {
  isReading = false;
  if (usbDevice && usbDevice.opened) {
    try {
      await usbDevice.releaseInterface(0);
      await usbDevice.close();
    } catch (e) {}
  }
  usbDevice = null;
  broadcastStatus(false);
}

// Escuta portas de abas de jogos e da janela popup
chrome.runtime.onConnect.addListener((port) => {
  clientPorts.add(port);

  port.postMessage({
    type: 'STATUS_UPDATE',
    connected: isReading && usbDevice && usbDevice.opened,
    device: usbDevice ? (usbDevice.productName || 'GameSir Nova Lite / Xbox 360') : null,
    packetCount: packetCount,
    pad: lastState
  });

  port.onDisconnect.addListener(() => {
    clientPorts.delete(port);
  });
});

// Mensagens vindas do popup
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg.type === 'DEVICE_PAIRED') {
    connectUSB().then((ok) => sendResponse({ ok, connected: isReading }));
    return true;
  }
  if (msg.type === 'DISCONNECT_DEVICE') {
    disconnectUSB().then(() => sendResponse({ ok: true }));
    return true;
  }
  if (msg.type === 'GET_STATUS') {
    sendResponse({
      connected: isReading && usbDevice && usbDevice.opened,
      device: usbDevice ? (usbDevice.productName || 'GameSir / Xbox 360') : null,
      packetCount: packetCount,
      pad: lastState
    });
    return false;
  }
});

// Eventos de hardware USB
navigator.usb.addEventListener('connect', (event) => {
  console.log('[WebUSB Background] Dispositivo conectado na porta:', event.device);
  connectUSB();
});

navigator.usb.addEventListener('disconnect', (event) => {
  console.log('[WebUSB Background] Dispositivo removido:', event.device);
  if (usbDevice === event.device) {
    disconnectUSB();
  }
});

// Tenta conexão automática se já houver permissão prévia
connectUSB();
