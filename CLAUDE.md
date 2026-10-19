# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Fluxo de git

- **Sempre suba as mudanças para a `main`**: depois de commitar, faça push para `origin/main` (em worktree,
  traga a branch para a `main` e envie). Não deixe o trabalho parado em branches separadas nem abra PR,
  a menos que o usuário peça.
- Textos do app, comentários e mensagens de commit são em português (pt-BR).
- **Toda versão nova vira release no GitHub**: ao mudar `project(Aeonis VERSION x.y.z)` no `CMakeLists.txt`,
  rode `scripts/package.ps1`, suba para a `main` e crie a release `vx.y.z` com
  `gh release create vx.y.z --title "Aeonis x.y.z"` anexando os três arquivos de `dist\`
  (`Aeonis-Setup-x.y.z.exe`, `Aeonis-x.y.z-portable.zip`, `Aeonis-Portable-x.y.z.exe`) e notas em pt-BR no
  formato das anteriores (Download, Novidades). A atualização automática do app depende do
  `Aeonis-Setup-*.exe` na release mais recente, e o `package.ps1` recusa empacotar se o `Aeonis.exe` não se
  apresentar com a versão do pacote (isso já causou um loop de atualização na 1.1.0).
- **Releases precisam do cliente OAuth do YouTube**: defina `AEONIS_YOUTUBE_CLIENT_ID` e
  `AEONIS_YOUTUBE_CLIENT_SECRET` (variáveis de ambiente do usuário) antes do `package.ps1`; sem elas o envio
  ao YouTube sai desativado. Nunca commite esses valores. Uma sessão aberta antes de elas existirem não as
  herda: carregue do escopo do usuário no processo do build
  (`foreach ($v in "AEONIS_YOUTUBE_CLIENT_ID","AEONIS_YOUTUBE_CLIENT_SECRET") { Set-Item "env:$v" ([Environment]::GetEnvironmentVariable($v,"User")) }`).
- Outras sessões também lançam versões: antes de mudar a versão, faça `git fetch --tags` e use a próxima depois
  da maior release/tag existente.

## Comandos

Windows only. Requer Visual Studio 2022/2026 (C++), Qt 6.8+ MSVC com Qt Multimedia (padrão
`C:\Qt\6.11.3\msvc2022_64`) e vcpkg (padrão `D:\Dev\vcpkg`).

```powershell
.\scripts\build.ps1                  # vcpkg (FFmpeg) + CMake + build Release + ctest
.\scripts\build.ps1 -SkipTests
.\scripts\build.ps1 -Config Debug
.\scripts\package.ps1                # build + instalador/zip/portátil em dist\ (precisa do Inno Setup 6)
```

Testes (Qt Test, um único executável `tst_core`). Para rodar um teste só, passe o nome da função:

```powershell
$env:PATH = "C:\Qt\6.11.3\msvc2022_64\bin;$env:PATH"
.\build\Release\tst_core.exe settingsDefaultsAndModes
```

Teste de ponta a ponta da captura, sem interface (resultado em `%LOCALAPPDATA%\Aeonis\selftest.txt`):

```powershell
.\build\Release\Aeonis.exe --selftest --seconds 10   # também: --encoder CpuX264, --no-audio, --mic, --out pasta
```

O app é de instância única (mutex `Aeonis.SingleInstance`): se o Aeonis instalado estiver aberto, abrir a
build só traz a janela dele para frente.

## Arquitetura

Um processo só, C++20 + Qt 6 + FFmpeg (libav* linkado estaticamente via vcpkg). O CMake gera a biblioteca
`aeonis_engine` (core/capture/audio/games/Hotkeys/LibraryModel, usada também pelos testes) e o executável
`Aeonis` (app/ui + módulo QML `Aeonis`).

- **Captura** (`src/capture/CaptureEngine`): Desktop Duplication → encoder de hardware recebendo a textura
  D3D11 (NVENC/AMF zero-copy; QSV/x264 como alternativa) → segmentos MPEG-TS de 2 s em disco com o áudio
  (`audio/AudioMixer`, WASAPI → AAC). Replay buffer, clipes e gravação longa saem desses mesmos segmentos
  por concatenação sem re-encode. Os métodos bloqueiam: são chamados de uma thread de trabalho.
- **Controlador** (`src/app/App`): app de bandeja que vive sempre. Detecta jogos (`games/GameWatcher`),
  liga/desliga a captura e salva clipes. Toda operação da captura passa por uma fila serial (`enqueue`,
  `QThreadPool` com 1 thread) para manter a ordem (ex.: destaque pendente salvo antes de encerrar a sessão).
- **Destaques**: `games/EventSources` (APIs locais do LoL e Game State Integration do CS2) emite eventos;
  cada evento vira `Library::event` (marcador na linha do tempo) e, conforme o `CaptureMode`, vai para o
  `Highlighter`, que junta eventos próximos num único clipe.
- **YouTube** (`share/YouTube`): login OAuth no navegador (loopback 127.0.0.1 + PKCE), token no Gerenciador de
  Credenciais do Windows, upload resumable em partes de 8 MB; links enviados em `youtube.json`. Diálogo em `qml/YouTubeDialog`.
- **Histórico** (`core/Library`): `%LOCALAPPDATA%\Aeonis\library.jsonl`, append-only, no formato da antiga
  versão C# (sessões, clipes, eventos) mais `info` (campeão, KDA, fila, resultado e o 0:00 do relógio do LoL;
  o último da sessão vale). `ui/LibraryModel` varre a pasta de vídeos e cruza com esse histórico para montar
  partidas, jogos, marcadores e o tempo de cada clipe na partida.
- **LoL** (`games/EventSources`): a gravação longa começa com o processo e é adiantada para o 0:00 do relógio
  (`CaptureEngine::moveRecordingStart`) quando a Live Client Data API mostra o `gameTime`; a fila vem do
  lockfile do cliente (LCU) e o ícone do campeão do Data Dragon (cache em `icons\lol-*.png`).
- **Configurações**: `core/Settings` lê/grava `settings.json` com chaves PascalCase e enums como texto,
  compatível com a versão C#; chaves desconhecidas são preservadas em `extra`. Para adicionar uma opção:
  campo em `Settings.h`, `KnownKeys`/`fromJson`/`toJson`/`normalize` em `Settings.cpp`, e `fields()`/`apply()`
  em `ui/SettingsSchema.cpp` — a tela de configurações é gerada desse esquema (`qml/SettingsView` +
  `qml/SettingRow`, que escolhe o controle pelo `type`: bool, select, preset, int, number, hotkey, folder, games, customgames…). Listas e Flickables usam `qml/FastWheel` para a roda do mouse.
- **UI**: QML (estilo FluentWinUI3, tema escuro em `qml/Theme.qml`). Tela inicial = `qml/SessionsView` (Sessões › jogo › dia, faixa de sessões e player embutido, no estilo do Outplayed); a seleção fica em `win.selGame/selDay/selSession`. `app/WindowHost` cria a janela só quando
  aberta e a destrói ao fechar para devolver memória; `ui/Backend` é o objeto `App` exposto ao QML. Arquivos
  QML novos precisam entrar em `qt_add_qml_module` no `CMakeLists.txt`.
- **Barra de título própria**: a janela é `FramelessWindowHint` com `qml/TitleBar.qml`; o `TitleBarFilter`
  (`app/WindowHost.cpp`) devolve os estilos nativos e trata `WM_NCCALCSIZE`/`WM_NCHITTEST` para manter arrastar,
  Aero Snap e redimensionar. A área arrastável vem das propriedades `titleBarHeight`/`titleBarButtonsWidth` do `Main.qml`.
- **Ícones**: `assets/icons/*.png` vão no qrc (`app/AppIcon`); `assets/aeonis.ico` vai no exe pelo `.rc`. O
  processo usa o AppUserModelID `Aeonis.Aeonis`, o mesmo dos atalhos criados em `installer/aeonis.iss`.

O instalador usa o mesmo AppId da versão C# para atualizar no lugar. A versão do app vem de
`project(Aeonis VERSION x.y.z)` no `CMakeLists.txt`.
