# Aeonis

Gravador de gameplay **open-source, standalone e sem anúncios** para Windows. Leve, sem conta, sem nuvem
e sem telemetria.

- **Replay buffer**: `Alt+F10` salva os últimos 30 s.
- **Gravação longa**: `Alt+F9` inicia/para; ou o modo *Partida completa* grava cada partida inteira.
- **Destaques automáticos** em **League of Legends** (abates, multikills, ace, dragão, barão, torre) e
  **Counter-Strike 2** (abates, double/triple/quadra kill, ace), usando as APIs oficiais locais dos jogos.
- **Detecção automática** de mais de 20 jogos (+ jogos personalizados): começa a gravar quando o jogo abre
  e para quando fecha.
- **Biblioteca** organizada: partidas por dia, clipes por jogo, player com os eventos marcados na
  linha do tempo, corte sem re-encode, ★ para proteger vídeos e exclusão para a lixeira.
- Áudio do sistema e microfone (WASAPI), limite de armazenamento, iniciar com o Windows, atualização automática.

## Download
Baixe **`Aeonis-Setup-x.y.z.exe`** em [Releases](https://github.com/JonasHenriqueDev/aeonis/releases/latest),
instale e jogue. O instalador (~29 MB) já traz tudo — Qt, FFmpeg e o runtime do Visual C++ — e não pede
administrador. Também há o `Aeonis-x.y.z-portable.zip` e o `Aeonis-Portable-x.y.z.exe` (arquivo único).

> Como o executável ainda não tem assinatura digital, o Windows SmartScreen pode avisar na primeira vez:
> clique em **Mais informações → Executar assim mesmo**.

Depois de instalado, o Aeonis fica no ícone da bandeja (perto do relógio). Abra um jogo e use **Alt+F10**
para salvar os últimos 30 s ou **Alt+F9** para iniciar/parar uma gravação. Clique no ícone para abrir a biblioteca.

Quem usava a versão anterior (C#) pode instalar por cima: configurações, histórico e vídeos continuam.

## Por que é leve

| | |
|---|---|
| Captura | DXGI Desktop Duplication própria: o quadro nunca sai da GPU |
| Encode | NVENC / AMF recebem a textura D3D11 direto (zero-copy); Quick Sync e x264 como alternativa |
| Buffer | segmentos MPEG-TS de 2 s em disco, não um buffer gigante em RAM |
| Clipes | concatenação sem re-encode dentro do processo (libavformat), salva em ~0,25 s |
| App | C++20 + Qt 6; a janela (QML) só existe enquanto está aberta e é destruída ao fechar |

Tudo roda num processo só: sem `ffmpeg.exe`, sem .NET, sem WebView2.

## Compilar a partir do código
Requisitos: Windows 10/11 x64, Visual Studio 2022 ou 2026 (C++), Qt 6.8+ para MSVC (com Qt Multimedia),
[vcpkg](https://github.com/microsoft/vcpkg) e, para o instalador, Inno Setup 6.

```bash
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -QtDir C:\Qt\6.11.3\msvc2022_64 -Vcpkg D:\Dev\vcpkg
```
O `build.ps1` instala o FFmpeg pelo manifesto `vcpkg.json` (avcodec/avformat com NVENC, AMF, QSV e x264),
compila e roda os testes. Para gerar o instalador, o ZIP e o exe portátil em `dist\`:
```bash
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```
Teste de ponta a ponta sem interface (grava, salva um clipe e uma gravação longa):
```bash
build/Release/Aeonis.exe --selftest --seconds 10
```
(o resultado vai para `%LOCALAPPDATA%\Aeonis\selftest.txt`; `--encoder CpuX264`, `--no-audio` e `--mic` também valem).

## Arquitetura
```
src/
├─ main.cpp                  instância única, bandeja, --selftest
├─ core/                     Paths, Log, Settings (settings.json), Library (library.jsonl), Media (libav), MediaCache
├─ capture/                  DesktopDuplication, Encoders, SegmentWriter, CaptureEngine, Clips (nomes, ★, limite)
├─ audio/                    AudioCapture (WASAPI loopback/microfone), AudioMixer (silêncio, mixagem, AAC)
├─ games/                    GameCatalog, GameWatcher, Highlighter, EventSources (LoL, CS2)
├─ app/                      App (controlador), TrayIcon, Hotkeys, AutoUpdater, WindowHost
└─ ui/                       Backend (objeto do QML), LibraryModel, SettingsSchema
qml/                         Main, Sidebar, MatchesView, ClipsView, Player, SettingsView, Theme…
tests/                       Qt Test: configurações e histórico da versão C#, destaques, LoL/CS2, atalhos,
                             limite de armazenamento e segmentos → MP4 → corte
```
Dados do app: `%LOCALAPPDATA%\Aeonis` (configurações, log, buffer, miniaturas, histórico). Vídeos: `Vídeos\Aeonis\<Jogo>`.
Gravações longas interrompidas por crash/queda de energia são recuperadas na próxima abertura.
O plano da migração para C++ está em [docs/PLANO_CPP.md](docs/PLANO_CPP.md).

## Limitações
- Jogos em **tela cheia exclusiva** antiga (DX9) podem aparecer pretos no Desktop Duplication; use modo
  janela sem bordas (a maioria dos jogos DX11/12 no Windows 10/11 funciona normalmente).
- Grava na resolução do monitor no início da sessão; se o jogo trocar a resolução, a imagem é redimensionada na GPU.
- Monitor girado (retrato) ainda não é corrigido; monitores HDR são gravados em SDR (8 bits).
- Destaques automáticos só em LoL e CS2 (os demais jogos não oferecem API pública de eventos).
- Ao abrir o CS2 pela primeira vez, o Aeonis instala `gamestate_integration_aeonis.cfg`; reinicie o jogo uma vez.

## Licença
[GPL-3.0-or-later](LICENSE). Você pode usar, estudar, modificar e redistribuir o Aeonis à vontade; quem
distribuir uma versão modificada precisa publicar o código dela sob a mesma licença. Assim o projeto não
pode virar um gravador fechado e cheio de anúncios. O FFmpeg (GPL, com x264) é linkado no `Aeonis.exe`;
as licenças de todas as dependências vão na pasta `licenses` da instalação.
