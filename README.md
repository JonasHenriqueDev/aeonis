# Aeonis

Gravador de gameplay **open-source, standalone e sem anúncios** para Windows — uma alternativa leve ao
Outplayed. Sem Overwolf, sem conta, sem nuvem, sem telemetria.

- **Replay buffer**: `Alt+F10` salva os últimos 30 s.
- **Gravação longa**: `Alt+F9` inicia/para; ou o modo *Partida completa* grava cada partida inteira.
- **Destaques automáticos** em **League of Legends** (abates, multikills, ace, dragão, barão, torre) e
  **Counter-Strike 2** (abates, double/triple/quadra kill, ace), usando as APIs oficiais locais dos jogos.
- **Detecção automática** de mais de 20 jogos (+ jogos personalizados): começa a gravar quando o jogo abre
  e para quando fecha.
- **Biblioteca** com miniaturas agrupadas por jogo, corte rápido sem re-encode e exclusão para a lixeira.
- Áudio do sistema e microfone (WASAPI), limite de armazenamento, iniciar com o Windows.

## Por que é leve

| | |
|---|---|
| Captura | `ddagrab` (Desktop Duplication API): o frame nunca sai da GPU |
| Encode | NVENC / AMF / Quick Sync com frames D3D11 direto (zero-copy) |
| Buffer | segmentos de 2 s em disco, não um buffer gigante em RAM |
| Clipes | concatenação `-c copy` — sem re-encode, salva em ~1 s |
| App | bandeja do sistema em WinForms, ~40 MB de RAM, sem navegador embutido |

Medido numa RTX 2060, 1080p60 a 20 Mbps: FFmpeg ~4% de **um** núcleo de CPU (≈0,2% do total em 20 threads).

## Requisitos
- Windows 10/11 x64
- [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0) (ou publique *self-contained*)
- FFmpeg 6.1+ com `ddagrab` — rode `scripts\get-ffmpeg.ps1`, ou coloque `ffmpeg.exe` ao lado do `Aeonis.exe`

## Compilar e rodar
```bash
dotnet build src/Aeonis -c Release
```
```bash
powershell -ExecutionPolicy Bypass -File scripts/get-ffmpeg.ps1
```
```bash
powershell -ExecutionPolicy Bypass -File scripts/publish.ps1
```
O executável fica em `dist\Aeonis.exe`. Teste de ponta a ponta sem UI:
```bash
dist/Aeonis.exe --selftest --seconds 10
```
(o resultado vai para `%LOCALAPPDATA%\Aeonis\selftest.txt`).

## Arquitetura
```
src/Aeonis
├─ Capture/CaptureEngine.cs  um ffmpeg por sessão: ddagrab → encoder HW → segmentos .ts de 2 s
├─ Capture/AudioPipe.cs      WASAPI loopback/mic → named pipe → ffmpeg (preenche silêncio)
├─ Capture/Encoders.cs       detecção NVENC > AMF > QSV > x264
├─ Capture/Highlighter.cs    junta eventos próximos num só clipe
├─ Games/GameWatcher.cs      detecção de jogos por processo
├─ Games/EventSources.cs     LoL Live Client Data API, CS2 Game State Integration
└─ UI/                       bandeja, biblioteca, corte, configurações (PropertyGrid)
```
Dados do app: `%LOCALAPPDATA%\Aeonis` (configurações, log, buffer, miniaturas). Vídeos: `Vídeos\Aeonis\<Jogo>`.
Gravações longas interrompidas por crash/queda de energia são recuperadas na próxima abertura.

## Limitações
- Jogos em **tela cheia exclusiva** antiga (DX9) podem aparecer pretos no Desktop Duplication; use modo
  janela sem bordas (a maioria dos jogos DX11/12 no Windows 10/11 funciona normalmente).
- Gravação na resolução nativa do monitor (sem redimensionamento, para manter tudo na GPU).
- Em PCs com vários monitores o índice do DXGI pode diferir da ordem do Windows; fixe-o em Configurações.
- Destaques automáticos só em LoL e CS2 (os demais jogos não oferecem API pública de eventos).
- Ao abrir o CS2 pela primeira vez, o Aeonis instala `gamestate_integration_aeonis.cfg`; reinicie o jogo uma vez.

## Sobre a análise do Outplayed
Veja [docs/ANALISE.md](docs/ANALISE.md). O Aeonis é uma reimplementação independente (clean-room) e não
contém código nem assets do Outplayed/Overwolf. "Outplayed" é marca de seus respectivos donos.

Licença: MIT. O FFmpeg é um programa separado (LGPL/GPL) executado como processo externo.
