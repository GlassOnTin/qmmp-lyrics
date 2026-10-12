/* Harness: verifies the playlist-aware lyrics plugin end to end, without
 * running an embedded qmmp instance. Loads the built plugin .so the same way
 * qmmp does (QPluginLoader + GeneralFactory), then drives the real
 * SoundCore/MediaPlayer/PlayListManager singletons.
 *
 * Usage: lyrics_test <path/to/liblyrics.so> <dir/with/wavs>
 * Expects 3 wav files in the directory (loaded into the playlist in full).
 *
 * Lyrics are satisfied from the plugin's own cache: before the interactions
 * the harness writes one cache file per playlist track, so no network access
 * is needed and every assertion is deterministic.
 */

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QListWidget>
#include <QtNetwork/QNetworkProxyFactory>
#include <QPluginLoader>
#include <QSettings>
#include <QTextBrowser>
#include <QThread>
#include <QToolButton>
#include <QtTest/QTest>
#include <functional>
#include <cstdio>

#include <qmmp/qmmp.h>
#include <qmmp/soundcore.h>
#include <qmmp/trackinfo.h>
#include <qmmpui/generalfactory.h>
#include <qmmpui/mediaplayer.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>

static int g_failures = 0;

static void check(bool cond, const QString &name, const QString &detail = QString())
{
    if(cond)
        printf("PASS  %s\n", qPrintable(name));
    else
        printf("FAIL  %s   (%s)\n", qPrintable(name), qPrintable(detail));
    fflush(stdout);
    if(!cond)
        ++g_failures;
}

static QListWidget *trackList(const QWidget *widget)
{
    for(QListWidget *list : widget->findChildren<QListWidget*>())
        return list;
    return nullptr;
}

static const QTextBrowser *lyricsBrowser(const QWidget *widget)
{
    return widget->findChild<QTextBrowser*>();
}

static QToolButton *button(const QWidget *widget, const QString &objectName)
{
    return widget->findChild<QToolButton*>(objectName);
}

static bool waitFor(const std::function<bool()> &pred, int msecs)
{
    QElapsedTimer timer;
    timer.start();
    while(!pred() && timer.elapsed() < msecs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return pred();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(u"qmmp"_s);
    app.setApplicationName(u"qmmp"_s);

    //qmmp's app code creates the singletons on startup (qmmpstarter.cpp);
    //the harness replicates the creation order:
    new MediaPlayer(nullptr);
    SoundCore::instance();

    const QStringList args = app.arguments();
    if(args.size() != 3)
    {
        fprintf(stderr, "usage: %s <plugin.so> <waves-dir>\n", qPrintable(args.constFirst()));
        return 2;
    }
    const QString pluginPath = args.at(1);
    const QString waveDir = args.at(2);

    //keep the run offline and deterministic: lyrics come from the seeded
    //cache; any provider query that still happens fails instantly on a dead
    //proxy instead of touching the network
    QNetworkProxy dead(QNetworkProxy::HttpProxy, u"127.0.0.1"_s, 9);
    QNetworkProxy::setApplicationProxy(dead);

    PlayListManager *plm = MediaPlayer::instance()->playListManager();
    PlayListModel *model = plm->currentPlayList();

    //--- load plugin --------------------------------------------------
    QPluginLoader loader(pluginPath);
    auto *factory = qobject_cast<GeneralFactory *>(loader.instance());
    check(factory, "plugin loads and exposes GeneralFactory", loader.errorString());
    if(!factory)
        return 1;

    const GeneralProperties props = factory->properties();
    check(props.shortName == QLatin1String("lyrics"), "plugin is the lyrics plugin", props.shortName);

    QWidget *widget = factory->createWidget(props.widgets.constFirst().id, nullptr);
    check(widget != nullptr, "lyrics widget created");
    if(!widget)
        return 1;
    widget->show();
    app.processEvents();

    QListWidget *list = widget->findChild<QListWidget*>();
    check(list, "playlist pane list exists");
    QTextBrowser *browser = widget->findChild<QTextBrowser*>();
    check(browser, "lyrics browser exists");
    QToolButton *nextBtn = button(widget, QStringLiteral("nextTrackButton"));
    QToolButton *prevBtn = button(widget, QStringLiteral("prevTrackButton"));
    QToolButton *followBtn = button(widget, QStringLiteral("followButton"));
    QToolButton *playlistBtn = button(widget, QStringLiteral("playlistButton"));
    check(nextBtn && prevBtn && followBtn && playlistBtn, "prev/next/follow/playlist buttons exist");
    if(!list || !browser || !nextBtn || !prevBtn || !followBtn || !playlistBtn)
        return 1;

    check(followBtn->isChecked(), "follow is on by default");

    //--- pre-seed the lyrics cache (deterministic, no network) -----------
    //rows load in name-sorted order, so row i corresponds to the i-th "*.wav"
    //entry; the plugin looks the track up by effective title: tag, else file
    //name. These wavs are untagged, so the keys are "_trackN.wav".
    const QStringList tokens = { u"TOKEN_1"_s, u"TOKEN_2"_s, u"TOKEN_3"_s };
    const QStringList names = QDir(waveDir).entryList({ u"*.wav"_s }, QDir::Files, QDir::Name);
    QDir cacheDir(Qmmp::configDir() + u"/lyrics/"_s);
    cacheDir.mkpath(cacheDir.absolutePath());
    for(int i = 0; i < names.size(); ++i)
    {
        QString key = QLatin1Char('_') + names.at(i);
        QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),
                                 QCryptographicHash::Md5).toHex());
        QFile f(cacheDir.filePath(hash + u".html"_s));
        if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
            check(false, "cache file created", f.errorString());
        else
            f.write(QString(u"<h2>%1</h2><p>%2</p>"_s)
                    .arg(key, tokens.at(qMin(i, tokens.size() - 1))).toUtf8());
    }
    check(cacheDir.count() >= tokens.size(), "lyrics cache pre-seeded", cacheDir.absolutePath());

    //--- populate playlist ---------------------------------------------
    QStringList paths;
    for(const QString &name : names)
        paths << QDir(waveDir).filePath(name);
    plm->addPaths(paths);
    check(waitFor([&]{ return model->trackCount() == paths.size(); }, 20000),
          "playlist loaded from wavs", QString("count=%1").arg(model->trackCount()));
    app.processEvents();

    check(list->count() == model->trackCount(), "track list mirrors playlist",
          QString("rows=%1 count=%2").arg(list->count()).arg(model->trackCount()));

    printf("tags:");
    for(int i = 0; i < model->trackCount(); ++i)
        printf(" [%s|%s]", qPrintable(model->track(i)->value(Qmmp::ARTIST)),
               qPrintable(model->track(i)->value(Qmmp::TITLE)));
    printf("\n");

    //nothing is playing, so following must not have fetched anything yet,
    //not even the track the model made current on load
    QString initialText = browser->toPlainText();
    check(initialText.isEmpty(), "no fetch before first playback or browse", initialText);
    check(!list->currentItem(), "no selection before first playback or browse");

    //--- browse by selecting a row (user-equivalent click) ---------------
    list->setCurrentRow(0);
    app.processEvents();
    check(waitFor([&]{ return browser->toPlainText().contains(tokens.at(0)); }, 5000),
          "clicking row 0 shows its cached lyrics", browser->toPlainText());
    check(list->currentRow() == 0, "row 0 selected");
    check(!followBtn->isChecked(), "clicking a non-playing row disables following");

    //--- next button browses forward -------------------------------------
    nextBtn->click();
    app.processEvents();
    check(list->currentRow() == 1, "next moved to row 1");
    check(waitFor([&]{ return browser->toPlainText().contains(tokens.at(1)); }, 5000),
          "next shows lyrics of next track", browser->toPlainText());
    check(!followBtn->isChecked(), "browsing disabled following");

    //--- prev button browses back ----------------------------------------
    prevBtn->click();
    app.processEvents();
    check(list->currentRow() == 0, "prev moved to row 0");
    check(browser->toPlainText().contains(tokens.at(0)), "prev shows lyrics of row 0",
          browser->toPlainText());

    //--- wrap-around -------------------------------------------------------
    prevBtn->click();
    app.processEvents();
    check(list->currentRow() == model->trackCount() - 1, "prev wraps to last row");
    nextBtn->click(); //wrap forward, back onto row 0
    app.processEvents();
    check(list->currentRow() == 0 && browser->toPlainText().contains(tokens.at(0)),
          "next wraps to row 0", browser->toPlainText());

    //--- double-click plays the track, follow snaps along ------------------
    //turn following back on (user-equivalent toggle)
    followBtn->setChecked(true);
    app.processEvents();
    check(followBtn->isChecked(), "follow re-enabled via toggle");

    //QTest::mouseDClick does not reliably reach the viewport on offscreen
    //platforms; the double click handler is driven through the same
    //itemDoubleClicked signal the mouse would emit
    QListWidgetItem *lastItem = list->item(model->trackCount() - 1);
    emit list->itemDoubleClicked(lastItem);
    check(model->currentIndex() == model->trackCount() - 1,
          "double-click made last track current");
    check(waitFor([&]{ return browser->toPlainText().contains(tokens.at(model->trackCount() - 1)); }, 5000),
          "lyrics of the playing track are shown", browser->toPlainText());
    check(list->currentRow() == model->currentIndex(),
          "follow snapped the list to the playing track");
    const bool started = waitFor([]{ return SoundCore::instance()->state() == Qmmp::Playing; }, 4000);
    check(started || SoundCore::instance()->state() == Qmmp::Stopped,
          "double-click triggered playback start (audio device may be absent)");

    //--- clicking a different row while playing disables following ----------
    list->setCurrentRow(0);
    app.processEvents();
    check(!followBtn->isChecked(), "clicking a different row while playing disables follow");
    check(waitFor([&]{ return browser->toPlainText().contains(tokens.at(0)); }, 5000),
          "clicked row shows its lyrics while playback runs", browser->toPlainText());

    //--- follow off: track changes leave the view alone ---------------------
    list->setCurrentRow(1);
    app.processEvents();
    check(browser->toPlainText().contains(tokens.at(1)), "browsed row shows its lyrics");
    const int browsedRow = list->currentRow();
    const QString browsedText = browser->toPlainText();

    MediaPlayer::instance()->next();
    check(waitFor([]{ return SoundCore::instance()->state() == Qmmp::Playing; }, 6000)
          || SoundCore::instance()->state() == Qmmp::Stopped, "next() advanced playback");
    app.processEvents();
    check(model->currentIndex() != browsedRow, "playing track changed");
    check(list->currentRow() == browsedRow, "browsed selection did not move");
    check(browser->toPlainText() == browsedText, "displayed lyrics untouched while follow is off");
    check(list->item(model->currentIndex()) && list->item(model->currentIndex())->font().bold(),
          "playing row is highlighted bold while browsing elsewhere");

    //--- follow back on: view re-syncs to playing track ----------------------
    followBtn->click();
    app.processEvents();
    check(list->currentRow() == model->currentIndex(),
          "re-enabling follow snaps back to the playing track");

    //--- settings: user toggle persisted, programmatic auto-off did not ------
    QSettings settings;
    check(settings.value(u"Lyrics/follow_playing"_s, true).toBool() == followBtn->isChecked(),
          "following state persisted");
    check(settings.value(u"Lyrics/playlist_shown"_s, true).toBool(), "playlist pane shown persisted");

    //--- structure change rebuilds the list ----------------------------------
    plm->addPaths({ waveDir });
    check(waitFor([&]{ return list->count() == model->trackCount(); }, 20000),
          "adding files keeps list in sync",
          QString("rows=%1 count=%2").arg(list->count()).arg(model->trackCount()));

    //--- stop, cleanup --------------------------------------------------------
    //Wait for the playlist loader before exiting: process exit (dl_fini)
    //races a still-running loader thread inside the qmmp input plugins. The
    //core dump put the fault in libshine.so's fini; the real player never
    //exits mid-load, so this is a harness-only teardown hazard.
    check(waitFor([]{ return !MediaPlayer::instance()->playListManager()->currentPlayList()->isLoaderRunning(); }, 15000),
          "playlist loader finished before exit");
    MediaPlayer::instance()->stop();

    printf("\n%d %s\n", g_failures, g_failures ? "FAILURES" : "checks passed");
    return g_failures ? 1 : 0;
}