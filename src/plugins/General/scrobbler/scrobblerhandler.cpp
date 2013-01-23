/***************************************************************************
 *   Copyright (C) 2009-2012 by Ilya Kotov                                 *
 *   forkotov02@hotmail.ru                                                 *
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

#include <QSettings>
#include "lastfmscrobbler.h"
#include "librefmscrobbler.h"
#include "scrobblerhandler.h"

ScrobblerHandler::ScrobblerHandler(QObject *parent) : QObject(parent)
{
    QSettings settings(Qmmp::configFile(), QSettings::IniFormat);
    settings.beginGroup("Scrobbler");
    if(settings.value("use_lastfm", false).toBool())
    {
        new LastfmScrobbler(this);
    }
    if(settings.value("use_librefm", false).toBool())
    {
        new LibrefmScrobbler(settings.value("librefm_login").toString(),
                             settings.value("librefm_password").toString(), this);

    }
    settings.endGroup();
}

ScrobblerHandler::~ScrobblerHandler()
{}
