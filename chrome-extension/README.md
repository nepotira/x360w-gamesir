# GameSir Nova Lite & Xbox 360 — Chrome WebUSB Bridge (Modo A)

Extensão para Google Chrome (Manifest V3) que conecta o dongle USB 2.4 GHz do **GameSir Nova Lite** (e controles Xbox 360) diretamente ao navegador via **WebUSB**, sem precisar carregar kexts, sem drivers em nível de kernel e **sem desativar o AMFI do macOS**.

---

## 🚀 Por que esta extensão foi criada?

1. **Netflix e DRM (Widevine) 100% funcionais no Chrome:**
   - Drivers de sistema convencionais para macOS exigem relaxar ou desativar o AMFI (`amfi_get_out_of_my_way=1` ou `amfi=0x80`), o que desativa a validação de bibliotecas (*Library Validation*) do macOS.
   - O Google Chrome recusa decodificar streams protegidos com Widevine (Netflix erro `M7702-1003`) quando o Library Validation está desligado.
   - **Esta extensão roda inteiramente em espaço de usuário via WebUSB padrão do Chrome.** Ela não requer nenhuma modificação no kernel do macOS, permitindo manter o AMFI ligado e a Netflix tocando normalmente no Chrome.
2. **Compatibilidade Universal (Apple Silicon & Hackintosh):**
   - Funciona nativamente em Macs reais com processadores Apple Silicon (**M1, M2, M3, M4**) e em **Hackintoshes** (Intel/AMD).
3. **Plug and Play:**
   - O macOS não captura a interface USB de controles Xbox 360 / GameSir por padrão (Class 0xFF, SubClass 0x5D). O Chrome tem acesso direto ao endpoint USB de entrada (`transferIn`).

---

## 📦 Como Instalar no Google Chrome

1. Abra o **Google Chrome**.
2. Digite na barra de endereços:
   ```text
   chrome://extensions
   ```
3. No canto superior direito, ative a chave **"Modo do desenvolvedor"** (*Developer mode*).
4. Clique no botão **"Carregar sem compactação"** (*Load unpacked*).
5. Selecione a pasta:
   ```text
   /Users/nepo/x360w/chrome-extension
   ```
   *(ou a pasta `chrome-extension` do repositório).*
6. A extensão **GameSir & Xbox 360 WebUSB Bridge** aparecerá instalada!

---

## 🎮 Como Usar

1. Conecte o **dongle USB 2.4 GHz** do GameSir Nova Lite no Mac.
2. Ligue o controle segurando `Home + X` (LED verde aceso no controle).
3. No Chrome, clique no ícone do quebra-cabeça (Extensões) e clique em **GameSir Nova Lite**.
4. Clique no botão **"Conectar GameSir (Dongle 2.4G)"**.
5. O Chrome abrirá uma janela nativa listando os dispositivos USB:
   - Selecione o item **GameSir Nova Lite 2.4G (Xbox 360 Mode)** ou **Xbox 360 Wired Controller**.
   - Clique em **"Conectar"**.
6. O status mudará imediatamente para **Conectado** e a contagem de pacotes começará a subir.
7. Abra o [Hardware Tester Gamepad](https://hardwaretester.com/gamepad) ou o [Xbox Cloud Gaming (xCloud)](https://www.xbox.com/play) e jogue diretamente com zero latência!

---

## 🛠️ Detalhes de Arquitetura

- **`manifest.json` (MV3):** Declara o service worker em background e os scripts de conteúdo.
- **`background/background.js`:** Mantém a conexão WebUSB ativa continuamente via `navigator.usb`, decodifica os pacotes brutos de 20 bytes do protocolo XInput (analógicos, gatilhos analógicos e botões digitais) e transmite o estado via `chrome.runtime.Port`.
- **`content/bridge.js`:** Roda no contexto isolado das páginas de jogos e repassa os pacotes para o DOM nativo.
- **`content/inject.js`:** Executado no contexto principal (`world: "MAIN"`) da página; intercepta chamadas a `navigator.getGamepads()` e emite eventos `gamepadconnected`, expondo um controle W3C Standard Gamepad nativo para a web.
