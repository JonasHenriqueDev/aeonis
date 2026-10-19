# Plano: Aeonis em C++ (Qt 6 + FFmpeg)

Reescrever o Aeonis em C++ para que ele seja o mais leve possível, tenha a interface do OpenSpeak e não peça
**nada** para o usuário instalar além do próprio Aeonis.

Base de comparação: a versão C# (`main` + `ui-moderna`, ~3.650 linhas) e o app nativo do OpenSpeak
(`D:\Dev\OpenSpeak\native`), do mesmo autor, que já resolve boa parte das peças difíceis.

## 1. Metas

| Métrica | C# hoje | Meta C++ | Como medir |
|---|---|---|---|
| Instalador | 89 MB | **≤ 40 MB** | tamanho do `Aeonis-Setup-x.y.z.exe` |
| RAM na bandeja, sem jogo | ~40 MB | **≤ 15 MB** | Working Set no Gerenciador de Tarefas |
| RAM com a janela aberta | 200–300 MB (WebView2) | **≤ 120 MB** | soma dos processos do Aeonis |
| RAM depois de fechar a janela | ~40 MB | **≤ 20 MB** | Working Set 10 s após fechar |
| CPU/GPU gravando 1080p60 | referência | **igual ou menor** | mesma partida, mesmo encoder |
| Salvar clipe de 30 s | ~1 s | **≤ 1 s** | log `clip saved in` |
| Dependências externas | runtime .NET embutido, WebView2 | **nenhuma** | instalar no Windows Sandbox limpo |

O que não muda: o comportamento. Os modos Destaques/Partida completa, o buffer em segmentos de 2 s em disco,
clipes sem re-encode, os eventos de LoL/CS2, os atalhos globais e os dados em `%LOCALAPPDATA%\Aeonis` continuam
iguais. Quem atualizar mantém as configurações, o histórico e os vídeos.

## 2. Decisões técnicas

| Tema | Decisão | Motivo |
|---|---|---|
| Linguagem / build | C++20, MSVC 2022, CMake + vcpkg (manifesto) | Igual ao OpenSpeak: mesmo ambiente e mesmos scripts |
| Interface | Qt 6.8+ Quick/QML, estilo **FluentWinUI3**, tema escuro próprio | É a interface do OpenSpeak |
| Bandeja | `QSystemTrayIcon` (base: `TrayIcon.cpp` do OpenSpeak) | Menu, balões e ícone de gravação |
| Janela | `QQmlApplicationEngine` **criado ao abrir e destruído ao fechar**, depois `SetProcessWorkingSetSize` | Mantém o "janela fechada = custo quase zero" |
| Player | Qt Multimedia `MediaPlayer` + `VideoOutput` (backend FFmpeg) | Toca H.264/HEVC/AV1 sem codec da Store, com busca precisa e quadro a quadro |
| Captura de tela | DXGI Desktop Duplication próprio → textura D3D11 → `AVHWFramesContext` D3D11 | Substitui o `ddagrab`. Os frames continuam na GPU (zero-copy para NVENC/AMF) |
| Encoders | libavcodec: `h264/hevc_nvenc`, `h264/hevc_amf`, `h264_qsv` (hwmap), `libx264` como fallback | Mesma lista e mesma ordem de detecção do `Encoders.cs` |
| Buffer / clipes | Segmentos MPEG-TS de 2 s em disco (muxer `segment` da libavformat), keyframe forçado a cada 2 s, clipe = remux `-c copy` em MP4 dentro do processo | Mesmo design de hoje: barato, sobrevive a crash, salva em ~1 s |
| Áudio | WASAPI loopback + microfone (base: `AudioCapture.cpp`), preenchimento com silêncio, mixagem em PCM, AAC 160 kbps | Substitui o NAudio, os named pipes e o `amix` |
| Eventos de jogo | LoL: `QNetworkAccessManager` em `https://127.0.0.1:2999` (TLS pelo Schannel do Windows, sem OpenSSL). CS2: `QTcpServer` mínimo em `127.0.0.1:3005` | Porta direta do `EventSources.cs` |
| Atalhos globais | `RegisterHotKey` + `QAbstractNativeEventFilter` | Funciona com o jogo em foco, sem hook de teclado |
| Detecção de jogo | `CreateToolhelp32Snapshot` a cada 3 s; com jogo aberto, só checa o PID; `MonitorFromWindow` → índice DXGI | Porta direta do `GameWatcher.cs` |
| Dados | JSON (`QJsonDocument`), **mesmos arquivos**: `settings.json`, `library.jsonl`, `thumbs/`, `icons/`, `buffer/` | Atualização transparente a partir da versão C# |
| Instância única | `QLocalServer`/`QLocalSocket` ("abrir janela" quando já está rodando) | Substitui o `EventWaitHandle` |
| Atualização | `AutoUpdater` do OpenSpeak (GitHub Releases) | Já testado |
| Distribuição | `windeployqt` + DLLs do VC++ ao lado do exe + Inno Setup; zip portátil e exe portátil de arquivo único (`portable/launcher.cpp`) | Nada para instalar por fora |

### Captura dentro do processo ou separada?
A v1 roda a captura **dentro do processo**, numa thread própria, atrás de uma interface `CaptureEngine` fechada.
Como os segmentos ficam em disco, um crash perde no máximo os últimos 2 s, e o `RecoverLeftovers` recupera o resto
na próxima abertura. Se aparecer instabilidade de driver, a mesma classe passa para um `aeonis-capture.exe`
(como o `ffmpeg.exe` hoje) sem mudar o resto do app.

### Uma cópia só do FFmpeg
O Qt Multimedia traz suas próprias DLLs do FFmpeg (`avcodec-61` etc.), e a captura usa a libav do vcpkg.
- **v1:** aceitar as duas (a do vcpkg linkada estática e enxuta, ~10 MB a mais).
- **Depois:** compilar o plugin FFmpeg do Qt Multimedia contra a mesma libav do vcpkg, ou tocar o vídeo com a
  própria libav + `QVideoSink` (o `VideoDecoder.cpp` do OpenSpeak já faz isso).

## 3. O que vem do OpenSpeak

| Arquivo do OpenSpeak | Linhas | Uso no Aeonis | Adaptação |
|---|---|---|---|
| `qml/Theme.qml`, `Icon.qml`, `Toasts.qml` | ~210 | Tema, ícones, avisos | Ajustar a cor de destaque para a identidade do Aeonis |
| `main.cpp` (FluentWinUI3, barra de título escura) | 289 | Inicialização | Tirar rede/RTC; adiar a criação do engine QML |
| `TrayIcon.*` | 186 | Bandeja | Ícone de gravação (ponto vermelho do `AppIcon.cs`) |
| `SoundEffects.*` | 212 | Som de "clipe salvo" | Trocar os sons |
| `media/AudioCapture.*` | 318 | Loopback + microfone | Tirar a exclusão de processo; entregar PCM ao mixer |
| `media/VideoEncoder.*` | 504 | Abrir encoders de hardware e detectar o que está disponível | Frames D3D11 em vez de CPU; HEVC; bitrate e GOP de 2 s |
| `media/WindowGrabber.*` (parte DXGI) | 576 | Base da duplicação de tela | Tela inteira, sem recorte nem PrintWindow |
| `media/VideoDecoder.*` | 255 | Miniaturas (e player próprio, no futuro) | Decodificar 1 frame → JPG |
| `core/AutoUpdater.*`, `UpdatePolicy.*` | 433 | Atualização automática | Trocar repositório e nome dos assets |
| `core/Settings.*` | 271 | Padrão de configurações | Ler e gravar o `settings.json` atual em vez de QSettings |
| `CMakeLists.txt`, `vcpkg.json`, `build.ps1`, `package.ps1`, `installer/*.iss`, `portable/` | — | Build e empacotamento | Só o necessário para o cliente |

São cerca de 3.000 linhas prontas, aproveitadas total ou parcialmente. O OpenSpeak é AGPL-3.0, mas como o autor
é o mesmo, os trechos copiados entram no Aeonis sob a licença do Aeonis (ver §7).

## 4. Estrutura do repositório

```
aeonis/
├─ CMakeLists.txt
├─ vcpkg.json                 ffmpeg[avcodec,avformat,swresample,swscale,nvcodec,amf,qsv,x264,gpl]
├─ src/
│  ├─ main.cpp                instância única, bandeja, ciclo de vida
│  ├─ core/                   Paths, Log, Settings, Library (jsonl), MediaCache, Quota
│  ├─ capture/                DesktopDuplication, Encoders, SegmentWriter, CaptureEngine, Clips, Recovery
│  ├─ audio/                  AudioCapture (WASAPI), Mixer, AacEncoder
│  ├─ games/                  GameCatalog, GameWatcher, Highlighter, LeagueEvents, Cs2Events
│  ├─ app/                    TrayIcon, Hotkeys, SoundEffects, AutoUpdater, Autostart, WindowHost
│  └─ ui/                     LibraryModel e SettingsModel expostos ao QML (QAbstractListModel)
├─ qml/                       Main, Library, MatchView, Player, Trimmer, Settings, Theme, Icon, Toasts
├─ assets/                    aeonis.ico, sons
├─ installer/aeonis.iss       mesmo AppId da versão C# (atualiza no lugar)
├─ portable/                  launcher de arquivo único
├─ scripts/                   build.ps1, package.ps1, release.ps1
├─ tests/                     Qt Test: Settings, Library, Highlighter, GameCatalog, SegmentWriter
└─ docs/
```

Durante a migração, o C# continua em `src/Aeonis/` e o C++ cresce em `native/`. Na fase 7, o `native/` vira a raiz
e o C# é removido.

## 5. Fases

Cada fase termina com algo que roda e pode ser testado.

### F0: Fundação
- CMake + vcpkg + Qt; app de bandeja vazio (ícone, menu, Sair); instância única; log rotativo; `Paths`
  (respeitando `AEONIS_DATA`).
- `package.ps1` gerando o instalador e o zip portátil desde o primeiro dia.
- **Pronto quando:** o instalador roda no **Windows Sandbox limpo** e o ícone aparece; RAM na bandeja ≤ 15 MB.

### F1: Captura de vídeo
- Desktop Duplication → D3D11 → encoder de hardware com detecção automática (NVENC → AMF → QSV → x264).
- Escritor de segmentos de 2 s com keyframe na fronteira, o "zelador" do buffer, clipe por remux e gravação longa.
- Porta do `--selftest` (grava, salva um clipe, faz uma gravação longa e escreve `selftest.txt`).
- **Pronto quando:** o `--selftest` passa em NVIDIA e no fallback x264; clipe ≤ 1 s; CPU ≤ versão C#
  no mesmo cenário.

### F2: Áudio
- Loopback do sistema + microfone escolhido, silêncio quando não há som, mixagem, AAC, timestamps alinhados
  ao vídeo.
- **Pronto quando:** gravação de 30 min sem dessincronia perceptível (comparar com a versão C#).

### F3: Jogos, destaques e atalhos
- `GameCatalog` (lista embutida + jogos personalizados), `GameWatcher`, `Highlighter` (junta eventos próximos,
  máx. 60 s), LoL Live Client, CS2 GSI (instala o `.cfg`), atalhos `Alt+F10`/`Alt+F9`, sons e balões.
- Testes unitários do Highlighter e do parser de catálogo.
- **Pronto quando:** uma partida de LoL e uma de CS2 geram destaques sozinhas; os atalhos funcionam com o jogo
  em tela cheia sem bordas.

### F4: Biblioteca e dados
- `library.jsonl` (sessão, fim, clipe, evento), `LibraryModel` (dia → partida → clipes; vídeos sem histórico
  agrupados por jogo e proximidade), miniaturas e durações em cache, ícones dos jogos.
- Limite de armazenamento com vídeos protegidos (da branch `claude/sessao-em-andamento-c643d3`) e recuperação
  de gravações interrompidas.
- **Pronto quando:** abre o histórico real criado pela versão C# sem perder nada.

### F5: Interface QML
- Telas: biblioteca (barra lateral de jogos, partidas por dia), partida (clipes + marcadores de eventos na
  linha do tempo), player com recorte sem re-encode, configurações (geradas a partir de um esquema, como o
  `FieldDto` atual), status (gravando, jogo, encoder).
- Visual do OpenSpeak (FluentWinUI3 + `Theme.qml`), barra de título escura e lista virtualizada de miniaturas.
- Engine QML criado ao abrir e destruído ao fechar.
- **Pronto quando:** janela aberta ≤ 120 MB; depois de fechar volta a ≤ 20 MB; HEVC toca sem extensão.

### F6: Empacotamento, atualização e migração
- `windeployqt` só com os módulos usados (sem Material/Imagine/Fusion), DLLs do VC++, licenças de
  terceiros, Inno Setup com o **mesmo AppId** (atualiza a versão C# no lugar), exe portátil de arquivo único,
  `AutoUpdater`, iniciar com o Windows (`HKCU\...\Run`).
- **Pronto quando:** atualizar uma instalação C# real preserva as configurações e o histórico; instalador ≤ 40 MB.

### F7: Paridade e remoção do C#
- Checklist de paridade (abaixo), medição das metas da §1, README atualizado, remoção de `src/Aeonis/`,
  release 1.0.0.

### Checklist de paridade
- [ ] Modos Destaques e Partida completa
- [ ] Buffer sempre ligado (opcional), duração do replay, pré/pós do clipe
- [ ] Escolha de encoder, FPS, bitrate, monitor, cursor
- [ ] Áudio do sistema, microfone e dispositivo
- [ ] Destaques automáticos: abates, mortes e assistências opcionais (LoL, CS2)
- [ ] Jogos personalizados
- [ ] Atalhos configuráveis
- [ ] Limite de armazenamento com vídeos protegidos
- [ ] Recuperação após crash ou queda de energia
- [ ] Biblioteca, player, recorte, abrir pasta, mostrar no Explorer, excluir
- [ ] Iniciar com o Windows, som de notificação, abrir log
- [ ] `--selftest`

## 6. Riscos

| Risco | Mitigação |
|---|---|
| Crash de driver/encoder derruba o app | Segmentos em disco + recuperação; a captura pode virar processo separado sem refatorar |
| Monitor HDR (Desktop Duplication entrega FP16) | Converter para NV12/P010 na GPU com um shader; testar cedo na F1 |
| Índice DXGI ≠ ordem do Windows | Mapear pelo `HMONITOR` da saída DXGI em vez de confiar no índice |
| Tela cheia exclusiva antiga (DX9) fica preta | Mesma limitação de hoje; documentar |
| QSV exige hwmap D3D11 → QSV | Fallback automático para x264 se falhar |
| Duas cópias do FFmpeg | Aceitar na v1; unificar depois (§2) |
| Tempo do build do Qt/FFmpeg no vcpkg | Cache binário do vcpkg, como no OpenSpeak |

## 7. Licença: GPL-3.0-or-later

O Aeonis passa de MIT para **GNU GPL v3 ou posterior**. Motivos:

1. **Protege a proposta do projeto.** Qualquer um pode usar, estudar, modificar e redistribuir, mas quem
   distribuir uma versão modificada tem que publicar o código. Ninguém pode pegar o Aeonis, fechar o código
   e encher de anúncios, que é exatamente o que ele existe para combater. É a mesma escolha do OBS Studio (GPL).
2. **Libera o FFmpeg completo.** Com a captura linkada dentro do app, a licença do Aeonis precisa ser compatível
   com a do FFmpeg que ele inclui. Sendo GPL, dá para usar o `libx264` (o melhor encoder por software) como
   fallback para quem não tem encoder de hardware. Com MIT/LGPL, seria preciso trocar pelo OpenH264, de
   qualidade inferior.
3. **Compatível com tudo o que o projeto usa:** Qt (LGPLv3, ou GPL nos módulos GPL-only), FFmpeg (GPL),
   x264 (GPLv2+), cabeçalhos NVENC/AMF/VPL (MIT).
4. **Reaproveitamento do OpenSpeak:** ele é AGPL-3.0. Como o autor dos dois é o mesmo, os trechos copiados
   entram no Aeonis sob GPL-3.0. A cláusula de rede da AGPL não faz sentido para um app desktop sem servidor.

"Or later" permite adotar uma futura GPL v4 sem precisar procurar todos os contribuidores.

Obrigações práticas na distribuição: incluir `LICENSE`, as licenças de terceiros (`licenses/`) e um link para o
código-fonte da versão distribuída (o repositório no GitHub + a tag da release já cumprem isso).

## 8. Fora do escopo da v1
- Upload, nuvem, contas e qualquer coisa que envie dados para fora do PC.
- Editor de vídeo além do recorte.
- Linux/macOS (o Qt permitiria, mas a captura é DXGI/WASAPI).
- Overlay dentro do jogo.
