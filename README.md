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

## Download
Baixe **`Aeonis-Setup-x.y.z.exe`** em [Releases](https://github.com/JonasHenriqueDev/aeonis/releases/latest),
instale e jogue. O instalador já traz tudo (runtime .NET e FFmpeg) e não pede administrador.
Também existe uma versão `portable.zip`: é só extrair e abrir o `Aeonis.exe`.

> Como o executável ainda não tem assinatura digital, o Windows SmartScreen pode avisar na primeira vez:
> clique em **Mais informações → Executar assim mesmo**.

Depois de instalado, o Aeonis fica no ícone da bandeja (perto do relógio). Abra um jogo e use **Alt+F10**
para salvar os últimos 30 s ou **Alt+F9** para iniciar/parar uma gravação. Dê dois cliques no ícone para abrir a biblioteca.

## Por que é leve

| | |
|---|---|
| Captura | `ddagrab` (Desktop Duplication API): o frame nunca sai da GPU |
| Encode | NVENC / AMF / Quick Sync com frames D3D11 direto (zero-copy) |
| Buffer | segmentos de 2 s em disco, não um buffer gigante em RAM |
| Clipes | concatenação `-c copy` — sem re-encode, salva em ~1 s |
| App | bandeja do sistema em WinForms, ~40 MB de RAM, sem navegador embutido |

Medido numa RTX 2060, 1080p60 a 20 Mbps: FFmpeg ~4% de **um** núcleo de CPU (≈0,2% do total em 20 threads).

## Compilar a partir do código
Requisitos: Windows 10/11 x64, .NET 8 SDK, FFmpeg 6.1+ com `ddagrab` (`scripts\get-ffmpeg.ps1`) e,
para o instalador, Inno Setup 6.

```bash
dotnet build src/Aeonis -c Release
```
```bash
powershell -ExecutionPolicy Bypass -File scripts/get-ffmpeg.ps1
```
```bash
powershell -ExecutionPolicy Bypass -File scripts/publish.ps1
```
O executável fica em `dist\Aeonis.exe`. Para gerar o instalador e o zip portable da release:
```bash
powershell -ExecutionPolicy Bypass -File scripts/release.ps1
```
Teste de ponta a ponta sem UI:
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
