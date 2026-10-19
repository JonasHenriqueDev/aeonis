# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Fluxo de git

- **Sempre suba as mudanças para a `main`**: depois de commitar, faça push para `origin/main` (em worktree,
  traga a branch para a `main` e envie). Não deixe o trabalho parado em branches separadas nem abra PR,
  a menos que o usuário peça.
- Textos do app, comentários e mensagens de commit são em português (pt-BR).

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
- **Histórico** (`core/Library`): `%LOCALAPPDATA%\Aeonis\library.jsonl`, append-only, no formato da antiga
  versão C# (sessões, clipes, eventos). `ui/LibraryModel` varre a pasta de vídeos e cruza com esse histórico
  para montar partidas, jogos e marcadores.
- **Configurações**: `core/Settings` lê/grava `settings.json` com chaves PascalCase e enums como texto,
  compatível com a versão C#; chaves desconhecidas são preservadas em `extra`. Para adicionar uma opção:
  campo em `Settings.h`, `KnownKeys`/`fromJson`/`toJson`/`normalize` em `Settings.cpp`, e `fields()`/`apply()`
  em `ui/SettingsSchema.cpp` — a tela de configurações é gerada desse esquema (`qml/SettingsView` +
  `qml/SettingRow`, que escolhe o controle pelo `type`: bool, select, preset, int, number, hotkey, folder…).
- **UI**: QML (estilo FluentWinUI3, tema escuro em `qml/Theme.qml`). `app/WindowHost` cria a janela só quando
  aberta e a destrói ao fechar para devolver memória; `ui/Backend` é o objeto `App` exposto ao QML. Arquivos
  QML novos precisam entrar em `qt_add_qml_module` no `CMakeLists.txt`.
- **Ícones**: `assets/icons/*.png` vão no qrc (`app/AppIcon`); `assets/aeonis.ico` vai no exe pelo `.rc`. O
  processo usa o AppUserModelID `Aeonis.Aeonis`, o mesmo dos atalhos criados em `installer/aeonis.iss`.

O instalador usa o mesmo AppId da versão C# para atualizar no lugar. A versão do app vem de
`project(Aeonis VERSION x.y.z)` no `CMakeLists.txt`.
