/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
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

#ifndef QMIME_H
#define QMIME_H

#include <QtCore/qmimedata.h>

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE

QT_MODULE(Gui)

class Q_GUI_EXPORT QMimeSource
{
public:
    virtual ~QMimeSource();
    virtual const char* format(int n = 0) const = 0;
    virtual bool provides(const char*) const;
    virtual QByteArray encodedData(const char*) const = 0;
};


#if defined(Q_WS_WIN)

QT_BEGIN_INCLUDE_NAMESPACE
typedef struct tagFORMATETC FORMATETC;
typedef struct tagSTGMEDIUM STGMEDIUM;
struct IDataObject;

#include <QtCore/qvariant.h>
QT_END_INCLUDE_NAMESPACE

/*
  Encapsulation of conversion between MIME and Windows CLIPFORMAT.
  Not need on X11, as the underlying protocol uses the MIME standard
  directly.
*/

class Q_GUI_EXPORT QWindowsMime
{
public:
    QWindowsMime();
    virtual ~QWindowsMime();

    // for converting from Qt
    virtual bool canConvertFromMime(const FORMATETC &formatetc, const QMimeData *mimeData) const = 0;
    virtual bool convertFromMime(const FORMATETC &formatetc, const QMimeData *mimeData, STGMEDIUM * pmedium) const = 0;
    virtual QVector<FORMATETC> formatsForMime(const QString &mimeType, const QMimeData *mimeData) const = 0;

    // for converting to Qt
    virtual bool canConvertToMime(const QString &mimeType, IDataObject *pDataObj) const = 0;
    virtual QVariant convertToMime(const QString &mimeType, IDataObject *pDataObj, QVariant::Type preferredType) const = 0;
    virtual QString mimeForFormat(const FORMATETC &formatetc) const = 0;

    static int registerMimeType(const QString &mime);

private:
    friend class QClipboardWatcher;
    friend class QDragManager;
    friend class QDropData;
    friend class QOleDataObject;

    static QWindowsMime *converterToMime(const QString &mimeType, IDataObject *pDataObj);
    static QStringList allMimesForFormats(IDataObject *pDataObj);
    static QWindowsMime *converterFromMime(const FORMATETC &formatetc, const QMimeData *mimeData);
    static QVector<FORMATETC> allFormatsForMime(const QMimeData *mimeData);
};

#endif
#if defined(Q_WS_PM)

/*
  Encapsulation of conversion between MIME and OS/2 PM clipboard formats and
  between MIME and Direct Manipulation (DND) objects.
*/

QT_BEGIN_INCLUDE_NAMESPACE

#include "qwindowdefs_pm.h"
typedef LHANDLE HSTR;
typedef struct _DRAGINFO DRAGINFO;
typedef struct _DRAGITEM DRAGITEM;

QT_END_INCLUDE_NAMESPACE

class QPMDragData;
class QPMCoopDragWorker;

class Q_GUI_EXPORT QPMMime
{
public:

#if !defined(QT_NO_DRAGANDDROP)

    typedef QList<QByteArray> QByteArrayList;

    class DragWorker
    {
    public:
        DragWorker() : src(0) {}
        virtual ~DragWorker() {}

        const QMimeData *source() const { return src; }

        virtual void init() {}
        // methods always implemented
        virtual bool cleanup(bool isCancelled) = 0;
        virtual bool isExclusive() const = 0;
        virtual ULONG itemCount() const = 0;
        virtual HWND hwnd() const = 0;
        // methods implemented if isExclusive() == TRUE and itemCount() == 0
        virtual DRAGINFO *createDragInfo(const QString &targetName, USHORT supportedOps)
                                        { return 0; }
        // methods implemented if itemCount() >= 0
        virtual QByteArray composeFormatString() { return QByteArray(); }
        virtual bool prepare(const char *drm, const char *drf, DRAGITEM *item,
                             ULONG itemIndex) { return false; }
        virtual void defaultFileType(QString &type, QString &ext) {};

    private:
        const QMimeData *src;
        friend class QPMCoopDragWorker;
        friend class QDragManager;
    };

    class DefaultDragWorker : public DragWorker
    {
    private:
        DefaultDragWorker(bool exclusive);
    public:
        virtual ~DefaultDragWorker();

        // DragWorker interface
        bool cleanup(bool isCancelled);
        bool isExclusive() const;
        ULONG itemCount() const;
        HWND hwnd() const;
        QByteArray composeFormatString();
        bool prepare(const char *drm, const char *drf, DRAGITEM *item,
                     ULONG itemIndex);
        void defaultFileType(QString &type, QString &ext);

        class Provider
        {
        public:
            virtual QString format(const char *drf) const = 0;
            virtual bool provide(const char *drf, const QByteArray &allData,
                                 ULONG itemIndex, QByteArray &itemData) = 0;
            virtual void fileType(const char *drf, QString &type, QString &ext) {};
        };

        bool addProvider(const QByteArray &drf, Provider *provider,
                         ULONG itemCount = 1);

        static bool canRender(const char *drm);

    private:
        struct Data;
        Data *d;
        friend class QPMMime;
    };

    class DropWorker
    {
    public:
        DropWorker() : nfo(0) {}
        virtual ~DropWorker() {}

        DRAGINFO *info() const { return nfo; }

        virtual void init() {}
        virtual void cleanup(bool isAccepted) {}
        virtual bool isExclusive() const = 0;

        virtual bool hasFormat(const QString &mimeType) const = 0;
        virtual QStringList formats() const = 0;
        virtual QVariant retrieveData(const QString &mimeType,
                                      QVariant::Type preferredType) const = 0;

    private:
        DRAGINFO *nfo;
        friend class QPMDragData;
    };

    class DefaultDropWorker : public DropWorker
    {
    private:
        DefaultDropWorker();
    public:
        virtual ~DefaultDropWorker();

        // DropWorker interface
        void cleanup(bool isAccepted);
        bool isExclusive() const;
        bool hasFormat(const QString &mimeType) const;
        QStringList formats() const;
        QVariant retrieveData(const QString &mimeType,
                              QVariant::Type preferredType) const;

        class Provider
        {
        public:
            virtual QByteArray drf(const QString &mimeType) const = 0;
            virtual bool provide(const QString &mimeType, ULONG itemIndex,
                                 const QByteArray &itemData,
                                 QByteArray &allData) = 0;
        };

        bool addProvider(const QString &mimeType, Provider *provider);
        bool addExclusiveProvider(const QString &mimeType, Provider *provider);

        static bool canRender(DRAGITEM *item, const char *drf);
        static bool getSupportedRMFs(DRAGITEM *item, QList<QByteArrayList> &list);

    private:
        struct Data;
        Data *d;
        friend class QPMMime;
    };

#endif // !QT_NO_DRAGANDDROP

    QPMMime();
    virtual ~QPMMime();

    struct MimeCFPair
    {
        MimeCFPair(const QString &m, ULONG f) : mime(m), format(f) {}
        QString mime;
        ULONG format;
    };

    // for converting from Qt
    virtual QList<MimeCFPair> formatsForMimeData(const QMimeData *mimeData) const = 0;
    virtual bool convertFromMimeData(const QMimeData *mimeData, ULONG format,
                                     ULONG &flags, ULONG *data) const = 0;
    // for converting to Qt
    virtual QList<MimeCFPair> mimesForFormats(const QList<ULONG> &formats) const = 0;
    virtual QVariant convertFromFormat(ULONG format, ULONG flags, ULONG data,
                                       const QString &mimeType,
                                       QVariant::Type preferredType) const = 0;

#if !defined(QT_NO_DRAGANDDROP)
    // Direct Manipulation (DND) converter interface
    virtual DragWorker *dragWorkerFor(const QString &mimeType,
                                      QMimeData *mimeData) { return 0; }
    virtual DropWorker *dropWorkerFor(DRAGINFO *info) { return 0; }
#endif // !QT_NO_DRAGANDDROP

protected:

    static ULONG registerMimeType(const QString &mime);
    static void unregisterMimeType(ULONG mimeId);

    static QList<QPMMime*> all();

    static ULONG allocateMemory(size_t size);
    static void freeMemory(ULONG addr);

    static QString formatName(ULONG format);

#if !defined(QT_NO_DRAGANDDROP)
    static QByteArray queryHSTR(HSTR hstr);
    static QByteArray querySourceNameFull(DRAGITEM *item);
    static bool canTargetRenderAsOS2File(DRAGITEM *item, QByteArray *fullName = 0);
    static bool parseRMFs(HSTR rmfs, QList<QByteArrayList> &list);
    static bool parseRMF(HSTR rmf, QByteArray &mechanism, QByteArray &format);

    static DefaultDragWorker *defaultCoopDragWorker();
    static DefaultDragWorker *defaultExclDragWorker();
    static DefaultDropWorker *defaultDropWorker();
#endif // !QT_NO_DRAGANDDROP

private:
    friend class QClipboardWatcher;
    friend class QClipboardData;
    friend class QClipboard;
    friend class QPMDragData;
    friend class QPMCoopDragWorker;

    struct Match
    {
        Match(QPMMime *c, const QString f, ULONG cf, int p) :
            converter(c), mime(f), format(cf), priority(p) {}

        QPMMime *converter;
        QString mime;
        ULONG format;
        int priority;
    };

    static QList<Match> allConvertersFromFormats(const QList<ULONG> &formats);
    static QList<Match> allConvertersFromMimeData(const QMimeData *mimeData);
};

#endif
#if defined(Q_WS_MAC)

/*
  Encapsulation of conversion between MIME and Mac flavor.
  Not needed on X11, as the underlying protocol uses the MIME standard
  directly.
*/

class Q_GUI_EXPORT QMacMime { //Obsolete
    char type;
public:
    enum QMacMimeType { MIME_DND=0x01, MIME_CLIP=0x02, MIME_QT_CONVERTOR=0x04, MIME_ALL=MIME_DND|MIME_CLIP };
    explicit QMacMime(char) { }
    virtual ~QMacMime() { }

    static void initialize() { }

    static QList<QMacMime*> all(QMacMimeType) { return QList<QMacMime*>(); }
    static QMacMime *convertor(QMacMimeType, const QString &, int) { return 0; }
    static QString flavorToMime(QMacMimeType, int) { return QString(); }

    virtual QString convertorName()=0;
    virtual int countFlavors()=0;
    virtual int flavor(int index)=0;
    virtual bool canConvert(const QString &mime, int flav)=0;
    virtual QString mimeFor(int flav)=0;
    virtual int flavorFor(const QString &mime)=0;
    virtual QVariant convertToMime(const QString &mime, QList<QByteArray> data, int flav)=0;
    virtual QList<QByteArray> convertFromMime(const QString &mime, QVariant data, int flav)=0;
};

class Q_GUI_EXPORT QMacPasteboardMime {
    char type;
public:
    enum QMacPasteboardMimeType { MIME_DND=0x01,
                                  MIME_CLIP=0x02,
                                  MIME_QT_CONVERTOR=0x04,
                                  MIME_QT3_CONVERTOR=0x08,
                                  MIME_ALL=MIME_DND|MIME_CLIP
    };
    explicit QMacPasteboardMime(char);
    virtual ~QMacPasteboardMime();

    static void initialize();

    static QList<QMacPasteboardMime*> all(uchar);
    static QMacPasteboardMime *convertor(uchar, const QString &mime, QString flav);
    static QString flavorToMime(uchar, QString flav);

    virtual QString convertorName() = 0;

    virtual bool canConvert(const QString &mime, QString flav) = 0;
    virtual QString mimeFor(QString flav) = 0;
    virtual QString flavorFor(const QString &mime) = 0;
    virtual QVariant convertToMime(const QString &mime, QList<QByteArray> data, QString flav) = 0;
    virtual QList<QByteArray> convertFromMime(const QString &mime, QVariant data, QString flav) = 0;
};

// ### Qt 5: Add const QStringList& QMacPasteboardMime::supportedFlavours()
Q_GUI_EXPORT void qRegisterDraggedTypes(const QStringList &types);
#endif // Q_WS_MAC

QT_END_NAMESPACE

QT_END_HEADER

#endif // QMIME_H
