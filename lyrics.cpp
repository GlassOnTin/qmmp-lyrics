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

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <qmmp/soundcore.h>
#include <qmmpui/uihelper.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistitem.h>
#include <qmmpui/mediaplayer.h>
#include "lyricswidget.h"
#include "lyrics.h"

Lyrics::Lyrics(QPointer<LyricsWidget> *lyricsWidget, QObject *parent) : QObject(parent)
{
    m_lyricsWidget = lyricsWidget;
    m_action = new QAction(tr("View Lyrics"), this);
    m_action->setShortcut(tr("Ctrl+L"));
    UiHelper::instance()->addAction(m_action, UiHelper::PLAYLIST_MENU);
    connect(m_action, &QAction::triggered, this, &Lyrics::showLyrics);
}

Lyrics::~Lyrics()
{}

TrackInfo Lyrics::lyricsTarget() const
{
    PlayListManager *pl_manager = MediaPlayer::instance()->playListManager();
    PlayListModel *model = pl_manager->currentPlayList();

    //preferred target: the playing track; then the first selected track
    //(former behaviour); then the first playlist track
    PlayListTrack *track = nullptr;
    Qmmp::State state = SoundCore::instance()->state();
    if(state == Qmmp::Playing || state == Qmmp::Paused)
        track = model->currentTrack();

    if(!track || track->value(Qmmp::ARTIST).isEmpty() || track->value(Qmmp::TITLE).isEmpty())
    {
        QList<PlayListTrack *> selected = pl_manager->selectedPlayList()->selectedTracks();
        if(!selected.isEmpty())
            track = selected.constFirst();
        else if(model->trackCount() > 0)
            track = model->track(0);
        else
            track = nullptr;
    }

    if(!track)
        return TrackInfo();

    TrackInfo info(*track); //prefer the playlist entry, it contains all metadata
    if(info.value(Qmmp::TITLE).isEmpty())
        info.setValue(Qmmp::TITLE, QFileInfo(TrackInfo::pathFromUrl(track->path())).fileName());
    return info;
}

void Lyrics::showLyrics()
{
    PlayListManager *pl_manager = MediaPlayer::instance()->playListManager();
    TrackInfo target = lyricsTarget();
    if (target.value(Qmmp::ARTIST).isEmpty() || target.value(Qmmp::TITLE).isEmpty())
        return;

    if(!m_lyricsWidget->isNull() && m_lyricsWidget->data()->isVisible())
    {
        m_lyricsWidget->data()->fetch(&target);
    }
    else
    {
        LyricsWidget *w = new LyricsWidget(true, qApp->activeWindow());
        w->fetch(&target);
        w->show();
    }
}