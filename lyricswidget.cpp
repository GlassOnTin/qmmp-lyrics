/***************************************************************************
 *   Copyright (C) 2009-2026 by Ilya Kotov                                 *
 *   forkotov02@ya.ru                                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QListWidget>
#include <QSettings>
#include <QCryptographicHash>
#include <utility>
#include <qmmp/metadatamanager.h>
#include <qmmp/qmmpsettings.h>
#include <qmmp/qmmp.h>
#include <qmmp/soundcore.h>
#include <qmmpui/mediaplayer.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include <qmmpui/playlistitem.h>
#include "ui_lyricswidget.h"
#include "lyricswidget.h"

//Title actually used for lyric lookups: untagged tracks fall back to the file
//name, the same value the playlist pane displays.
static QString displayTitle(const PlayListTrack *track)
{
    QString title = track->value(Qmmp::TITLE);
    if(title.isEmpty())
        title = QFileInfo(TrackInfo::pathFromUrl(track->path())).fileName();
    return title;
}

//whether a track is on; PlayListModel sets currentIndex=0 as soon as the list
//becomes non-empty, which alone means nothing to follow
static bool playbackActive()
{
    Qmmp::State state = SoundCore::instance()->state();
    return state == Qmmp::Playing || state == Qmmp::Paused;
}

LyricsWidget::LyricsWidget(bool dialog, QWidget *parent) : QWidget(parent),
    m_ui(new Ui::LyricsWidget)
{
    m_ui->setupUi(this);

    if(dialog)
    {
        setWindowFlags(Qt::Dialog);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_QuitOnClose, false);
    }
    else
    {
        m_ui->buttonBox->hide();
    }

    m_cachePath = Qmmp::configDir() + u"/lyrics/"_s;
    m_ui->editWidget->setVisible(false);

    m_http = new QNetworkAccessManager(this);
    m_http->setTransferTimeout(10000);
     //load global proxy settings
    QmmpSettings *gs = QmmpSettings::instance();
    if (gs->isProxyEnabled())
    {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy, gs->proxy().host(),  gs->proxy().port());
        if(gs->proxyType() == QmmpSettings::SOCKS5_PROXY)
            proxy.setType(QNetworkProxy::Socks5Proxy);
        if(gs->useProxyAuth())
        {
            proxy.setUser(gs->proxy().userName());
            proxy.setPassword(gs->proxy().password());
        }
        m_http->setProxy(proxy);
    }
    connect(m_http, &QNetworkAccessManager::finished, this, &LyricsWidget::onRequestFinished);

    if(!m_parser.load(u":/ultimate_providers.xml"_s))
    {
        qCWarning(plugin, "unable to load ultimate_providers.xml");
        m_ui->textBrowser->setText(m_parser.errorString());
        return;
    }

    QSettings settings;
    m_enabledProviders = settings.value(u"Lyrics/enabled_providers"_s, m_parser.defaultProviders()).toStringList();
    m_fetchByRequest = settings.value(u"Lyrics/fetch_by_request"_s, false).toBool();
    m_follow = settings.value(u"Lyrics/follow_playing"_s, true).toBool();
    bool showPlaylist = settings.value(u"Lyrics/playlist_shown"_s, true).toBool();

    if(dialog)
        restoreGeometry(settings.value(u"Lyrics/geometry"_s).toByteArray());

    //restore the playlist pane state
    m_ui->playlistButton->blockSignals(true);
    m_ui->playlistButton->setChecked(showPlaylist);
    m_ui->playlistButton->blockSignals(false);
    m_ui->trackListWidget->setVisible(showPlaylist);
    QByteArray splitterState = settings.value(u"Lyrics/splitter_state"_s).toByteArray();
    if(!splitterState.isEmpty())
        m_ui->trackSplitter->restoreState(splitterState);

    //playlist pane
    connect(m_ui->prevTrackButton, &QToolButton::clicked, this, &LyricsWidget::onPrevTrackClicked);
    connect(m_ui->nextTrackButton, &QToolButton::clicked, this, &LyricsWidget::onNextTrackClicked);
    connect(m_ui->trackListWidget, &QListWidget::currentRowChanged, this, &LyricsWidget::onTrackClicked);
    connect(m_ui->trackListWidget, &QListWidget::itemDoubleClicked, this, &LyricsWidget::onTrackDoubleClicked);
    connect(m_ui->followButton, &QToolButton::toggled, this, &LyricsWidget::onFollowToggled);
    connect(m_ui->playlistButton, &QToolButton::toggled, this, &LyricsWidget::onPlaylistPaneToggled);

    m_ui->followButton->blockSignals(true);
    m_ui->followButton->setChecked(m_follow);
    m_ui->followButton->blockSignals(false);

    //playlist tracking
    PlayListManager *pl_manager = MediaPlayer::instance()->playListManager();
    connect(pl_manager, &PlayListManager::currentPlayListChanged, this, &LyricsWidget::onCurrentPlayListChanged);
    connect(pl_manager, &PlayListManager::selectedPlayListChanged, this, &LyricsWidget::onSelectedPlayListChanged);
    connect(SoundCore::instance(), &SoundCore::trackInfoChanged, this, &LyricsWidget::onTrackInfoChanged);
    rebindModel(pl_manager->currentPlayList());

    QDir cacheDir(m_cachePath);
    if(!cacheDir.exists())
    {
        if(!cacheDir.mkpath(cacheDir.absolutePath()))
            qCWarning(plugin, "unable to create cache directory");
    }
}

LyricsWidget::~LyricsWidget()
{
    delete m_ui;
    qCDebug(plugin) << Q_FUNC_INFO;
}

void LyricsWidget::fetch(const TrackInfo *info)
{
    m_ui->titleLineEdit->setText(info->value(Qmmp::TITLE));
    m_ui->artistLineEdit->setText(info->value(Qmmp::ARTIST));
    m_ui->albumLineEdit->setText(info->value(Qmmp::ALBUM));
    m_ui->trackSpinBox->setValue(info->value(Qmmp::TRACK).toInt());
    m_ui->yearSpinBox->setValue(info->value(Qmmp::YEAR).toInt());

    m_ui->providerComboBox->clear();

    if(!loadFromTag(info->path()) && !loadFromCache())
    {
        if(m_fetchByRequest)
        {
            m_ui->textBrowser->setHtml(QStringLiteral("<a href=\"load://\">%1</a>").arg(tr("Fetch lyrics from the Internet")));
        }
        else
        {
            on_refreshButton_clicked();
        }
    }
}

//Playlist tracks with no title tag get their file name as the lookup title
void LyricsWidget::fetch(const PlayListTrack *track)
{
    TrackInfo info(*track);
    info.setValue(Qmmp::TITLE, displayTitle(track));
    fetch(&info);
}

QString LyricsWidget::cacheFilePath() const
{
    QString name = m_ui->artistLineEdit->text() + QLatin1Char('_') + m_ui->titleLineEdit->text();
    QByteArray hash = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Md5);
    return m_cachePath + QString::fromLatin1(hash.toHex()) + u".html"_s;
}

void LyricsWidget::onRequestFinished(QNetworkReply *reply)
{
    m_tasks.removeAll(reply);
    QString name = reply->property("provider").toString();
    if(name.isEmpty() || reply->error() == QNetworkReply::OperationCanceledError)
    {
        reply->deleteLater();
        return;
    }
    QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if(reply->error() == QNetworkReply::NoError && code == 200)
    {
        QByteArray data = reply->readAll();
        LyricsProvider *provider = m_parser.provider(name);
        if(provider)
        {
            QString content = provider->format(data, m_info);

            if(content.startsWith(u"http:"_s) || content.startsWith(u"https:"_s))
            {
                QNetworkRequest request;
                request.setUrl(content);
                request.setRawHeader("User-Agent", QStringLiteral("qmmp/%1").arg(Qmmp::strVersion()).toLatin1());
                m_tasks << m_http->get(request);
                m_tasks.last()->setProperty("provider", provider->name());
                provider->skipRules(true);
            }
            else if(!content.isEmpty())
            {
                content.prepend(tr("<h2>%1 - %2</h2>").arg(m_info.value(Qmmp::ARTIST), m_info.value(Qmmp::TITLE)));
                m_ui->providerComboBox->addItem(name, content);
                if(m_ui->providerComboBox->count() == 1)
                {
                    m_ui->providerComboBox->setCurrentIndex(0);
                    m_ui->textBrowser->setHtml(content);
                    saveToCache(content);
                }
            }
            else if(m_tasks.isEmpty() && m_ui->providerComboBox->count() == 0)
            {
                m_ui->textBrowser->setHtml(u"<b>"_s + tr("Not found") + u"</b>"_s);
            }
        }
    }
    else if(redirectTarget.isValid())
    {
        m_tasks << m_http->get(QNetworkRequest(redirectTarget.toUrl()));
        m_tasks.last()->setProperty("provider", name);
    }
    else if(m_tasks.isEmpty() && m_ui->providerComboBox->count() == 0)
    {
        m_ui->textBrowser->clear();
        m_ui->textBrowser->setHtml(tr("Error: %1 - %2").arg(code).arg(reply->errorString()));
        qCWarning(plugin) << "error:" << reply->errorString();
    }
    else
    {
        qCWarning(plugin) << "error:" << reply->errorString();
    }

    reply->deleteLater();
}

void LyricsWidget::on_refreshButton_clicked()
{
    m_ui->textBrowser->setHtml(QStringLiteral("<b>%1</b>").arg(tr("Receiving")));
    m_ui->providerComboBox->clear();

    m_info.clear();
    m_info.setValue(Qmmp::TITLE,  m_ui->titleLineEdit->text());
    m_info.setValue(Qmmp::ARTIST, m_ui->artistLineEdit->text());
    m_info.setValue(Qmmp::ALBUM, m_ui->albumLineEdit->text());
    m_info.setValue(Qmmp::TRACK, m_ui->trackSpinBox->value());
    m_info.setValue(Qmmp::YEAR, m_ui->yearSpinBox->value());

    //abort previous tasks
    for(QNetworkReply *reply : std::as_const(m_tasks))
        reply->abort();

    m_tasks.clear();

    for(LyricsProvider *provider : m_parser.providers())
    {
        if(m_enabledProviders.contains(provider->name()))
        {
            QString url = provider->getUrl(m_info);
            QNetworkRequest request;
            request.setUrl(url);
            request.setRawHeader("User-Agent", QStringLiteral("qmmp/%1").arg(Qmmp::strVersion()).toLatin1());
            m_tasks << m_http->get(request);
            m_tasks.last()->setProperty("provider", provider->name());
            provider->skipRules(false);
        }
    }
}

void LyricsWidget::on_editButton_clicked(bool checked)
{
    m_ui->editWidget->setVisible(checked);
}

void LyricsWidget::on_providerComboBox_activated(int index)
{
    m_ui->textBrowser->setHtml(m_ui->providerComboBox->itemData(index).toString());
}

void LyricsWidget::on_textBrowser_anchorClicked(const QUrl &url)
{
    if(url.scheme() == QLatin1String("load"))
        on_refreshButton_clicked();
}

bool LyricsWidget::loadFromTag(const QString &path)
{
    MetaDataModel *model = MetaDataManager::instance()->createMetaDataModel(path, true);
    if(!model)
        return false;
    QString content = model->lyrics();
    delete model;

    if(!content.isEmpty())
    {
        content.replace(u"\r\n"_s, u"<br>"_s);
        content.replace(u"\n"_s, u"<br>"_s);
        content.prepend(tr("<h2>%1 - %2</h2>").arg(m_ui->artistLineEdit->text(), m_ui->titleLineEdit->text()));
        m_ui->textBrowser->setHtml(content);
        m_ui->providerComboBox->addItem(tr("Tag"));
        return true;
    }

    return false;
}

bool LyricsWidget::loadFromCache()
{
    QFile file(cacheFilePath());
    if(!file.exists())
        return false;

    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qCWarning(plugin, "unable to open cache file '%s', error: %s",
                 qPrintable(file.fileName()), qPrintable(file.errorString()));
        return false;
    }

    m_ui->textBrowser->setHtml(QString::fromUtf8(file.readAll()));
    m_ui->providerComboBox->addItem(tr("Cache"));
    return true;
}

void LyricsWidget::saveToCache(const QString &text)
{
    QFile file(cacheFilePath());
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qCWarning(plugin, "unable to open cache file '%s', error: %s",
                 qPrintable(file.fileName()), qPrintable(file.errorString()));
        return;
    }
    file.write(text.toUtf8());
}

//*************************************************************************
// Playlist pane
//*************************************************************************

void LyricsWidget::rebindModel(PlayListModel *model)
{
    for(const QMetaObject::Connection &c : std::as_const(m_modelConnections))
        disconnect(c);
    m_modelConnections.clear();

    m_model = model;
    if(m_model)
        m_modelConnections << connect(m_model, &PlayListModel::listChanged, this, &LyricsWidget::onTrackListChanged);

    updateTrackList();
}

void LyricsWidget::updateTrackList()
{
    QListWidget *list = m_ui->trackListWidget;
    int browsed = list->currentRow();
    int count = m_model ? m_model->trackCount() : 0;
    int current = m_model ? m_model->currentIndex() : -1;

    list->blockSignals(true);
    list->clear();

    QFont regular = font();
    QFont bold = font();
    bold.setBold(true);

    for(int i = 0; i < count; ++i)
    {
        PlayListTrack *track = m_model->track(i);
        QString title = track->value(Qmmp::TITLE);
        QString artist = track->value(Qmmp::ARTIST);
        if(title.isEmpty())
            title = QFileInfo(TrackInfo::pathFromUrl(track->path())).fileName();
        else if(artist.isEmpty())
            artist = tr("[unknown artist]");

        QString text;
        if(artist.isEmpty())
            text = title;
        else
            text = artist + u" - "_s + title;

        QString duration = track->formattedDuration();
        if(!duration.isEmpty())
            text += u" ("_s + duration + u")"_s;

        QListWidgetItem *item = new QListWidgetItem(u"%1. %2"_s.arg(i + 1).arg(text), list);
        item->setToolTip(track->path());
        item->setFont(i == current ? bold : regular);
    }

    if(count == 0)
    {
        QListWidgetItem *item = new QListWidgetItem(tr("No tracks"), list);
        item->setFlags(Qt::NoItemFlags);
    }
    list->blockSignals(false);

    m_ui->prevTrackButton->setEnabled(count > 0);
    m_ui->nextTrackButton->setEnabled(count > 0);

    //keep a browsed track in view after the list was rebuilt
    if(m_follow && playbackActive())
        highlightCurrent(true);
    else if(browsed >= 0)
    {
        list->blockSignals(true);
        list->setCurrentRow(qMin(browsed, count - 1));
        list->blockSignals(false);
    }
}

void LyricsWidget::highlightCurrent(bool sync)
{
    if(!m_model)
        return;

    //bold marker on the current/playing track
    int current = m_model->currentIndex();
    QFont regular = font();
    QFont bold = font();
    bold.setBold(true);
    for(int i = 0; i < m_model->trackCount(); ++i)
    {
        if(QListWidgetItem *item = m_ui->trackListWidget->item(i))
            item->setFont(i == current ? bold : regular);
    }

    //syncing moves the selection and looks up lyrics; while following is off
    //(browsing), the marker alone is updated and the view stays put. Nothing
    //is playing yet -> nothing to mirror
    if(!sync || m_model->trackCount() == 0 || current < 0)
        return;

    int row = qMin(current, m_model->trackCount() - 1);
    QListWidget *list = m_ui->trackListWidget;
    if(list->currentRow() != row)
    {
        list->blockSignals(true);
        list->setCurrentRow(row);
        list->blockSignals(false);
    }
    list->scrollToItem(list->item(row), QAbstractItemView::EnsureVisible);

    //show lyrics of the current track, unless it is already displayed
    if(PlayListTrack *track = m_model->track(row);
       m_ui->artistLineEdit->text() != track->value(Qmmp::ARTIST)
       || m_ui->titleLineEdit->text() != displayTitle(track))
    {
        fetch(track);
    }
}

void LyricsWidget::onTrackListChanged(int flags)
{
    if(flags & (PlayListModel::STRUCTURE | PlayListModel::METADATA))
        updateTrackList();
    else if(flags & PlayListModel::CURRENT)
    {
        if(m_follow && playbackActive())
            highlightCurrent(true);
        else
            highlightCurrent(false); //restyle only, keep browsed lyrics
    }
}

void LyricsWidget::onTrackInfoChanged()
{
    if(m_follow)
        highlightCurrent(true);
}

void LyricsWidget::onCurrentPlayListChanged()
{
    rebindModel(MediaPlayer::instance()->playListManager()->currentPlayList());
}

void LyricsWidget::onSelectedPlayListChanged()
{
    //mirror the playlist the user is looking at, but only while nothing plays
    Qmmp::State state = SoundCore::instance()->state();
    if(state != Qmmp::Playing && state != Qmmp::Paused)
        rebindModel(MediaPlayer::instance()->playListManager()->selectedPlayList());
}

void LyricsWidget::onPrevTrackClicked()
{
    browseRow(-1);
}

void LyricsWidget::onNextTrackClicked()
{
    browseRow(1);
}

void LyricsWidget::browseRow(int direction)
{
    int count = m_model ? m_model->trackCount() : 0;
    if(count == 0)
        return;

    setFollow(false, false); //browsing is not following

    int row = m_ui->trackListWidget->currentRow();
    if(row < 0)
        row = 0;
    else
    {
        row += direction;
        if(row >= count)
            row = 0;
        if(row < 0)
            row = count - 1;
    }
    fetch(m_model->track(row));

    QListWidget *list = m_ui->trackListWidget;
    list->blockSignals(true);
    list->setCurrentRow(row);
    list->blockSignals(false);
    list->scrollToItem(list->item(row), QAbstractItemView::EnsureVisible);
}

void LyricsWidget::onTrackClicked(int row)
{
    if(row < 0)
        return;

    if(playingRow() != row)   //picking another track turns following off
        setFollow(false, false);

    if(m_model && row < m_model->trackCount())
        fetch(m_model->track(row));
}

void LyricsWidget::onTrackDoubleClicked(QListWidgetItem *item)
{
    if(!m_model || !item)
        return;
    int row = m_ui->trackListWidget->row(item);
    PlayListTrack *track = m_model->track(row);
    if(!track)
        return;
    m_model->setCurrent(track);
    MediaPlayer::instance()->play();
}

void LyricsWidget::onFollowToggled(bool on)
{
    setFollow(on, true);
    if(on)
        highlightCurrent(true);
}

void LyricsWidget::onPlaylistPaneToggled(bool visible)
{
    m_ui->trackListWidget->setVisible(visible);
    QSettings settings;
    settings.setValue(u"Lyrics/playlist_shown"_s, visible);
}

void LyricsWidget::setFollow(bool on, bool userAction)
{
    if(m_follow == on)
        return;

    m_follow = on;
    m_ui->followButton->blockSignals(true);
    m_ui->followButton->setChecked(on);
    m_ui->followButton->blockSignals(false);

    if(userAction)
    {
        QSettings settings;
        settings.setValue(u"Lyrics/follow_playing"_s, on);
    }
}

void LyricsWidget::closeEvent(QCloseEvent *)
{
    QSettings settings;
    if(windowFlags() & Qt::Dialog)
        settings.setValue(u"Lyrics/geometry"_s, saveGeometry());
    settings.setValue(u"Lyrics/splitter_state"_s, m_ui->trackSplitter->saveState());
}

//The row a click keeps following: only meaningful while a track is actually
//playing (or paused); -1 otherwise, so every click counts as browsing
int LyricsWidget::playingRow() const
{
    if(!m_model)
        return -1;
    Qmmp::State state = SoundCore::instance()->state();
    if(state != Qmmp::Playing && state != Qmmp::Paused)
        return -1;
    return m_model->currentIndex();
}
