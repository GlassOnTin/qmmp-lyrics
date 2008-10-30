/***************************************************************************
 *   Copyright (C) 2008 by Ilya Kotov                                      *
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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef AACFILE_H
#define AACFILE_H


#include <QMap>
#include <QString>

#include <qmmp/qmmp.h>

class QIODevice;
class TagExtractor;

/**
    @author Ilya Kotov <forkotov02@hotmail.ru>
*/
class AACFile
{
public:
    AACFile(QIODevice *i);

    ~AACFile();

    qint64 length();
    quint32 bitrate();
    bool isValid();
    const QMap<Qmmp::MetaData, QString> metaData();

private:
    void parseADTS();
    qint64 m_length;
    quint32 m_bitrate;
    QIODevice *m_input;
    bool m_isValid;
    QMap<Qmmp::MetaData, QString> m_metaData;
    TagExtractor *m_ext;
};

#endif
