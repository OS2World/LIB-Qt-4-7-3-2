/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qt_os2.h"
#include "qfont.h"
#include "qfont_p.h"
#include "qfontengine_p.h"
#include "qtextengine_p.h"
#include "qfontmetrics.h"
#include "qfontinfo.h"

QT_BEGIN_NAMESPACE

Q_GUI_EXPORT const char *qt_fontFamilyFromStyleHint(const QFontDef &request)
{
    const char *family = 0;
    switch (request.styleHint) {
    case QFont::Helvetica:
        family = "Helvetica";
        break;
    case QFont::Times:
    case QFont::OldEnglish:
        family = "Times New Roman";
        break;
    case QFont::Courier:
        family = "Courier";
        break;
    case QFont::System:
        family = "WarpSans";
        break;
    case QFont::AnyStyle:
    default:
        if (request.fixedPitch)
            family = "Courier";
        else
            family = "Helvetica";
        break;
    }
    return family;
}

/*****************************************************************************
  QFont member functions
 *****************************************************************************/

void QFont::initialize()
{
}

void QFont::cleanup()
{
    QFontCache::cleanup();
}

Qt::HANDLE QFont::handle() const
{
    // we only use the Freetype font engine (QFontEngineFT) which doesn't
    // have a concept of font handles
    return 0;
}

QString QFont::rawName() const
{
    return family();
}

void QFont::setRawName(const QString &name)
{
    setFamily(name);
}

QString QFont::defaultFamily() const
{
    return QString::fromLocal8Bit(qt_fontFamilyFromStyleHint(d->request));
}

QString QFont::lastResortFamily() const
{
    return QString::fromLatin1("Helvetica");
}

QString QFont::lastResortFont() const
{
    return QString::fromLatin1("Helvetica");
}

QT_END_NAMESPACE
