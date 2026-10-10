# Guia Definitivo: GameSir Nova Lite / Xbox 360 no macOS e Hackintosh
## Como usar o controle, xCloud e Netflix/DRM ao mesmo tempo (Modo A e Modo B)

Este documento detalha as duas soluções definitivas para fazer o **GameSir Nova Lite (via Dongle USB 2.4 GHz)** ou **controles Xbox 360** funcionarem no **macOS** (macOS 13 Ventura até macOS 15+ Sequoia / 16+ Tahoe) com **zero conflito de DRM com a Netflix, Prime Video ou Spotify**.

---

## 📌 O Diagnóstico Técnico: Por que a Netflix e o Driver entravam em conflito?

Para que um driver de terceiros crie um controle virtual no macOS (DualSense / Xbox) reconhecido pelo sistema, ele usa a API nativa da Apple **`IOHIDUserDevice`**.

1. **A barreira do AMFI (Apple Mobile File Integrity):**
   * A Apple exige o entitlement privado `com.apple.developer.hid.virtual.device`.
   * Sem assinatura paga da Apple, o kernel encerra o processo com o erro **`Killed: 9` (SIGKILL)** se o AMFI estiver estrito.
   * A solução clássica em Hackintoshes era colocar o argumento `amfi_get_out_of_my_way=1` nos `boot-args` do OpenCore.
2. **A quebra do Widevine DRM no Google Chrome:**
   * O boot-arg `amfi_get_out_of_my_way=1` desliga completamente a **Validação de Bibliotecas (*Library Validation*)** do macOS.
   * O módulo **Widevine CDM** do Google Chrome se recusa a inicializar ou descriptografar fluxos DRM quando a validação de bibliotecas está desativada, disparando o erro **`M7702-1003`** na Netflix.
3. **A limitação do Safari em Hackintoshes Intel:**
   * No Safari, a Netflix usa o **Apple FairPlay** em vez de Widevine. Em Hackintoshes com iGPU Intel de 10ª geração (Comet Lake) sem chip de segurança T2 e sem placa de vídeo dedicada suportada, o FairPlay falha com o erro **`S7706`**.
   * **Conclusão:** A Netflix **precisa** rodar dentro do **Google Chrome**.

Para atender a todos os cenários, implementamos duas soluções completas:

---

## 🟢 MODO A: A Extensão Chrome WebUSB (Recomendada para xCloud & Netflix)
> **Compatibilidade:** Macs Apple Silicon reais (M1, M2, M3, M4) e Hackintoshes (Intel/AMD).  
> **Segurança:** AMFI 100% ativo, integridade do sistema 100% preservada.

### O que é?
Uma extensão Manifest V3 para Google Chrome que lê o dongle USB 2.4 GHz do GameSir Nova Lite diretamente do espaço do usuário através da API **WebUSB** (`navigator.usb`).

* O macOS não captura a interface USB do protocolo Xbox 360 / GameSir por padrão (`Class 0xFF, SubClass 0x5D`).
* O Google Chrome tem permissão nativa para abrir a interface USB e ler os pacotes de 20 bytes do controle a mais de 120–240 Hz.
* A extensão traduz os dados brutos e injeta um controle padrão W3C (`navigator.getGamepads()`) diretamente no **Xbox Cloud Gaming (xCloud)**, **GeForce NOW**, **Amazon Luna** e sites de teste.
* **Vantagem Absoluta:** O AMFI permanece **100% ativado**. A validação de bibliotecas fica ativa, permitindo assistir à **Netflix em 1080p Full HD no Google Chrome** enquanto você joga seus jogos no xCloud!

### Como Instalar e Usar o Modo A:
1. Abra o **Google Chrome** e acesse:
   ```text
   chrome://extensions
   ```
2. No canto superior direito, ative a opção **"Modo do desenvolvedor"** (*Developer mode*).
3. Clique em **"Carregar sem compactação"** (*Load unpacked*).
4. Selecione a pasta:
   ```text
   /Users/nepo/x360w/chrome-extension
   ```
5. Conecte o dongle USB 2.4 GHz e ligue o GameSir Nova Lite com `Home + X` (LED verde).
6. Clique no ícone da extensão no Chrome e clique no botão **"Conectar GameSir (Dongle 2.4G)"**.
7. Selecione o dispositivo na lista de dispositivos USB e clique em **Conectar**.
8. Abra o [Xbox Cloud Gaming](https://www.xbox.com/play) ou o [Gamepad Tester](https://hardwaretester.com/gamepad) e jogue imediatamente!

---

## 🔵 MODO B: Driver Nativo de Sistema com AMFIPass (Para Hackintosh & Emuladores)
> **Compatibilidade:** Hackintosh OpenCore.  
> **Finalidade:** Jogos e emuladores fora do navegador (Ryujinx, RPCS3, Dolphin, RetroArch, OpenEmu) que exigem o controle reconhecido como gamepad global no macOS.  
> *(Nota: Jogos na Steam já funcionam nativamente com o GameSir Nova Lite através do Steam Input sem precisar deste driver).*

### O que é?
Em vez de desativar o AMFI de forma agressiva com `amfi_get_out_of_my_way=1`, o OpenCore utiliza a kext **`AMFIPass.kext` (v1.4.1+)** desenvolvida pela equipe do Dortania/Acidanthera.

* O `AMFIPass.kext` permite a execução de binários locais que precisam de privilégios de sistema enquanto **mantém o AMFI ativo** para as aplicações de espaço de usuário (como o Google Chrome).
* Isso preserva o *Library Validation* do Chrome, permitindo que o Widevine funcione sem erros.

### Como Configurar o OpenCore para o Modo B:

#### 1. Verificar a presença da Kext
Certifique-se de que a kext `AMFIPass.kext` está presente em:
```text
EFI/OC/Kexts/AMFIPass.kext
```
E declarada como habilitada no seu `EFI/OC/config.plist` sob `Kernel -> Add`.

#### 2. Ajustar os `boot-args` no `config.plist`
1. No seu `config.plist`, navegue até:  
   `NVRAM` → `Add` → `7C436110-AB2A-4BBB-A880-FE41995C9F82` → `boot-args`.
2. **REMOVA** o argumento prejudicial:
   ```text
   amfi_get_out_of_my_way=1   <--- REMOVER ESTE ARGUMENTO!
   ```
3. Seus `boot-args` devem conter o `-amfipassbeta` (ou nenhum argumento especial de AMFI, já que a versão 1.4.1+ cuida de tudo automaticamente).
4. Salve o `config.plist` e reinicie o computador.

#### 3. Assinar o Binário do Driver Localmente
No terminal do macOS, assine o executável com a lista de entitlements:
```bash
cd ~/x360w
sudo codesign --force --deep --sign - --entitlements entitlements.plist x360w-signed
sudo xattr -cr x360w-signed
```

#### 4. Executar o Driver
```bash
~/x360w/x360w-signed
```
O driver assumirá o dongle USB e instanciará o controle virtual `IOHIDUserDevice` (DualSense), liberando o controle para qualquer jogo nativo no macOS.

---

## 📊 Comparativo Rápido dos Modos

| Recurso | Modo A (Extensão Chrome) | Modo B (Driver Nativo + AMFIPass) | Modo Legado (amfi_get_out_of_my_way) |
| :--- | :---: | :---: | :---: |
| **xCloud no Google Chrome** | ✅ Sim | ✅ Sim | ✅ Sim |
| **Netflix no Google Chrome** | ✅ Sim (1080p) | ✅ Sim (1080p) | ❌ Quebra (M7702-1003) |
| **Netflix no Safari (iGPU Intel)** | ❌ Falha (S7706) | ❌ Falha (S7706) | ❌ Falha (S7706) |
| **Jogos Nativos fora do Chrome (Emuladores / App Store)** | ❌ Não | ✅ Sim | ✅ Sim |
| **Compatível com Apple Silicon (M1..M4)** | ✅ Sim | ❌ Não (exige kext/SIP off) | ❌ Não |
| **Exige alterar EFI / OpenCore** | ❌ Não | ✅ Sim | ✅ Sim |

---

## 🛠️ Resumo das Decisões
- Se o seu objetivo principal é jogar **xCloud / Navegador** e assistir **Netflix** na mesma máquina: use o **Modo A**. É a solução mais moderna, não mexe na EFI e é imune a atualizações do macOS.
- Se você joga **emuladores ou títulos standalone** fora do navegador: configure o OpenCore conforme o **Modo B** com `AMFIPass.kext`. *(Lembrando que jogos que rodam diretamente pela Steam já possuem suporte nativo próprio ao controle).*
