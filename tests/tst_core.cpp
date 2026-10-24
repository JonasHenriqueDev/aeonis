// Testes do núcleo: configurações e histórico compatíveis com a versão C#, catálogo de jogos,
// destaques, atalhos, nomes de arquivo, limite de armazenamento e o caminho segmentos -> MP4.

#include "app/Hotkeys.h"
#include "capture/Clips.h"
#include "capture/SegmentWriter.h"
#include "core/Library.h"
#include "core/Media.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "games/EventSources.h"
#include "games/GameCatalog.h"
#include "games/Highlighter.h"
#include "share/YouTube.h"
#include "ui/LibraryModel.h"

#include <QFile>
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
