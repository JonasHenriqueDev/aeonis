// Testes do núcleo: configurações e histórico compatíveis com a versão C#, catálogo de jogos,
// destaques, atalhos, nomes de arquivo, limite de armazenamento e o caminho segmentos -> MP4.

#include "app/Hotkeys.h"
#include "capture/Clips.h"
#include "capture/SegmentWriter.h"
#include "core/Library.h"
#include "core/Media.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "games/DeadlockMatch.h"
#include "games/DeadlockSetup.h"
#include "games/EventSources.h"
#include "games/GameCatalog.h"
#include "games/Highlighter.h"
#include "share/YouTube.h"
#include "ui/LibraryModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

using namespace aeonis;

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("AEONIS_DATA", m_data.path().toUtf8());
        Paths::ensure();
    }

    void settingsFromCSharp()
    {
        // Arquivo gravado pela versão C# (enums como texto, com BOM e uma chave obsoleta).
        const QByteArray json = "\xEF\xBB\xBF{\"OutputDir\":\"D:\\\\Videos\\\\Aeonis\",\"Mode\":\"PartidaCompleta\","
                                "\"Encoder\":\"NvidiaHevc\",\"Fps\":500,\"BitrateMbps\":25,\"MonitorIndex\":1,"
                                "\"HotkeySaveClip\":\"Ctrl+Shift+S\",\"MaxStorageGb\":0,\"FfmpegPath\":\"C:\\\\x\\\\ffmpeg.exe\"}";
        QFile f(Paths::settingsFile());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();
        Settings s = Settings::load();
        QCOMPARE(s.outputDir, QStringLiteral("D:\\Videos\\Aeonis"));
        QVERIFY(s.mode == CaptureMode::PartidaCompleta);
        QVERIFY(s.encoder == EncoderChoice::NvidiaHevc);
        QCOMPARE(s.fps, 240); // normalizado
        QCOMPARE(s.monitorIndex, 1);
        QCOMPARE(s.hotkeySaveClip, QStringLiteral("Ctrl+Shift+S"));
        QCOMPARE(s.maxStorageGb, 0.0);
        QVERIFY(s.save());
        QFile back(Paths::settingsFile());
        QVERIFY(back.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(back.readAll()).object();
        QCOMPARE(o.value("Encoder").toString(), QStringLiteral("NvidiaHevc"));
        QCOMPARE(o.value("FfmpegPath").toString(), QStringLiteral("C:\\x\\ffmpeg.exe")); // preservada
    }

    void exeDisplayName()
    {
        // Nome da versão do arquivo (ex.: "Bloco de Notas"/"Notepad"); sem versão, o nome do arquivo.
        const QString notepad = GameCatalog::displayName("C:/Windows/System32/notepad.exe");
        QVERIFY(!notepad.isEmpty());
        QVERIFY(!notepad.contains('='));
        QCOMPARE(GameCatalog::displayName("C:/nao/existe/MeuJogo.exe"), QStringLiteral("MeuJogo"));
    }

    void settingsGameFilter()
    {
        Settings s;
        QVERIFY(s.captures("Valorant"));
        s.gameFilter = "Excluir";
        s.gameList = {"Valorant"};
        QVERIFY(!s.captures("valorant"));
        QVERIFY(s.captures("League of Legends"));
        s.gameFilter = "Somente";
        QVERIFY(s.captures("Valorant"));
        QVERIFY(!s.captures("League of Legends"));
        const Settings back = Settings::fromJson(s.toJson());
        QCOMPARE(back.gameFilter, QStringLiteral("Somente"));
        QCOMPARE(back.gameList, QStringList{"Valorant"});
        QJsonObject bad = s.toJson();
        bad.insert("GameFilter", "???");
        QCOMPARE(Settings::fromJson(bad).gameFilter, QStringLiteral("Todos"));
    }

    void settingsDefaultsAndModes()
    {
        const Settings d;
        QCOMPARE(d.bitrateMbps, 10);
        QVERIFY(!d.notifications);
        QVERIFY(captureModeFrom(QStringLiteral("SomentePartida")) == CaptureMode::SomentePartida);
        QCOMPARE(toString(CaptureMode::SomentePartida), QStringLiteral("SomentePartida"));
    }

    void libraryDates()
    {
        const auto e = LibEntry::fromJsonLine(R"({"T":"clip","S":"abc","At":"2026-10-18T22:51:03.1234567-03:00","Path":"D:\\v.mp4","Kind":"highlight","Label":"Abate","Duration":21.5,"Start":"2026-10-18T22:50:41.6234567-03:00"})");
        QVERIFY(e.has_value());
        QCOMPARE(e->t, QStringLiteral("clip"));
        QVERIFY(e->at.isValid());
        QCOMPARE(e->at.toUTC().toString(Qt::ISODateWithMs), QStringLiteral("2026-10-19T01:51:03.123Z"));
        QCOMPARE(*e->duration, 21.5);
        QCOMPARE(e->start.msecsTo(e->at), 21'500);
        const auto round = LibEntry::fromJsonLine(e->toJsonLine());
        QVERIFY(round.has_value());
        QCOMPARE(round->at, e->at);
        QVERIFY(Library::parseDate("2026-10-18T22:51:03").isValid());
        QCOMPARE(Library::parseDate("2026-10-19T01:51:03.5Z").toMSecsSinceEpoch(),
                 Library::parseDate("2026-10-18T22:51:03.500-03:00").toMSecsSinceEpoch());
        QVERIFY(!Library::parseDate("2026-13-18T22:51:03Z").isValid());
    }

    void gameCatalog()
    {
        const auto custom = GameCatalog::parseCustom("# comentário\nMeu Jogo=jogo.exe; outro.EXE\n\nsolo.exe\n");
        QCOMPARE(custom.size(), 2);
        QCOMPARE(custom[0].name, QStringLiteral("Meu Jogo"));
        QCOMPARE(custom[0].processes, QStringList({"jogo", "outro"}));
        QCOMPARE(custom[1].processes, QStringList({"solo"}));
        const auto all = GameCatalog::all("X=cs2.exe");
        QCOMPARE(all.first().name, QStringLiteral("X")); // personalizados primeiro
        const auto deadlock = std::find_if(all.begin(), all.end(), [](const GameDef &g) { return g.name == QLatin1String("Deadlock"); });
        QVERIFY(deadlock != all.end() && deadlock->processes.contains(QStringLiteral("deadlock")));
    }

    void highlighterLabels()
    {
        QCOMPARE(Highlighter::label({"Abate", "Double Kill", "Abate"}), QStringLiteral("Double Kill"));
        QCOMPARE(Highlighter::label({"Abate", "Abate", "Abate"}), QStringLiteral("3 Abates"));
        QCOMPARE(Highlighter::label({"Abate"}), QStringLiteral("Abate"));
        QCOMPARE(Highlighter::label({"Torre", "Dragão"}), QStringLiteral("Dragão"));
        QCOMPARE(Highlighter::label({}), QStringLiteral("Destaque"));
    }

    void highlighterSpans()
    {
        // 10 e 25 se tocam (pré 15 + pós 5); 100 fica sozinho; 200..280 passa de 60 s e quebra em dois.
        const auto s = Highlighter::spans({{25, "Double Kill"}, {10, "Abate"}, {100, "Torre"}, {200, "Abate"}, {219, "Abate"},
                                           {238, "Abate"}, {257, "Abate"}, {276, "Morte"}}, 15, 5, 270);
        QCOMPARE(s.size(), 4);
        QCOMPARE(s[0].start, 0.0);
        QCOMPARE(s[0].end, 30.0);
        QCOMPARE(Highlighter::label(s[0].reasons), QStringLiteral("Double Kill"));
        QCOMPARE(s[1].start, 85.0);
        QCOMPARE(s[1].end, 105.0);
        QCOMPARE(s[2].reasons.size(), 4); // 200..257; 276 já passa de 60 s do primeiro
        QCOMPARE(s[2].end, 262.0);
        QCOMPARE(s[3].start, 261.0);
        QCOMPARE(s[3].end, 270.0); // limitado à duração
        QVERIFY(Highlighter::spans({}, 15, 5).isEmpty());
    }

    void highlighterMerges()
    {
        int saves = 0, seconds = 0;
        QString label;
        Highlighter h([] { return std::make_pair(15, 1); }, [&](int s, const QString &l) { ++saves; seconds = s; label = l; });
        h.trigger("Abate");
        QTest::qWait(300);
        h.trigger("Double Kill");
        QTRY_COMPARE_WITH_TIMEOUT(saves, 1, 3000);
        QCOMPARE(label, QStringLiteral("Double Kill"));
        QVERIFY(seconds >= 16 && seconds <= 18);
    }

    void leagueEvents()
    {
        const EventOptions none{}, all{true, true};
        auto ev = [](const char *json) { return QJsonDocument::fromJson(json).object(); };
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"ChampionKill","KillerName":"Eu#BR1"})"), "Eu#BR1", none), QStringLiteral("Abate"));
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"ChampionKill","KillerName":"Eu"})"), "Eu#BR1", none), QStringLiteral("Abate"));
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Eu"})"), "Eu", none), QString());
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Eu"})"), "Eu", all), QStringLiteral("Morte"));
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Y","Assisters":["Eu"]})"), "Eu", all), QStringLiteral("Assistência"));
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"Multikill","KillerName":"Eu","KillStreak":3})"), "Eu", none), QStringLiteral("Triple Kill"));
        QCOMPARE(LeagueEvents::classify(ev(R"({"EventName":"BaronKill","KillerName":"Eu"})"), "Eu", none), QStringLiteral("Barão"));
    }

    void leagueTimeline()
    {
        auto ev = [](const char *json) { return QJsonDocument::fromJson(json).object(); };
        const QStringList allies{"Amigo", "Eu"};
        auto mark = [&](const char *json) { return LeagueEvents::timelineLabel(ev(json), "Eu#BR1", "ORDER", allies); };
        // Morte e assistência sempre marcam a linha do tempo, mesmo sem virar clipe.
        QCOMPARE(mark(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Eu"})"), QStringLiteral("Morte"));
        QCOMPARE(mark(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Y","Assisters":["Eu"]})"), QStringLiteral("Assistência"));
        QCOMPARE(mark(R"({"EventName":"ChampionKill","KillerName":"X","VictimName":"Y"})"), QString());
        // Objetivos do time (inclusive torre derrubada por tropa) contam; os do inimigo, não.
        QCOMPARE(mark(R"({"EventName":"DragonKill","KillerName":"Amigo"})"), QStringLiteral("Dragão"));
        QCOMPARE(mark(R"({"EventName":"DragonKill","KillerName":"Inimigo"})"), QString());
        QCOMPARE(mark(R"({"EventName":"TurretKilled","TurretKilled":"Turret_T2_L_03_A","KillerName":"Minion_T1L0S0N0"})"), QStringLiteral("Torre"));
        QCOMPARE(mark(R"({"EventName":"TurretKilled","TurretKilled":"Turret_T1_L_03_A","KillerName":"Inimigo"})"), QString());
        QCOMPARE(mark(R"({"EventName":"InhibKilled","InhibKilled":"Barracks_T2_L1","KillerName":"Amigo"})"), QStringLiteral("Inibidor"));
    }

    void leaguePlayerInfo()
    {
        const QByteArray json = "{\"gameData\":{\"gameMode\":\"CLASSIC\",\"gameTime\":600.5},\"allPlayers\":["
            "{\"riotId\":\"Outro#BR1\",\"championName\":\"Ahri\",\"rawChampionName\":\"game_character_displayname_Ahri\",\"scores\":{\"kills\":1}},"
            "{\"riotId\":\"Eu#BR1\",\"championName\":\"Wukong\",\"rawChampionName\":\"game_character_displayname_MonkeyKing\","
            "\"level\":11,\"scores\":{\"kills\":7,\"deaths\":2,\"assists\":5,\"creepScore\":140}}]}";
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        const QJsonObject info = LeagueEvents::playerInfo(root, "Eu#BR1");
        QCOMPARE(info.value("champion").toString(), QStringLiteral("Wukong"));
        QCOMPARE(info.value("championId").toString(), QStringLiteral("MonkeyKing"));
        QCOMPARE(info.value("kills").toInt(), 7);
        QCOMPARE(info.value("deaths").toInt(), 2);
        QCOMPARE(info.value("assists").toInt(), 5);
        QCOMPARE(info.value("cs").toInt(), 140);
        QCOMPARE(info.value("mode").toString(), QStringLiteral("Summoner's Rift"));
        QCOMPARE(LeagueEvents::queueName(420), QStringLiteral("Ranqueada Solo/Duo"));
    }

    void deadlockEvents()
    {
        QCOMPARE(QDir::fromNativeSeparators(DeadlockEvents::logPath(QStringLiteral("D:/S/Deadlock/game/bin/win64/deadlock.exe"))),
                 QStringLiteral("D:/S/Deadlock/game/citadel/console.log"));
        DeadlockEvents dl([] { return EventOptions{}; }, {});
        QSignalSpy started(&dl, &EventSource::matchStarted), ended(&dl, &EventSource::matchEnded);
        const QDateTime now(QDate(2026, 11, 8), QTime(19, 50, 0));
        auto feed = [&](const char *text) {
            for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n')))
                dl.processLine(line.trimmed(), now);
        };
        // Saguão (hideout): também passa por GameInProgress, mas não é partida.
        feed(R"(11/08 19:37:32 Loading map "dl_hideout"
                11/08 19:37:32 [Client] Disconnecting from server: NETWORK_DISCONNECT_LOOPDEACTIVATE
                11/08 19:37:40 [Client] Map: "dl_hideout"
                11/08 19:37:41 ChangeGameState: PreGameWait (6)
                11/08 19:37:41 ChangeGameState: GameInProgress (7)
                11/08 19:37:41 OnGameStateChanged: GameInProgress (7))");
        QCOMPARE(started.count(), 0);
        // Partida: começa no PreGameWait (servidor e cliente repetem o estado) e acaba no PostGame.
        feed(R"(11/08 19:38:00 [Client] Disconnecting from server: NETWORK_DISCONNECT_DISCONNECT_BY_USER
                11/08 19:38:05 [Client] Map: "dl_streets"
                11/08 19:38:10 OnGameStateChanged: WaitForMapToLoad (5)
                11/08 19:49:50 OnGameStateChanged: PreGameWait (6)
                11/08 19:49:50 ChangeGameState: PreGameWait (6)
                11/08 19:49:55 OnGameStateChanged: GameInProgress (7))");
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().first().toDouble(), 10.0); // a linha é de 10 s atrás
        QVERIFY(dl.inMatch());
        feed("11/08 19:49:58 OnGameStateChanged: PostGame (8)\n11/08 19:49:59 OnGameStateChanged: End (10)");
        QCOMPARE(ended.count(), 1);
        QVERIFY(!dl.inMatch());
        // Sair no meio (desconectar) também encerra.
        feed("11/08 19:49:59 OnGameStateChanged: PreGameWait (6)\n11/08 19:50:00 [Client] Disconnecting from server: X");
        QCOMPARE(started.count(), 2);
        QCOMPARE(ended.count(), 2);
        // Replay (Aeonis aberto no meio da partida) só reconstrói o estado, sem emitir.
        DeadlockEvents late([] { return EventOptions{}; }, {});
        QSignalSpy lateStarted(&late, &EventSource::matchStarted);
        late.replay({QStringLiteral("11/08 19:38:05 [Client] Map: \"dl_streets\""),
                     QStringLiteral("11/08 19:40:00 OnGameStateChanged: GameInProgress (7)")});
        QVERIFY(late.inMatch());
        QCOMPARE(late.map(), QStringLiteral("dl_streets"));
        QCOMPARE(lateStarted.count(), 0);
        QVERIFY(late.controlsRecording());

        // Queda de conexão no meio (log real de 13/11): passa pelo saguão e reconecta à mesma partida.
        DeadlockEvents rc([] { return EventOptions{}; }, {});
        QSignalSpy rcStarted(&rc, &EventSource::matchStarted), rcEnded(&rc, &EventSource::matchEnded);
        QDateTime clock(QDate(2026, 11, 13), QTime(18, 40, 51));
        auto feedAt = [&](const char *text) {
            for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n')))
                rc.processLine(line.trimmed(), clock);
        };
        feedAt(R"(11/13 18:39:27 [Client] Map: "start"
                  11/13 18:40:22 OnGameStateChanged: PreGameWait (6)
                  11/13 18:40:51 OnGameStateChanged: GameInProgress (7))");
        QCOMPARE(rcStarted.count(), 1);
        clock = QDateTime(QDate(2026, 11, 13), QTime(18, 43, 14));
        feedAt(R"(11/13 18:42:53 [Client] Disconnecting from server: NETWORK_DISCONNECT_LOCALPROBLEM_MANYRELAYS
                  11/13 18:42:53 [Client] Map: "<empty>"
                  11/13 18:42:53 Loading map "dl_hideout"
                  11/13 18:42:57 [Client] Map: "dl_hideout"
                  11/13 18:42:57 ChangeGameState: WaitingForPlayersToJoin (2)
                  11/13 18:42:57 ChangeGameState: HeroSelection (3)
                  11/13 18:42:57 ChangeGameState: GameInProgress (7)
                  11/13 18:43:04 [Client] Disconnecting from server: NETWORK_DISCONNECT_LOOPDEACTIVATE)");
        QVERIFY(rc.waitingReconnect());
        QCOMPARE(rcEnded.count(), 0);
        feedAt(R"(11/13 18:43:08 [Client] Map: "start"
                  11/13 18:43:14 OnGameStateChanged: GameInProgress (7))");
        QVERIFY(rc.inMatch() && !rc.waitingReconnect());
        QCOMPARE(rcStarted.count(), 1); // mesma gravação
        QCOMPARE(rcEnded.count(), 0);
        clock = QDateTime(QDate(2026, 11, 13), QTime(19, 25, 41));
        feedAt(R"(11/13 19:25:31 OnGameStateChanged: PostGame (8)
                  11/13 19:25:41 OnGameStateChanged: End (11)
                  11/13 19:25:41 [Client] Disconnecting from server: NETWORK_DISCONNECT_DISCONNECT_BY_USER)");
        QCOMPARE(rcEnded.count(), 1);
        QVERIFY(!rc.waitingReconnect());

        // Sem reconexão no prazo: encerra com o fim no instante da queda.
        feedAt(R"(11/13 19:30:00 [Client] Map: "start"
                  11/13 19:30:00 OnGameStateChanged: GameInProgress (7))");
        QCOMPARE(rcStarted.count(), 2);
        clock = QDateTime(QDate(2026, 11, 13), QTime(19, 40, 0));
        feedAt("11/13 19:40:00 [Client] Disconnecting from server: NETWORK_DISCONNECT_TIMEOUT");
        QVERIFY(rc.waitingReconnect());
        rc.checkReconnect(clock.addSecs(60));
        QCOMPARE(rcEnded.count(), 1);
        rc.checkReconnect(clock.addSecs(DeadlockEvents::ReconnectGraceSeconds + 10));
        QCOMPARE(rcEnded.count(), 2);
        QCOMPARE(rcEnded.last().first().toDouble(), DeadlockEvents::ReconnectGraceSeconds + 10.0);
        QVERIFY(!rc.waitingReconnect());
    }

    void deadlockMatchApi()
    {
        QCOMPARE(DeadlockMatch::accountId(76561198000000123ULL), qint64(39734395));
        // Metadata no formato de /v1/matches/{id}/metadata (reduzido): eu sou o slot 1 do time 0.
        const QByteArray json = R"({"match_info":{"match_id":42,"winning_team":0,"match_mode":4,
            "match_pauses":[{"game_time_s":100,"pause_duration_s":0},{"game_time_s":300,"pause_duration_s":20}],
            "mid_boss":[{"team_killed":0,"team_claimed":0,"destroyed_time_s":400},{"team_claimed":1,"destroyed_time_s":900}],
            "objectives":[{"destroyed_time_s":500,"team_objective_id":3,"team":1},
                          {"destroyed_time_s":510,"team_objective_id":3,"team":0},
                          {"destroyed_time_s":0,"team_objective_id":7,"team":1},
                          {"destroyed_time_s":600,"team_objective_id":0,"team":1},
                          {"destroyed_time_s":700,"team_objective_id":9,"team":1}],
            "players":[
              {"account_id":7,"player_slot":1,"team":0,"hero_id":85,"kills":3,"deaths":1,"assists":5,"level":20,"last_hits":99,
               "death_details":[{"game_time_s":250,"killer_player_slot":8}]},
              {"account_id":8,"player_slot":8,"team":1,"death_details":[{"game_time_s":200,"killer_player_slot":1},
                                                                         {"game_time_s":320,"killer_player_slot":1}]},
              {"account_id":9,"player_slot":9,"team":1,"death_details":[{"game_time_s":208,"killer_player_slot":1},
                                                                         {"game_time_s":330,"killer_player_slot":2}]}]}})";
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QVERIFY(!DeadlockMatch::parse(root, 1234, {}).ok);
        const DeadlockMatch::Parsed p = DeadlockMatch::parse(root, 7, {});
        QVERIFY(p.ok);
        QCOMPARE(p.heroId, 85);
        QCOMPARE(p.info.value("kills").toInt(), 3);
        QCOMPARE(p.info.value("result").toString(), QStringLiteral("Win"));
        QCOMPARE(p.info.value("mode").toString(), QStringLiteral("Ranqueada"));
        QStringList got;
        for (const DeadlockMatch::Event &e : p.events)
            got << QStringLiteral("%1 %2%3").arg(e.gameTime).arg(e.label, e.highlight ? QStringLiteral("*") : QString());
        // Abates aos 200 e 208 (double kill), morte aos 250 (sem clipe: opção desligada), abate aos 320,
        // chefe do meio que meu time pegou, guardião e patrono inimigos (o meu guardião, o núcleo e o 0 s ficam fora).
        QCOMPARE(got, QStringList({"200 Abate*", "208 Abate*", "208 Double Kill*", "250 Morte", "320 Abate*",
                                   "400 Chefe do meio", "500 Guardião", "700 Patrono"}));
        EventOptions withDeaths;
        withDeaths.deaths = true;
        QVERIFY(DeadlockMatch::parse(root, 7, withDeaths).events.at(3).highlight);
        // Relógio: 0:00 no GameInProgress; a pausa de 20 s aos 300 empurra o que vem depois.
        const QDateTime zero(QDate(2026, 11, 13), QTime(18, 40, 51));
        QCOMPARE(DeadlockMatch::wallTime(zero, 200, p.pauses), zero.addSecs(200));
        QCOMPARE(DeadlockMatch::wallTime(zero, 320, p.pauses), zero.addSecs(340));

        // O log dá o id da partida, a conta e o 0:00 do relógio.
        DeadlockEvents dl([] { return EventOptions{}; }, {});
        const QDateTime now(QDate(2026, 11, 13), QTime(18, 41, 0));
        for (const char *line : {"11/13 18:17:44 [SteamNetSockets] AuthStatus (steamid:90297416925726737):  OK  (OK)",
                                 "11/13 18:17:44 [SteamNetSockets] AuthStatus (steamid:76561198000000123):  Attempting  (Requesting cert)",
                                 "11/13 18:39:26 Lobby 102166054893010264 for Match 136866150 created",
                                 "11/13 18:39:27 [Client] Map: \"start\"",
                                 "11/13 18:40:22 OnGameStateChanged: PreGameWait (6)",
                                 "11/13 18:40:51 OnGameStateChanged: GameInProgress (7)"})
            dl.processLine(QString::fromUtf8(line), now);
        QCOMPARE(dl.matchId(), qint64(136866150));
        QCOMPARE(dl.accountId(), qint64(39734395));
        QCOMPARE(dl.clockZero(), zero);
        // Partida seguinte sem a linha do lobby: não herda o id da anterior.
        for (const char *line : {"11/13 19:25:31 OnGameStateChanged: PostGame (8)",
                                 "11/13 19:40:00 [Client] Map: \"bots\"",
                                 "11/13 19:40:10 OnGameStateChanged: GameInProgress (7)"})
            dl.processLine(QString::fromUtf8(line), now);
        QVERIFY(dl.inMatch());
        QCOMPARE(dl.matchId(), qint64(0));

        // Metadata real (opcional): AEONIS_DEADLOCK_SAMPLE=arquivo.json;account_id
        const QStringList sample = qEnvironmentVariable("AEONIS_DEADLOCK_SAMPLE").split(QLatin1Char(';'));
        if (sample.size() == 2) {
            QFile f(sample[0]);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const DeadlockMatch::Parsed real = DeadlockMatch::parse(QJsonDocument::fromJson(f.readAll()).object(), sample[1].toLongLong(), {});
            QVERIFY(real.ok);
            for (const DeadlockMatch::Event &e : real.events)
                qWarning().noquote() << "DEADLOCK" << e.gameTime << e.label << e.highlight
                                     << DeadlockMatch::wallTime(zero, e.gameTime, real.pauses).toString(QStringLiteral("HH:mm:ss"));
            qWarning().noquote() << "DEADLOCK info" << QJsonDocument(real.info).toJson(QJsonDocument::Compact);
        }
    }

    void compressPlanDiscord()
    {
        Media::SourceInfo src;
        src.ok = true;
        src.width = 1920;
        src.height = 1080;
        src.fps = 60;
        src.audioBitrate = 160'000;
        // Curto: qualidade cheia (teto de 8 Mbps, 1080p60), bem abaixo de 20 MB.
        const Media::CompressPlan shortClip = Media::compressPlan(src, 10);
        QCOMPARE(shortClip.height, 1080);
        QCOMPARE(shortClip.fps, 60.0);
        QVERIFY(shortClip.fits && shortClip.estimatedBytes < Media::DiscordLimitBytes);
        // 2 min: cabe em 20 MB descendo resolução e fps.
        const Media::CompressPlan twoMin = Media::compressPlan(src, 120);
        QVERIFY(twoMin.fits);
        QVERIFY(twoMin.estimatedBytes <= Media::DiscordLimitBytes);
        QVERIFY(twoMin.height < 1080);
        QCOMPARE(twoMin.fps, 30.0);
        // Longo demais: nem no mínimo cabe.
        QVERIFY(!Media::compressPlan(src, 20 * 60).fits);
        // Nunca aumenta a resolução da origem.
        src.height = 720;
        QCOMPARE(Media::compressPlan(src, 10).height, 720);
        // Compressão real de um vídeo (opcional): AEONIS_COMPRESS_SAMPLE=arquivo;inicio;fim
        const QStringList sample = qEnvironmentVariable("AEONIS_COMPRESS_SAMPLE").split(QLatin1Char(';'));
        if (sample.size() == 3) {
            const QString out = m_data.path() + QStringLiteral("/compress_sample.mp4");
            const Media::RemuxResult r = Media::trim(sample[0], out, sample[1].toDouble(), sample[2].toDouble(), true);
            QVERIFY2(r.ok, qPrintable(r.error));
            qWarning() << "COMPRESS" << QFileInfo(out).size() << "bytes" << r.duration << "s";
            QVERIFY(QFileInfo(out).size() <= Media::DiscordLimitBytes);
        }
    }

    void deadlockSetup()
    {
        using namespace DeadlockSetup;
        // Trecho real do localconfig.vdf: o app aparece em mais de uma seção.
        const QString vdf = QStringLiteral(
            "\"UserLocalConfigStore\"\n{\n\t\"apptickets\"\n\t{\n\t\t\"1422450\"\t\t\"abc\"\n\t}\n"
            "\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"apps\"\n\t\t\t\t{\n"
            "\t\t\t\t\t\"1422450\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1794427107\"\n"
            "\t\t\t\t\t\t\"cloud\"\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\"last_sync_state\"\t\t\"synchronized\"\n\t\t\t\t\t\t}\n"
            "\t\t\t\t\t\t\"LaunchOptions\"\t\t\"-console -vulkan -dev\"\n\t\t\t\t\t}\n"
            "\t\t\t\t\t\"730\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LaunchOptions\"\t\t\"-condebug\"\n\t\t\t\t\t}\n"
            "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n");
        const AppConfig c = appConfig(vdf);
        QVERIFY(c.found);
        QCOMPARE(c.launchOptions, QStringLiteral("-console -vulkan -dev"));
        QCOMPARE(c.lastPlayed, 1794427107LL);
        QVERIFY(!hasCondebug(c.launchOptions)); // o -condebug do CS2 (730) não conta
        QVERIFY(hasCondebug(QStringLiteral("-console  -CONDEBUG -dev")));
        QVERIFY(!hasCondebug(QStringLiteral("-condebugx")));
        QVERIFY(!appConfig(QStringLiteral("\"730\" { \"LaunchOptions\" \"x\" }")).found);
        // Bloco mais recente sem LaunchOptions não esconde o que tem as opções.
        const AppConfig two = appConfig(QStringLiteral(
            "\"1422450\" { \"LastPlayed\" \"5\" \"LaunchOptions\" \"-condebug\" } \"1422450\" { \"LastPlayed\" \"9\" }"));
        QVERIFY(hasCondebug(two.launchOptions));
    }

    void cs2Events()
    {
        Cs2Events cs([] { return EventOptions{true, false}; }, {}, {});
        QSignalSpy spy(&cs, &EventSource::highlight);
        auto state = [](int kills, int deaths, int roundKills, const char *steam = "1") {
            QJsonObject o = QJsonDocument::fromJson(QStringLiteral(
                R"({"auth":{"token":"aeonis-gsi"},"provider":{"steamid":"1"},"map":{"name":"de_dust2"},
                    "player":{"steamid":"%1","state":{"round_kills":%2},"match_stats":{"kills":%3,"deaths":%4}}})")
                .arg(QLatin1String(steam)).arg(roundKills).arg(kills).arg(deaths).toUtf8()).object();
            return o;
        };
        cs.process(state(0, 0, 0));
        cs.process(state(1, 0, 1));
        cs.process(state(3, 0, 3));
        cs.process(state(3, 1, 0));
        cs.process(state(9, 9, 5, "2")); // espectando outro jogador: ignora
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy[0][0].toString(), QStringLiteral("Abate"));
        QCOMPARE(spy[1][0].toString(), QStringLiteral("Triple Kill"));
        QCOMPARE(spy[2][0].toString(), QStringLiteral("Morte"));
    }

    void hotkeys()
    {
        UINT mods = 0, vk = 0;
        QVERIFY(Hotkeys::parse("Alt+F10", &mods, &vk));
        QCOMPARE(mods, UINT(MOD_ALT));
        QCOMPARE(vk, UINT(VK_F10));
        QVERIFY(Hotkeys::parse("Ctrl+Shift+S", &mods, &vk));
        QCOMPARE(mods, UINT(MOD_CONTROL | MOD_SHIFT));
        QCOMPARE(vk, UINT('S'));
        QVERIFY(Hotkeys::parse("Ctrl+D1", &mods, &vk));
        QCOMPARE(vk, UINT('1'));
        QVERIFY(Hotkeys::parse("Alt+NumPad5", &mods, &vk));
        QCOMPARE(vk, UINT(VK_NUMPAD5));
        QVERIFY(!Hotkeys::parse("Alt+", &mods, &vk));
        QVERIFY(!Hotkeys::parse("Ctrl+Banana", &mods, &vk));
    }

    void clipNames()
    {
        QCOMPARE(Clips::sanitize("a<b>:c?. "), QStringLiteral("a_b__c_"));
        QCOMPARE(Clips::sanitize("..."), QStringLiteral("Clip"));
        QCOMPARE(LibraryModel::labelFromName("League of Legends_2026-10-18_22-51-03_Double Kill_2"), QStringLiteral("Double Kill"));
        QCOMPARE(LibraryModel::kindFromLabel("Partida"), QStringLiteral("full"));
        QCOMPARE(LibraryModel::kindFromLabel("Clipe (corte)"), QStringLiteral("manual"));
        QCOMPARE(LibraryModel::kindFromLabel("Ace"), QStringLiteral("highlight"));
    }

    void quotaKeepsSavedAndNewest()
    {
        QTemporaryDir out;
        auto make = [&](const QString &name, int mb, int ageSec) {
            const QString path = out.path() + "/g/" + name;
            QDir().mkpath(out.path() + "/g");
            QFile f(path);
            (void)f.open(QIODevice::WriteOnly);
            f.write(QByteArray(mb * 1024 * 1024, 'x'));
            f.close();
            f.setFileTime(QDateTime::currentDateTime().addSecs(-ageSec), QFileDevice::FileModificationTime);
            return path;
        };
        const QString oldest = make("a.mp4", 3, 300), saved = make("b.mp4", 3, 200), mid = make("c.mp4", 3, 100), newest = make("d.mp4", 3, 0);
        Clips::setSaved({saved}, true);
        // Limite de 4 MB: os não salvos (9 MB) precisam cair para 4 MB, mas o mais novo fica.
        const int removed = Clips::enforceQuota(out.path(), 4.0 / 1024);
        QCOMPARE(removed, 2);
        QVERIFY(!QFile::exists(oldest));
        QVERIFY(!QFile::exists(mid));
        QVERIFY(QFile::exists(saved));
        QVERIFY(QFile::exists(newest));
        QCOMPARE(Clips::unsavedBytes(out.path()), qint64(3 * 1024 * 1024));

        // Vídeo de um remux interrompido: some na limpeza, os completos ficam.
        const QString partial = out.path() + "/g/e.mp4.part";
        QFile pf(partial);
        QVERIFY(pf.open(QIODevice::WriteOnly));
        pf.write(QByteArray(1000, 'x'));
        pf.close();
        QCOMPARE(Clips::removePartials(out.path()), qint64(1000));
        QVERIFY(!QFile::exists(partial));
        QVERIFY(QFile::exists(newest));
    }

    /// Codifica 6 s sintéticos (x264 + AAC) em segmentos de 2 s e concatena/corta sem re-encode.
    void segmentsToMp4()
    {
        const AVCodec *vc = avcodec_find_encoder_by_name("libx264");
        QVERIFY(vc);
        AVCodecContext *v = avcodec_alloc_context3(vc);
        v->width = 320;
        v->height = 180;
        v->time_base = {1, 30};
        v->framerate = {30, 1};
        v->pix_fmt = AV_PIX_FMT_YUV420P;
        v->gop_size = 60;
        v->max_b_frames = 0;
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "preset", "ultrafast", 0);
        av_dict_set(&opts, "forced-idr", "1", 0);
        QVERIFY(avcodec_open2(v, vc, &opts) >= 0);
        av_dict_free(&opts);
        const AVCodec *ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
        AVCodecContext *a = avcodec_alloc_context3(ac);
        a->sample_fmt = AV_SAMPLE_FMT_FLTP;
        a->sample_rate = 48000;
        av_channel_layout_default(&a->ch_layout, 2);
        a->bit_rate = 128000;
        a->time_base = {1, 48000};
        QVERIFY(avcodec_open2(a, ac, nullptr) >= 0);

        QTemporaryDir dir;
        SegmentWriter writer;
        QString err;
        QVERIFY2(writer.open(dir.path(), 0, v, a, &err), qPrintable(err));
        AVPacket *pkt = av_packet_alloc();
        auto drainV = [&] { while (avcodec_receive_packet(v, pkt) == 0) { writer.writeVideo(pkt); av_packet_unref(pkt); } };
        auto drainA = [&] { while (avcodec_receive_packet(a, pkt) == 0) { writer.writeAudio(pkt); av_packet_unref(pkt); } };
        AVFrame *f = av_frame_alloc();
        f->format = v->pix_fmt;
        f->width = v->width;
        f->height = v->height;
        av_frame_get_buffer(f, 0);
        AVFrame *af = av_frame_alloc();
        af->format = AV_SAMPLE_FMT_FLTP;
        af->sample_rate = 48000;
        av_channel_layout_copy(&af->ch_layout, &a->ch_layout);
        af->nb_samples = a->frame_size;
        av_frame_get_buffer(af, 0);
        int64_t audioPts = 0;
        for (int i = 0; i < 180; ++i) {
            av_frame_make_writable(f);
            memset(f->data[0], (i * 3) & 0xff, f->linesize[0] * f->height);
            memset(f->data[1], 128, f->linesize[1] * f->height / 2);
            memset(f->data[2], 128, f->linesize[2] * f->height / 2);
            f->pts = i;
            const bool key = i % 60 == 0;
            f->pict_type = key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
            if (key) f->flags |= AV_FRAME_FLAG_KEY; else f->flags &= ~AV_FRAME_FLAG_KEY;
            QVERIFY(avcodec_send_frame(v, f) >= 0);
            drainV();
            while (audioPts < (i + 1) * 1600) {
                av_frame_make_writable(af);
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < af->nb_samples; ++s)
                        reinterpret_cast<float *>(af->data[c])[s] = 0.1f * std::sin((audioPts + s) * 0.05);
                af->pts = audioPts;
                audioPts += af->nb_samples;
                QVERIFY(avcodec_send_frame(a, af) >= 0);
                drainA();
            }
        }
        avcodec_send_frame(v, nullptr);
        drainV();
        avcodec_send_frame(a, nullptr);
        drainA();
        writer.close();
        QCOMPARE(writer.currentIndex(), 2); // 3 segmentos: 0, 1, 2

        QStringList segs;
        for (int i = 0; i < 3; ++i) {
            const QString s = dir.path() + QStringLiteral("/seg_%1.ts").arg(i, 6, 10, QLatin1Char('0'));
            QVERIFY(QFile::exists(s));
            segs << s;
        }
        const QString mp4 = dir.path() + "/out.mp4";
        const Media::RemuxResult r = Media::concat(segs, mp4);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.duration > 5.5 && r.duration < 6.5, qPrintable(QString::number(r.duration)));
        QVERIFY(!QFile::exists(mp4 + ".part")); // escreve no .part e renomeia no fim
        // Gravação longa: sem faststart, mesmo resultado.
        const Media::RemuxResult plain = Media::concat(segs, dir.path() + "/plain.mp4", false);
        QVERIFY2(plain.ok, qPrintable(plain.error));
        QVERIFY(qAbs(plain.duration - r.duration) < 0.1);
        // Cancelado: nem o .mp4 nem o .part ficam na pasta.
        const std::atomic<bool> cancel{true};
        QVERIFY(!Media::concat(segs, dir.path() + "/cancel.mp4", true, &cancel).ok);
        QVERIFY(!QFile::exists(dir.path() + "/cancel.mp4"));
        QVERIFY(!QFile::exists(dir.path() + "/cancel.mp4.part"));
        // Só os dois últimos segmentos (como um clipe de ~4 s).
        const Media::RemuxResult last = Media::concat(segs.mid(1), dir.path() + "/last.mp4");
        QVERIFY(last.ok);
        QVERIFY2(last.duration > 3.5 && last.duration < 4.5, qPrintable(QString::number(last.duration)));
        // Corte sem re-encode a partir do keyframe de 2 s.
        const Media::RemuxResult cut = Media::trim(mp4, dir.path() + "/cut.mp4", 2.5, 5.0);
        QVERIFY2(cut.ok, qPrintable(cut.error));
        QVERIFY(qAbs(cut.startOffset - 2.0) < 0.1);
        QVERIFY2(cut.duration > 2.5 && cut.duration < 3.6, qPrintable(QString::number(cut.duration)));
        // Corte comprimido (x264): começa exatamente no início pedido.
        const Media::RemuxResult small = Media::trim(mp4, dir.path() + "/small.mp4", 2.5, 5.0, true);
        QVERIFY2(small.ok, qPrintable(small.error));
        QCOMPARE(small.startOffset, 2.5);
        QVERIFY2(small.duration > 2.2 && small.duration < 2.9, qPrintable(QString::number(small.duration)));
        // Miniatura e duração do MP4.
        const double d = Media::thumbnail(mp4, dir.path() + "/t.jpg");
        QVERIFY(d > 5.5);
        QVERIFY(QFile::exists(dir.path() + "/t.jpg"));

        av_frame_free(&f);
        av_frame_free(&af);
        av_packet_free(&pkt);
        avcodec_free_context(&v);
        avcodec_free_context(&a);
    }

    void youtubeRules()
    {
        // PKCE: exemplo do apêndice B da RFC 7636.
        QCOMPARE(YouTube::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        // Título: sem < >, espaços normalizados, no máximo 100 caracteres, nunca vazio.
        QCOMPARE(YouTube::sanitizeTitle(QStringLiteral("  Pentakill  <LoL>  ")), QStringLiteral("Pentakill ‹LoL›"));
        QCOMPARE(YouTube::sanitizeTitle(QString(150, QLatin1Char('a'))).size(), 100);
        QCOMPARE(YouTube::sanitizeTitle(QStringLiteral("   ")), QStringLiteral("Clipe do Aeonis"));
        // Descrição: limite de 5000 bytes em UTF-8.
        QVERIFY(YouTube::sanitizeDescription(QString(3000, QChar(0x00E9))).toUtf8().size() <= 5000);
        // Tags: separadas por vírgula, sem repetidas nem vazias.
        QCOMPARE(YouTube::parseTags(QStringLiteral("League of Legends, pentakill,,PENTAKILL, <x>")),
                 QStringList({QStringLiteral("League of Legends"), QStringLiteral("pentakill"), QStringLiteral("x")}));
        QVERIFY(YouTube::parseTags(QString(600, QLatin1Char('a'))).isEmpty());
        // Range de uma resposta 308.
        QCOMPARE(YouTube::confirmedBytes("bytes=0-8388607"), qint64(8388608));
        QCOMPARE(YouTube::confirmedBytes(""), qint64(0));
    }

private:
    QTemporaryDir m_data;
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
