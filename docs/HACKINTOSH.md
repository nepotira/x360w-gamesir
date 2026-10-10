# Guia Definitivo: GameSir Nova Lite / Xbox 360 no Hackintosh (macOS 13 → 26+)
## Como conciliar Gamepad Virtual, xCloud e Netflix/DRM sem conflitos

Este adendo documenta a arquitetura e o passo a passo completo para utilizar o **GameSir Nova Lite (via Dongle 2.4GHz)** ou **Controles Xbox 360** no **Hackintosh** (OpenCore, macOS Ventura, Sonoma, Sequoia e Tahoe), mantendo serviços de streaming protegidos por DRM (como **Netflix, Apple TV e Prime Video**) 100% funcionais.

---

### 1. Entendendo o Problema Técnico no Hackintosh

Para que o macOS reconheça um controle sem fio de terceiros vindo por USB no navegador e no sistema como um controle nativo (DualSense/Xbox), o driver utiliza a API **`IOHIDUserDevice`**.

1. **A Restrição do AMFI (Apple Mobile File Integrity):**
   * A Apple restringe a criação de dispositivos virtuais através do entitlement privado `com.apple.developer.hid.virtual.device`.
   * Binários com assinatura Ad-Hoc (`codesign -s -`) que declaram essa permissão são sumariamente encerrados pelo kernel com o erro **`Killed: 9` (SIGKILL)** se o AMFI estiver em modo estrito.
   * A solução padrão da comunidade é relaxar a checagem com o argumento de inicialização:  
     `amfi_get_out_of_my_way=1` nos `boot-args` do OpenCore.

2. **O Efeito Colateral no Google Chrome (Widevine DRM):**
   * Ao relaxar o AMFI (`amfi_get_out_of_my_way=1`), o macOS desativa globalmente a **Validação de Bibliotecas (*Library Validation*)**.
   * O **Google Chrome** utiliza o módulo **Widevine CDM** para reproduzir conteúdos protegidos da Netflix, Spotify e outros. O Widevine exige validação estrita de bibliotecas na memória para evitar interceptação do fluxo de vídeo descriptografado.
   * Consequentemente, a Netflix no Chrome apresentará erros de reprodução (ex: `D7361-1253` ou falha ao carregar o componente).

---

### 2. A Solução Arquitetural: O "Dueto" Safari + Chrome

A solução mais elegante, limpa e estável no Hackintosh divide as tarefas entre as tecnologias nativas de cada navegador:

* **Para Jogos (xCloud / GeForce NOW / Navegadores):**  
  Utilize o **Google Chrome** ou **Microsoft Edge**. O driver `x360w` emula perfeitamente um controle DualSense virtual. O Chrome comunica-se com o `GameController.framework` com latência zero e suporte total a gatilhos analógicos e vibração.
  
* **Para Streaming (Netflix / Apple TV / Prime Video):**  
  Utilize o **Safari**.  
  **Por que funciona?** O Safari **NÃO** utiliza o plugin Widevine da Google. Ele utiliza o DRM proprietário da Apple (**Apple FairPlay**), decodificado diretamente pelo hardware da placa de vídeo integrada (Intel QuickSync / WhateverGreen).  
  O FairPlay **não depende** da validação de bibliotecas do AMFI. Portanto, no Safari a Netflix roda em **1080p Full HD real** (com bitrate superior ao Chrome), áudio multicanal e aceleração gráfica completa, mesmo com o `amfi_get_out_of_my_way=1` ativo!

---

### 3. Passo a Passo Completo de Instalação no OpenCore

#### Passo 1: Ajustar os `boot-args` na EFI do OpenCore

1. Monte a sua partição EFI (`diskutil mount disk0s1` ou via OpenCore Configurator / MountEFI).
2. Abra o arquivo `EFI/OC/config.plist` em um editor de texto ou editor de Plist.
3. Navegue até:  
   `NVRAM` → `Add` → `7C436110-AB2A-4BBB-A880-FE41995C9F82` → `boot-args`.
4. Adicione os dois argumentos essenciais:
   * **`amfi_get_out_of_my_way=1`**: Permite que o driver crie o gamepad virtual ad-hoc (`IOHIDUserDevice`) sem sofrer `Killed: 9`.
   * **`unfairgva=1`**: Ativa a decodificação de hardware do DRM FairPlay 1.x da Apple para GPUs Intel (WhateverGreen), garantindo reprodução fluida no Safari.

   *Exemplo de linha de `boot-args`:*
   ```xml
   <key>boot-args</key>
   <string>keepsyms=1 debug=0x100 unfairgva=1 amfi_get_out_of_my_way=1</string>
   ```
5. Salve o `config.plist` e desmonte a partição EFI.

---

#### Passo 2: Compilar e Assinar o Driver (Sem necessidade de Xcode)

O projeto foi adaptado para compilar diretamente usando o compilador standalone **Zig C Compiler** (`zig cc`), sem precisar dos gigabytes do Xcode ou do instalador de Command Line Tools da Apple:

```bash
# 1. Clone o repositório
git clone https://github.com/nepotira/x360w-gamesir.git ~/x360w
cd ~/x360w

# 2. Compile e assine o binário (gera x360w e x360w-signed)
bash scripts/build.sh
```

O script `scripts/build.sh` realiza a compilação otimizada (`-O2`) e injeta o entitlement via assinatura Ad-Hoc:
```bash
codesign -s - --entitlements entitlements.plist -f x360w-signed
```

---

#### Passo 3: Configurar Inicialização Automática em Segundo Plano (`launchd`)

Para não precisar abrir o Terminal toda vez que ligar o computador ou for jogar:

1. Copie o arquivo de serviço para a pasta de agentes do usuário:
   ```bash
   cp ~/x360w/com.x360w.driver.plist ~/Library/LaunchAgents/
   ```

2. Carregue o serviço no sistema operacional:
   ```bash
   launchctl load ~/Library/LaunchAgents/com.x360w.driver.plist
   ```

O serviço agora roda silenciosamente em segundo plano:
* Se o dongle estiver desconectado, o driver aguarda sem consumir CPU.
* Quando você espetar o dongle USB ou ligar o controle, o driver assume a porta, instancia o DualSense virtual e encaminha todos os botões e analógicos a 240 Hz.
* Se você desligar o controle, o driver volta ao modo de espera automaticamente.
* Logs de diagnóstico são gravados em `/tmp/x360w.log`.

---

### 4. Validação e Testes

1. **Reinicie o Hackintosh** para aplicar os novos `boot-args`.
2. **Conecte o Dongle:** Insira o receptor USB 2.4GHz e ligue o GameSir Nova Lite (`Home + X` para modo Dongle/Verde).
3. **Teste do Controle:**  
   Abra o **Google Chrome** e acesse [hardwaretester.com/gamepad](https://hardwaretester.com/gamepad). Pressione qualquer botão: o controle surgirá instantaneamente como um gamepad totalmente funcional.
4. **Teste de Streaming:**  
   Abra o **Safari** e reproduza qualquer filme ou série na **Netflix**. O vídeo iniciará imediatamente em 1080p Full HD com aceleração gráfica ativa.

---

### 5. Perguntas Frequentes & Diagnóstico (Troubleshooting)

* **O driver fecha com `Killed: 9`:**  
  O argumento `amfi_get_out_of_my_way=1` não foi aplicado pelo OpenCore. Verifique com o comando `nvram boot-args` no Terminal se ele consta na lista ativa. Se não constar, revise o `config.plist` e limpe a NVRAM no boot do OpenCore (*Reset NVRAM*).

* **A porta USB diz "Erro ao abrir dispositivo / LIBUSB_ERROR_ACCESS":**  
  A Steam ou outro aplicativo pode estar monopolizando a interface USB do receptor. Feche a Steam temporariamente ou aguarde alguns segundos: o driver possui reconexão automática e assumirá a porta assim que for liberada.

* **O dongle não é detectado pelo sistema:**  
  Certifique-se de que o mapeamento de portas USB do seu Hackintosh (`USBToolBox.kext` / `UTBMap.kext` ou `USBPorts.kext`) está mapeando corretamente a porta física onde o dongle foi inserido como `Internal` (255) ou `USB 2.0` (0).
