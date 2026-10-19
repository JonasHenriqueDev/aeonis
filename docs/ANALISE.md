# Análise do Outplayed (base de design do Aeonis)

O Aeonis é uma implementação **clean-room**: o Outplayed foi analisado só para entender *o que* ele faz.
Nenhum código, asset, texto ou binário do Outplayed/Overwolf foi copiado ou é redistribuído aqui.

Ferramentas: REA (`rea-agents`) com o provider Ghidra 12.1.4 para binários nativos e o inspetor de
metadados .NET do próprio REA para assemblies gerenciados. Alvo: Outplayed 176.4.13602 (extensão Overwolf
`cghphpbjeabdkomiphingnegihoigeggcfphdofo`) e OWOBS 315.0.7, que é o motor de captura que ele usa.

## O que foi observado

| Componente | Observação | Evidência REA |
|---|---|---|
| App Outplayed | JS empacotado (webpack) rodando no Overwolf: `background.js`, `index.js`, janelas in-game etc. Captura via `overwolf.media.replays.*` / `overwolf.streaming.*`; eventos de jogo via `overwolf.games.events.*` (GEP, proprietário do Overwolf). | `ev_e728eb49…` (grafo JS, cobertura parcial: worker sem heap nos bundles grandes) |
| `outplayed-plugin-io.dll` | .NET Framework 4.8, sem lógica de captura. Métodos de E/S e pós-processamento: `CutVideo`, `CutVideoSegments`, `GenerateThumbnail`, `GetVideoDuration`, `AdjustAudioTrackVolumes`, `ExportProject`, `UploadVideo`, `StartTiktokUpload`, `SetOverwolfFFMPEGPath`… → pós-processamento é feito chamando **FFmpeg**. | `inspect_managed_members` (lista de métodos) |
| `ow-obs.exe` (OWOBS) | Ghidra: wrapper de **libobs** com classes `obs_control::ReplayOutput`, `CommandSplitVideo`, `CommandStartReplayCapture`. Strings mostram replay buffer com `replay-max-time`/`max_size_mb`, início adiado "waiting for game capture to start", parada "game exit, stopping replay", override de `keyint_sec` e gravação dividida (`split_file`, `split_video_duration`). | `ev_1f003dab…` (search_strings) |
| `gameslist.json` | Por jogo: `defaultCaptureMode` = `highlights` ou `fullSession`, flag `isGEP` (eventos automáticos). | leitura direta |

## Como isso virou o Aeonis

| Outplayed | Aeonis (design próprio) |
|---|---|
| Overwolf + OBS completo (≈60 MB de DLLs) | Um processo `ffmpeg.exe` por sessão |
| Captura via hook do OBS | `ddagrab` (Desktop Duplication) → frames ficam na GPU → NVENC/AMF zero-copy |
| Replay buffer do OBS em RAM | Segmentos MPEG-TS de 2 s em disco; zelador apaga os antigos |
| `keyint_sec` forçado | `-force_key_frames` a cada 2 s → clipes por concatenação `-c copy`, sem re-encode |
| Modos `highlights` / `fullSession` | `Destaques` / `PartidaCompleta` — o **mesmo** encoder serve buffer, clipes e gravação longa |
| GEP do Overwolf (fechado) | APIs oficiais locais: Riot Live Client Data (LoL) e Valve Game State Integration (CS2) |
| Corte/miniatura via FFmpeg | Igual (`-c copy` e frame único) |
| Anúncios, login, upload, nuvem | Não existe. Nada sai do PC. |

## Lacunas conhecidas
- Bundles JS grandes não tiveram grafo semântico completo (limite de heap do analisador); a análise
  usou a lista de APIs e os binários, o que bastou para o design.
- Valorant, Fortnite e outros não têm API pública de eventos: no Aeonis eles usam clipe por atalho
  e/ou partida completa.
