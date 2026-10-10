// inject.js — Injetado no contexto nativo da página (world: MAIN)
// Substitui navigator.getGamepads para expor o GameSir Nova Lite / Xbox 360 diretamente
(function() {
  if (window.__x360w_injected) return;
  window.__x360w_injected = true;

  let virtualPad = null;
  let hasDispatchedConnected = false;

  function createGamepadState(data) {
    const btnValues = new Array(17).fill(0);
    const btnPressed = new Array(17).fill(false);

    // Mapeamento padrão W3C Standard Gamepad / XInput
    // 0: A, 1: B, 2: X, 3: Y, 4: LB, 5: RB, 6: LT, 7: RT, 8: Back, 9: Start, 10: L3, 11: R3, 12: Up, 13: Down, 14: Left, 15: Right, 16: Guide
    const b = data.buttons || 0;
    btnPressed[0] = !!(b & (1 << 0));  // A
    btnPressed[1] = !!(b & (1 << 1));  // B
    btnPressed[2] = !!(b & (1 << 2));  // X
    btnPressed[3] = !!(b & (1 << 3));  // Y
    btnPressed[4] = !!(b & (1 << 4));  // LB
    btnPressed[5] = !!(b & (1 << 5));  // RB

    const ltNorm = Math.min(1.0, Math.max(0.0, (data.lt || 0) / 255.0));
    const rtNorm = Math.min(1.0, Math.max(0.0, (data.rt || 0) / 255.0));
    btnValues[6] = ltNorm; btnPressed[6] = ltNorm > 0.1; // LT
    btnValues[7] = rtNorm; btnPressed[7] = rtNorm > 0.1; // RT

    btnPressed[8] = !!(b & (1 << 6));  // Back
    btnPressed[9] = !!(b & (1 << 7));  // Start
    btnPressed[10] = !!(b & (1 << 8)); // L3
    btnPressed[11] = !!(b & (1 << 9)); // R3

    btnPressed[12] = !!data.dpad_up;    // Up
    btnPressed[13] = !!data.dpad_down;  // Down
    btnPressed[14] = !!data.dpad_left;  // Left
    btnPressed[15] = !!data.dpad_right; // Right
    btnPressed[16] = !!(b & (1 << 10)); // Guide

    for (let i = 0; i < 17; i++) {
      if (i !== 6 && i !== 7) {
        btnValues[i] = btnPressed[i] ? 1.0 : 0.0;
      }
    }

    const buttons = btnValues.map((val, idx) => ({
      pressed: btnPressed[idx],
      touched: btnPressed[idx],
      value: val
    }));

    // Analog sticks (-32768..32767 -> -1.0..1.0, Y invertido conforme padrão W3C)
    const normAxis = (val) => {
      const v = val / 32767.0;
      return Math.max(-1.0, Math.min(1.0, v));
    };

    const axes = [
      normAxis(data.lx || 0),
      -normAxis(data.ly || 0),
      normAxis(data.rx || 0),
      -normAxis(data.ry || 0)
    ];

    return {
      id: "Xbox 360 Controller (STANDARD GAMEPAD Vendor: 045e Product: 028e)",
      index: 0,
      connected: true,
      timestamp: performance.now(),
      mapping: "standard",
      axes: axes,
      buttons: buttons,
      vibrationActuator: {
        type: "dual-rumble",
        playEffect: function(type, params) { return Promise.resolve("complete"); },
        reset: function() { return Promise.resolve("complete"); }
      }
    };
  }

  // Intercepta navigator.getGamepads
  const originalGetGamepads = navigator.getGamepads ? navigator.getGamepads.bind(navigator) : null;

  navigator.getGamepads = function() {
    let raw = originalGetGamepads ? Array.from(originalGetGamepads()) : [];
    if (virtualPad) {
      if (raw.length === 0 || !raw[0]) {
        raw[0] = virtualPad;
      } else {
        const emptyIdx = raw.findIndex(p => !p);
        if (emptyIdx !== -1) raw[emptyIdx] = virtualPad;
        else raw.push(virtualPad);
      }
    }
    return raw;
  };

  // Escuta dados vindos do bridge.js
  window.addEventListener('message', (event) => {
    if (event.data && event.data.source === 'x360w-webusb-bridge') {
      const padData = event.data.pad;
      if (padData) {
        virtualPad = createGamepadState(padData);

        if (!hasDispatchedConnected) {
          hasDispatchedConnected = true;
          try {
            const ev = new Event('gamepadconnected', { bubbles: true, cancelable: false });
            Object.defineProperty(ev, 'gamepad', {
              get: () => virtualPad,
              enumerable: true,
              configurable: true
            });
            window.dispatchEvent(ev);
          } catch (e) {}
        }
      }
    }
  });

  console.log('[GameSir WebUSB Bridge] Hook injetado com sucesso no navegador.');
})();
