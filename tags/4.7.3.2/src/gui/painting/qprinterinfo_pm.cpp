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

#include "qprinterinfo.h"

#if !defined(QT_NO_CUPS)
#  include <private/qcups_p.h>
#  include <cups/cups.h>
#  include <private/qpdf_p.h>
#endif

QT_BEGIN_NAMESPACE

#ifndef QT_NO_PRINTER

class QPrinterInfoPrivate
{
Q_DECLARE_PUBLIC(QPrinterInfo)
public:
    QPrinterInfoPrivate();
    QPrinterInfoPrivate(const QString& name);
    ~QPrinterInfoPrivate();

    static QPrinter::PaperSize string2PaperSize(const QString& str);
    static QString pageSize2String(QPrinter::PaperSize size);

private:
    QString                     m_name;
    bool                        m_isNull;
    bool                        m_default;
    mutable bool                m_mustGetPaperSizes;
    mutable QList<QPrinter::PaperSize> m_paperSizes;
    int                         m_cupsPrinterIndex;

    QPrinterInfo*               q_ptr;
};

static QPrinterInfoPrivate nullQPrinterInfoPrivate;

class QPrinterInfoPrivateDeleter
{
public:
    static inline void cleanup(QPrinterInfoPrivate *d)
    {
        if (d != &nullQPrinterInfoPrivate)
            delete d;
    }
};

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

QList<QPrinterInfo> QPrinterInfo::availablePrinters()
{
    QList<QPrinterInfo> list;

#if !defined(QT_NO_CUPS)
    QCUPSSupport cups;
    if (QCUPSSupport::isAvailable()) {
        //const ppd_file_t* cupsPPD = cups.currentPPD();
        int cupsPrinterCount = cups.availablePrintersCount();
        const cups_dest_t* cupsPrinters = cups.availablePrinters();

        for (int i = 0; i < cupsPrinterCount; ++i) {
            QString printerName(QString::fromLocal8Bit(cupsPrinters[i].name));
            if (cupsPrinters[i].instance)
                printerName += QLatin1Char('/') + QString::fromLocal8Bit(cupsPrinters[i].instance);
            list.append(QPrinterInfo(printerName));
            if (cupsPrinters[i].is_default)
                list[i].d_ptr->m_default = true;
            list[i].d_ptr->m_cupsPrinterIndex = i;
        }
    }
#endif

    return list;
}

QPrinterInfo QPrinterInfo::defaultPrinter()
{
    QList<QPrinterInfo> prnList = availablePrinters();
    for (int i = 0; i < prnList.size(); ++i) {
        if (prnList[i].isDefault())
            return prnList[i];
    }
    return (prnList.size() > 0) ? prnList[0] : QPrinterInfo();
}

QPrinterInfo::QPrinterInfo()
    : d_ptr(&nullQPrinterInfoPrivate)
{
}

QPrinterInfo::QPrinterInfo(const QPrinterInfo& src)
    : d_ptr(&nullQPrinterInfoPrivate)
{
    *this = src;
}

QPrinterInfo::QPrinterInfo(const QPrinter& printer)
    : d_ptr(new QPrinterInfoPrivate(printer.printerName()))
{

    Q_D(QPrinterInfo);
    d->q_ptr = this;

#if !defined(QT_NO_CUPS)
    QCUPSSupport cups;
    if (QCUPSSupport::isAvailable()) {
        int cupsPrinterCount = cups.availablePrintersCount();
        const cups_dest_t* cupsPrinters = cups.availablePrinters();

        for (int i = 0; i < cupsPrinterCount; ++i) {
            QString printerName(QString::fromLocal8Bit(cupsPrinters[i].name));
            if (cupsPrinters[i].instance)
                printerName += QLatin1Char('/') + QString::fromLocal8Bit(cupsPrinters[i].instance);
            if (printerName == printer.printerName()) {
                if (cupsPrinters[i].is_default)
                    d->m_default = true;
                d->m_cupsPrinterIndex = i;
                return;
            }
        }
    }
#endif

    // Printer not found.
    d_ptr.reset(&nullQPrinterInfoPrivate);
}

QPrinterInfo::QPrinterInfo(const QString& name)
    : d_ptr(new QPrinterInfoPrivate(name))
{
    d_ptr->q_ptr = this;
}

QPrinterInfo::~QPrinterInfo()
{
}

QPrinterInfo& QPrinterInfo::operator=(const QPrinterInfo& src)
{
    Q_ASSERT(d_ptr);
    d_ptr.reset(new QPrinterInfoPrivate(*src.d_ptr));
    d_ptr->q_ptr = this;
    return *this;
}

QString QPrinterInfo::printerName() const
{
    const Q_D(QPrinterInfo);
    return d->m_name;
}

bool QPrinterInfo::isNull() const
{
    const Q_D(QPrinterInfo);
    return d->m_isNull;
}

bool QPrinterInfo::isDefault() const
{
    const Q_D(QPrinterInfo);
    return d->m_default;
}

QList< QPrinter::PaperSize> QPrinterInfo::supportedPaperSizes() const
{
    const Q_D(QPrinterInfo);
    if (d->m_mustGetPaperSizes) {
        d->m_mustGetPaperSizes = false;

#if !defined(QT_NO_CUPS)
        QCUPSSupport cups;
        if (QCUPSSupport::isAvailable()) {
            // Find paper sizes from CUPS.
            cups.setCurrentPrinter(d->m_cupsPrinterIndex);
            const ppd_option_t* sizes = cups.pageSizes();
            if (sizes) {
                for (int j = 0; j < sizes->num_choices; ++j) {
                    d->m_paperSizes.append(
                        QPrinterInfoPrivate::string2PaperSize(
                            QLatin1String(sizes->choices[j].choice)));
                }
            }
        }
#endif

    }
    return d->m_paperSizes;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

QPrinterInfoPrivate::QPrinterInfoPrivate()
{
    m_isNull = true;
    m_default = false;
    m_mustGetPaperSizes = true;
    m_cupsPrinterIndex = 0;
    q_ptr = 0;
}

QPrinterInfoPrivate::QPrinterInfoPrivate(const QString& name)
{
    m_name = name;
    m_isNull = false;
    m_default = false;
    m_mustGetPaperSizes = true;
    m_cupsPrinterIndex = 0;
    q_ptr = 0;
}

QPrinterInfoPrivate::~QPrinterInfoPrivate()
{
}

QPrinter::PaperSize QPrinterInfoPrivate::string2PaperSize(const QString& str)
{
    if (str == QLatin1String("A4")) {
        return QPrinter::A4;
    } else if (str == QLatin1String("B5")) {
        return QPrinter::B5;
    } else if (str == QLatin1String("Letter")) {
        return QPrinter::Letter;
    } else if (str == QLatin1String("Legal")) {
        return QPrinter::Legal;
    } else if (str == QLatin1String("Executive")) {
        return QPrinter::Executive;
    } else if (str == QLatin1String("A0")) {
        return QPrinter::A0;
    } else if (str == QLatin1String("A1")) {
        return QPrinter::A1;
    } else if (str == QLatin1String("A2")) {
        return QPrinter::A2;
    } else if (str == QLatin1String("A3")) {
        return QPrinter::A3;
    } else if (str == QLatin1String("A5")) {
        return QPrinter::A5;
    } else if (str == QLatin1String("A6")) {
        return QPrinter::A6;
    } else if (str == QLatin1String("A7")) {
        return QPrinter::A7;
    } else if (str == QLatin1String("A8")) {
        return QPrinter::A8;
    } else if (str == QLatin1String("A9")) {
        return QPrinter::A9;
    } else if (str == QLatin1String("B0")) {
        return QPrinter::B0;
    } else if (str == QLatin1String("B1")) {
        return QPrinter::B1;
    } else if (str == QLatin1String("B10")) {
        return QPrinter::B10;
    } else if (str == QLatin1String("B2")) {
        return QPrinter::B2;
    } else if (str == QLatin1String("B3")) {
        return QPrinter::B3;
    } else if (str == QLatin1String("B4")) {
        return QPrinter::B4;
    } else if (str == QLatin1String("B6")) {
        return QPrinter::B6;
    } else if (str == QLatin1String("B7")) {
        return QPrinter::B7;
    } else if (str == QLatin1String("B8")) {
        return QPrinter::B8;
    } else if (str == QLatin1String("B9")) {
        return QPrinter::B9;
    } else if (str == QLatin1String("C5E")) {
        return QPrinter::C5E;
    } else if (str == QLatin1String("Comm10E")) {
        return QPrinter::Comm10E;
    } else if (str == QLatin1String("DLE")) {
        return QPrinter::DLE;
    } else if (str == QLatin1String("Folio")) {
        return QPrinter::Folio;
    } else if (str == QLatin1String("Ledger")) {
        return QPrinter::Ledger;
    } else if (str == QLatin1String("Tabloid")) {
        return QPrinter::Tabloid;
    } else {
        return QPrinter::Custom;
    }
}

QString QPrinterInfoPrivate::pageSize2String(QPrinter::PaperSize size)
{
    switch (size) {
    case QPrinter::A4:
        return QLatin1String("A4");
    case QPrinter::B5:
        return QLatin1String("B5");
    case QPrinter::Letter:
        return QLatin1String("Letter");
    case QPrinter::Legal:
        return QLatin1String("Legal");
    case QPrinter::Executive:
        return QLatin1String("Executive");
    case QPrinter::A0:
        return QLatin1String("A0");
    case QPrinter::A1:
        return QLatin1String("A1");
    case QPrinter::A2:
        return QLatin1String("A2");
    case QPrinter::A3:
        return QLatin1String("A3");
    case QPrinter::A5:
        return QLatin1String("A5");
    case QPrinter::A6:
        return QLatin1String("A6");
    case QPrinter::A7:
        return QLatin1String("A7");
    case QPrinter::A8:
        return QLatin1String("A8");
    case QPrinter::A9:
        return QLatin1String("A9");
    case QPrinter::B0:
        return QLatin1String("B0");
    case QPrinter::B1:
        return QLatin1String("B1");
    case QPrinter::B10:
        return QLatin1String("B10");
    case QPrinter::B2:
        return QLatin1String("B2");
    case QPrinter::B3:
        return QLatin1String("B3");
    case QPrinter::B4:
        return QLatin1String("B4");
    case QPrinter::B6:
        return QLatin1String("B6");
    case QPrinter::B7:
        return QLatin1String("B7");
    case QPrinter::B8:
        return QLatin1String("B8");
    case QPrinter::B9:
        return QLatin1String("B9");
    case QPrinter::C5E:
        return QLatin1String("C5E");
    case QPrinter::Comm10E:
        return QLatin1String("Comm10E");
    case QPrinter::DLE:
        return QLatin1String("DLE");
    case QPrinter::Folio:
        return QLatin1String("Folio");
    case QPrinter::Ledger:
        return QLatin1String("Ledger");
    case QPrinter::Tabloid:
        return QLatin1String("Tabloid");
    default:
        return QLatin1String("Custom");
    }
}

#endif // QT_NO_PRINTER

QT_END_NAMESPACE
