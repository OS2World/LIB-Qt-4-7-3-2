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

#include "qsound.h"

#ifndef QT_NO_SOUND

#include "qapplication.h"
#include "qapplication_p.h"
#include <qfile.h>
#include "qsound_p.h"

#include "qlist.h"
#include "qhash.h"
#include "qlibrary.h"

#include <qt_os2.h>

#include <stdlib.h> // for getenv()

//#define QT_QSOUND_DEBUG

// local QT_DEBUG override, must be placed *after* all includes
#if defined(QT_QSOUND_DEBUG) && !defined(QT_DEBUG)
#   define QT_DEBUG
#endif

QT_BEGIN_NAMESPACE

////////////////////////////////////////////////////////////////////////////////

//#define INCL_MCIOS2
//#define INCL_MMIOOS2
//#include <os2me.h>

// The below definitions are stolen from the OS/2 Toolkit 4.5 headers
// to avoid the requirement of having the Toolkit installed when building Qt
// and let it directly link to mdm.dll and mmio.dll.

typedef ULONG HMMIO;                  /* Handle to an MMIO object*/

typedef ULONG  FOURCC;

typedef LONG (APIENTRY MMIOPROC)                    /*  Format must       */
                                (PVOID pmmioinfo,   /*  appear this       */
                                 USHORT usMsg,      /*  way for h2inc     */
                                 LONG lParam1,      /*  to work properly. */
                                 LONG lParam2);

typedef MMIOPROC FAR *PMMIOPROC;

typedef struct _MMIOINFO {       /* mmioinfo                    */
   ULONG       ulFlags;          /* Open flags                  */
   FOURCC      fccIOProc;        /* FOURCC of the IOProc to use */
   PMMIOPROC   pIOProc;          /* Function Pointer to IOProc to use */
   ULONG       ulErrorRet;       /* Extended Error return code  */
   LONG        cchBuffer;        /* I/O buff size (if used), Fsize if MEM */
   PCHAR       pchBuffer;        /* Start of I/O buff           */
   PCHAR       pchNext;          /* Next char to read or write in buff */
   PCHAR       pchEndRead;       /* Last char in buff can be read + 1  */
   PCHAR       pchEndWrite;      /* Last char in buff can be written + 1 */
   LONG        lBufOffset;       /* Offset in buff to pchNext */
   LONG        lDiskOffset;      /* Disk offset in file       */
   ULONG       aulInfo[4];       /* IOProc specific fields    */
   LONG        lLogicalFilePos;  /* Actual file position, buffered or not */
   ULONG       ulTranslate;      /* Translation field         */
   FOURCC      fccChildIOProc;   /* FOURCC of Child IOProc    */
   PVOID       pExtraInfoStruct; /* Pointer to a structure of related data */
   HMMIO       hmmio;            /* Handle to media element   */
   } MMIOINFO;

typedef MMIOINFO FAR *PMMIOINFO;

typedef struct _WAVE_HEADER {                /* waveheader          */
   USHORT          usFormatTag;              /* Type of wave format */
   USHORT          usChannels;               /* Number of channels  */
   ULONG           ulSamplesPerSec;          /* Sampling rate       */
   ULONG           ulAvgBytesPerSec;         /* Avg bytes per sec   */
   USHORT          usBlockAlign;             /* Block Alignment in bytes */
   USHORT          usBitsPerSample;          /* Bits per sample     */
   } WAVE_HEADER;

typedef struct _XWAV_HEADERINFO {            /* xwaveheader info        */
   ULONG           ulAudioLengthInMS;        /* Audio data in millisecs */
   ULONG           ulAudioLengthInBytes;     /* Audio data in bytes     */
   PVOID           pAdditionalInformation;
   } XWAV_HEADERINFO;

typedef struct _MMXWAV_HEADER {              /* mmxwaveheader            */
   WAVE_HEADER     WAVEHeader;               /* Per RIFF WAVE Definition */
   XWAV_HEADERINFO XWAVHeaderInfo;           /* Extended wave definition */
   } MMXWAV_HEADER;

typedef struct _MMAUDIOHEADER {              /* mmaudioheader   */
   ULONG           ulHeaderLength;           /* Length in Bytes */
   ULONG           ulContentType;            /* Image content   */
   ULONG           ulMediaType;              /* Media Type      */
   MMXWAV_HEADER   mmXWAVHeader;             /* header          */
   } MMAUDIOHEADER;

typedef MMAUDIOHEADER *PMMAUDIOHEADER;

typedef struct _MCI_OPEN_PARMS
{
   HWND    hwndCallback;    /* PM window handle for MCI notify message */
   USHORT  usDeviceID;      /* Device ID returned to user              */
   USHORT  usReserved0;     /* Reserved                                */
   PSZ     pszDeviceType;   /* Device name from SYSTEM.INI             */
   PSZ     pszElementName;  /* Typically a file name or NULL           */
   PSZ     pszAlias;        /* Optional device alias                   */
} MCI_OPEN_PARMS;
typedef MCI_OPEN_PARMS   *PMCI_OPEN_PARMS;

typedef struct _MCI_PLAY_PARMS
{
   HWND    hwndCallback;    /* PM window handle for MCI notify message */
   ULONG   ulFrom;          /* Play from this position                 */
   ULONG   ulTo;            /* Play to this position                   */
} MCI_PLAY_PARMS;
typedef MCI_PLAY_PARMS   *PMCI_PLAY_PARMS;

#define MMIO_TRANSLATEDATA       0x00000001L /* Translation */
#define MMIO_TRANSLATEHEADER     0x00000002L /* Translation */

#define MMIO_READ       0x00000004L       /* Open */

#define MMIO_SUCCESS                    0L

#define MMIO_MEDIATYPE_AUDIO        0x00000002L  /* Audio media */

#define MCI_DEVTYPE_WAVEFORM_AUDIO      7

#define MCI_NOTIFY                          0x00000001L
#define MCI_WAIT                            0x00000002L
#define MCI_FROM                            0x00000004L

#define MCI_OPEN_TYPE_ID                    0x00001000L
#define MCI_OPEN_SHAREABLE                  0x00002000L
#define MCI_OPEN_MMIO                       0x00004000L
#define MCI_READONLY                        0x00008000L

#define MCI_RETURN_RESOURCE                 0x00000100L

#define MCI_CLOSE_EXIT                 0x10000000L

#define MCI_OPEN                        1
#define MCI_CLOSE                       2
#define MCI_PLAY                        4
#define MCI_STOP                        6
#define MCI_ACQUIREDEVICE               23
#define MCI_RELEASEDEVICE               24

#define MM_MCINOTIFY                        0x0500
#define MM_MCIPASSDEVICE                    0x0501

#define MCI_NOTIFY_SUCCESSFUL               0x0000
#define MCI_NOTIFY_SUPERSEDED               0x0001

#define MCI_LOSING_USE                      0x00000001L
#define MCI_GAINING_USE                     0x00000002L

#define MCIERR_BASE                      5000
#define MCIERR_SUCCESS                   0
#define MCIERR_INSTANCE_INACTIVE         (MCIERR_BASE + 34)
#define MCIERR_DEVICE_LOCKED             (MCIERR_BASE + 32)

// functions resolved by mmio.dll

typedef
USHORT (APIENTRY *mmioClose_T)(HMMIO hmmio,
                               USHORT usFlags);
typedef
HMMIO (APIENTRY *mmioOpen_T)(PSZ pszFileName,
                             PMMIOINFO pmmioinfo,
                             ULONG ulOpenFlags);

typedef
ULONG (APIENTRY *mmioGetHeader_T)(HMMIO hmmio,
                                  PVOID pHeader,
                                  LONG lHeaderLength,
                                  PLONG plBytesRead,
                                  ULONG ulReserved,
                                  ULONG ulFlags);

static mmioClose_T mmioClose = 0;
static mmioOpen_T mmioOpen = 0;
static mmioGetHeader_T mmioGetHeader = 0;

// functions resolved by mdm.dll

typedef
ULONG (APIENTRY *mciSendCommand_T)(USHORT   usDeviceID,
                                   USHORT   usMessage,
                                   ULONG    ulParam1,
                                   PVOID    pParam2,
                                   USHORT   usUserParm);

static mciSendCommand_T mciSendCommand = 0;

////////////////////////////////////////////////////////////////////////////////

class QAuBucketMMPM;

class QAuServerMMPM : public QAuServer {

    Q_OBJECT

public:

    QAuServerMMPM(QObject* parent);
    ~QAuServerMMPM();

    void init(QSound *s);
    void play(const QString &filename);
    void play(QSound *s);
    void stop(QSound *s);
    bool okay();

private slots:

    void serverUninit();

private:

    HWND hwnd;
    bool isOk;

    QLibrary mdmLib;
    QLibrary mmioLib;

    QHash<int, QAuBucketMMPM *> bucketMap;
    QList<QAuBucketMMPM *> bucketList;

    static const char *ClassName;
    static MRESULT EXPENTRY WindowProc(HWND hwnd, ULONG msg,
                                       MPARAM mp1, MPARAM mp2);

    friend class QAuBucketMMPM;
};

////////////////////////////////////////////////////////////////////////////////

class QAuBucketMMPM : public QAuBucket {

public:

    enum State { Stopped, Playing, Waiting };

    QAuBucketMMPM(QAuServerMMPM *server, QSound *sound);
    QAuBucketMMPM(QAuServerMMPM *server, const QString &soundFile);
    ~QAuBucketMMPM();

    void open();
    void close(bool atExit = false);
    bool play();
    void stop();
    bool okay() { return fileHandle != NULLHANDLE; }

    QSound *sound() { return snd; }
    State state() { return st; }

    void onDeviceGained(bool gained);

#if defined(QT_DEBUG)
    QByteArray fileName() { return fName; }
#endif

private:

    void init(const QString &fileName);

    QAuServerMMPM *srv;
    QSound *snd;

#if defined(QT_DEBUG)
    QByteArray fName;
#endif

    HMMIO fileHandle;
    USHORT deviceId;

    State st;
};

////////////////////////////////////////////////////////////////////////////////

QAuBucketMMPM::QAuBucketMMPM(QAuServerMMPM *server, QSound *sound) :
    srv(server), snd(sound), fileHandle(NULLHANDLE), deviceId(0),
    st(Stopped)
{
    Q_ASSERT(srv);
    Q_ASSERT(snd);
    if (!srv || !snd)
        return;

    init(snd->fileName());
}

QAuBucketMMPM::QAuBucketMMPM(QAuServerMMPM *server, const QString &fileName) :
    srv(server), snd(NULL), fileHandle(NULLHANDLE), deviceId(0),
    st(Stopped)
{
    Q_ASSERT(srv);
    if (!srv)
        return;

    init(fileName);
}

void QAuBucketMMPM::init(const QString &soundFile)
{
    Q_ASSERT(fileHandle == NULLHANDLE);
    if (fileHandle != NULLHANDLE)
        return;

#if !defined(QT_DEBUG)
    QByteArray
#endif
    fName = QFile::encodeName(soundFile);

    MMIOINFO mmioinfo = { 0 };
    mmioinfo.ulTranslate = MMIO_TRANSLATEDATA | MMIO_TRANSLATEHEADER;
    fileHandle = mmioOpen(fName.data(), &mmioinfo, MMIO_READ);
    if (fileHandle == NULLHANDLE) {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: falied to open sound file [%s]", fName.data());
#endif
        return;
    }

    MMAUDIOHEADER mmah;
    LONG bytesRead = 0;
    ULONG rc = mmioGetHeader(fileHandle, &mmah, sizeof(mmah), &bytesRead, 0, 0);
    if (rc != MMIO_SUCCESS || mmah.ulMediaType != MMIO_MEDIATYPE_AUDIO) {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: [%s] is not a sound file or "
               "has an unsupported format (rc=%04hu:%04hu)",
               fName.data(), HIUSHORT(rc), LOUSHORT(rc));
#endif
        mmioClose(fileHandle, 0);
        fileHandle = NULLHANDLE;
        return;
    }

    srv->bucketList.append(this);

#if defined(QT_QSOUND_DEBUG)
    qDebug("QAuBucketMMPM::init(): {%p} [%s]", this, fName.data());
#endif
}

QAuBucketMMPM::~QAuBucketMMPM()
{
#if defined(QT_QSOUND_DEBUG)
    qDebug("~QAuBucketMMPM(): {%p} [%s]", this, fName.data());
#endif

    if (deviceId)
        close(srv->hwnd == NULLHANDLE);

    if (fileHandle != NULLHANDLE) {
        // removeo from the list unless called from serverUninit()
        if (srv->hwnd != NULLHANDLE)
            srv->bucketList.removeAll(this);
        mmioClose(fileHandle, 0);
    }
}

void QAuBucketMMPM::open()
{
    Q_ASSERT(!deviceId);
    if (deviceId)
        return;

    MCI_OPEN_PARMS openParams = { 0 };
    openParams.hwndCallback = srv->hwnd;
    openParams.pszDeviceType = (PSZ) MAKEULONG(MCI_DEVTYPE_WAVEFORM_AUDIO, 0);
    openParams.pszElementName = (PSZ) fileHandle;
    ULONG params = MCI_WAIT | MCI_OPEN_MMIO | MCI_READONLY | MCI_OPEN_TYPE_ID;
    if (getenv("QT_PM_NO_SOUND_SHARE") != NULL) {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: WARNING: opening device for sound file [%s] in "
                "exclusive mode due to QT_PM_NO_SOUND_SHARE=%s",
                fName.data(), getenv("QT_PM_NO_SOUND_SHARE"));
#endif
    } else if (getenv("QT_PM_SOUND_SHARE") != NULL) {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: WARNING: opening device for sound file [%s] in "
               "shareable mode due to QT_PM_SOUND_SHARE=%s",
               fName.data(), getenv("QT_PM_SOUND_SHARE"));
#endif
        params |= MCI_OPEN_SHAREABLE;
    } else {
        // use shared mode by default
        params |= MCI_OPEN_SHAREABLE;
    }
    ULONG rc = mciSendCommand(0, MCI_OPEN, params, &openParams, 0);
    if (rc != MCIERR_SUCCESS) {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: failed to open a device for sound file [%s] "
               "(rc=%04hu:%04hu)", fName.data(), HIUSHORT(rc), LOUSHORT(rc));
#endif
        return;
    }

    deviceId = openParams.usDeviceID;

    srv->bucketMap.insert(deviceId, this);

#if defined(QT_QSOUND_DEBUG)
    qDebug("QAuBucketMMPM::open(): {%p} [%s] deviceId=%08hu",
           this, fName.data(), deviceId);
#endif
}

void QAuBucketMMPM::close(bool atExit /* = false */)
{
    Q_ASSERT(deviceId);
    if (!deviceId)
        return;

#if defined(QT_QSOUND_DEBUG)
    qDebug("QAuBucketMMPM::close(): {%p} [%s] atExit=%d",
           this, fName.data(), atExit);
#endif

    // remove from the map anyway -- we don't plan to retry
    srv->bucketMap.remove(deviceId);

    // Use MCI_CLOSE_EXIT to tell the media control driver it should
    // close immediately, w/o waiting or doing any notifications, etc.
    ULONG param = atExit ? MCI_CLOSE_EXIT : 0;

    ULONG rc = mciSendCommand(deviceId, MCI_CLOSE, param, NULL, 0);
#if defined(QT_DEBUG)
    if (rc != MCIERR_SUCCESS)
        qDebug("QAuBucketMMPM: failed to close the device for sound file [%s] "
               "(rc=%04hu:%04hu)",
               fName.data(), HIUSHORT(rc), LOUSHORT(rc));
#endif
    Q_UNUSED(rc);

    st = Stopped;
    deviceId = 0;
}

bool QAuBucketMMPM::play()
{
#if defined(QT_QSOUND_DEBUG)
    qDebug("QAuBucketMMPM::play(): {%p} [%s]", this, fName.data());
#endif

    if (!deviceId) {
        open();
        if (!deviceId)
            return false;
    }

    MCI_PLAY_PARMS playParams = { 0 };
    playParams.hwndCallback = srv->hwnd;
    playParams.ulFrom = 0; // always play from the beginning

    ULONG rc = mciSendCommand(deviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM,
                              &playParams, 0);

    if (LOUSHORT(rc) == MCIERR_INSTANCE_INACTIVE) {
        // There are not enough simultaneous audio streams. Try to acquire the
        // resources and play again. Note that if the device is already acquired
        // for exclusive use, this command will not wait but return immediately.
        rc = mciSendCommand(deviceId, MCI_ACQUIREDEVICE, MCI_WAIT, NULL, 0);
        if (rc == MCIERR_SUCCESS) {
            rc = mciSendCommand(deviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM,
                                &playParams, 0);
        } else if (LOUSHORT(rc) == MCIERR_DEVICE_LOCKED &&
                   snd && snd->loops() < 0) {
            // Enter the special state to let this infinitive sound start
            // playing when the resource becomes available.
            st = Waiting;
            return false;
        }
    }

    if (rc == MCIERR_SUCCESS) {
        st = Playing;
    } else {
        st = Stopped;
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: failed to play sound file [%s] (rc=%04hu:%04hu)",
               fName.data(), HIUSHORT(rc), LOUSHORT(rc));
#endif
    }

    return st == Playing;
}

void QAuBucketMMPM::stop()
{
    if (st == Stopped)
        return;

    // always go to Stopped -- we won't retry
    // (this is also used in MM_MCINOTIFY processing)
    st = Stopped;

    ULONG rc = mciSendCommand(deviceId, MCI_STOP, MCI_WAIT, NULL, 0);

    if (rc == MCIERR_SUCCESS) {
        // nothing
    } else if (LOUSHORT(rc) == MCIERR_INSTANCE_INACTIVE) {
        // The infinite QSound-full bucket is now suspended (some other instance
        // has gained the resource). Close this instance to prevent it from
        // being automatically resumed later.
        close();
    } else {
#if defined(QT_DEBUG)
        qDebug("QAuBucketMMPM: failed to stop sound file [%s] (rc=%04hu:%04hu)",
               fName.data(), HIUSHORT(rc), LOUSHORT(rc));
#endif
        // last chance to stop
        close();
    }
}

void QAuBucketMMPM::onDeviceGained(bool gained)
{
    if (gained) {
        // We gained the device resource.
        if (st == Waiting) {
            /// @todo (dmik) For some reason, starting playback from here
            //  (i.e. when we've been given the deivce resource back after
            //  some other application finished using it exclusively), we can
            //  get error 5053 (MCIERR_NO_CONNECTION) or even undocumented 5659.
            //  When it happens, subsequent attempts to play something will
            //  result into an unkillable hang... Experimemtal for now.
#if 1
            // A QSound-full bucket attempted to play eventually regained
            // the resource. Start playing.
            play();
#else
            st = Stopped;
#endif
        }
    } else {
        // We lost the device resource.
        if (st == Playing) {
            // Close the instance to prevent the playback from being resumed
            // when the device is auto-gained again (by another instance that
            // uses MCI_RETURN_RESOURCE in MCI_RELEASEDEVICE). The exception is
            // a QSound-full bucket with an infinitive loop (that will continue
            // playing when the resuorce is back).
            if (snd) {
                // infinitive loop?
                if (snd->loops() < 0)
                    return;
                // decrease loops to zero
                while (srv->decLoop(snd) > 0) ;
            }
            close();
            if (!snd) {
                // delete QSound-less bucket
                delete this;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

const char *QAuServerMMPM::ClassName = "QAuServerMMPM";

QAuServerMMPM::QAuServerMMPM(QObject* parent) :
    QAuServer(parent),
    hwnd(NULLHANDLE), isOk(false),
    mdmLib(QLatin1String("mdm.dll")), mmioLib(QLatin1String("mmio.dll"))
{
    WinRegisterClass(0, ClassName, WindowProc, 0, sizeof(PVOID));

    hwnd = WinCreateWindow(HWND_OBJECT, ClassName, NULL, 0, 0, 0, 0, 0,
                           NULL, HWND_BOTTOM, 0,
                           this, NULL);

    if (hwnd == NULLHANDLE) {
        qWarning("QAuServerMMPM: WinCreateWindow(HWND_OBJECT) "
                 "failed with 0x%08lX", WinGetLastError(0));
        return;
    }

    // resolve functions
    mmioClose = (mmioClose_T) mmioLib.resolve("mmioClose");
    mmioOpen = (mmioOpen_T) mmioLib.resolve("mmioOpen");
    mmioGetHeader = (mmioGetHeader_T) mmioLib.resolve("mmioGetHeader");
    mciSendCommand = (mciSendCommand_T) mdmLib.resolve("mciSendCommand");
    if (!mmioClose || !mmioGetHeader || !mmioOpen || !mciSendCommand) {
        qWarning("QAuServerMMPM: failed to resolve MMPM system functions");
        return;
    }

    // try to open the default waveaudio device to ensure it exists
    MCI_OPEN_PARMS openParams = { 0 };
    openParams.pszDeviceType = (PSZ) MAKEULONG(MCI_DEVTYPE_WAVEFORM_AUDIO, 0);
    ULONG rc = mciSendCommand(0, MCI_OPEN, MCI_WAIT |
                                            MCI_OPEN_SHAREABLE | MCI_OPEN_TYPE_ID,
                               &openParams, 0);
    if (rc != MCIERR_SUCCESS) {
#if defined(QT_DEBUG)
        qDebug("QAuServerMMPM: failed to open the default waveaudio device "
               "(rc=%04hu:%04hu)", HIUSHORT(rc), LOUSHORT(rc));
#endif
        return;
    }

    // close the device opened above
    mciSendCommand(openParams.usDeviceID, MCI_CLOSE, MCI_WAIT, NULL, 0);

    connect(qApp, SIGNAL(aboutToQuit()), SLOT(serverUninit()));

    isOk = true;
}

QAuServerMMPM::~QAuServerMMPM()
{
    // still alive?
    if (hwnd)
        serverUninit();
}

void QAuServerMMPM::serverUninit()
{
#if defined(QT_QSOUND_DEBUG)
    qDebug("QAuServerMMPM::serverUninit(): buckets left: %d",
           bucketList.count());
#endif

    Q_ASSERT(hwnd);
    if (hwnd == NULLHANDLE)
        return;

    WinDestroyWindow(hwnd);
    hwnd = NULLHANDLE;

    // Deassociate all remaining buckets from QSound objects and delete them
    // (note that deleting a bucket will remove it from the list, which
    // in turn will advance the current item to the next one, so current() is
    // is used instead of next() in the loop).
    foreach (QAuBucketMMPM *b, bucketList) {
        QSound *s = b->sound();
        if (s) {
            // the below call will delete the associated bucket
            setBucket (s, 0);
        } else {
            delete b;
        }
    }

    bucketList.clear();

    Q_ASSERT(bucketMap.count() == 0);

    mmioClose = 0;
    mmioOpen = 0;
    mmioGetHeader = 0;
    mciSendCommand = 0;
}

void QAuServerMMPM::init(QSound *s)
{
    Q_ASSERT(s);

    if (!okay())
        return;

    QAuBucketMMPM *b = new QAuBucketMMPM(this, s);
    if (b->okay()) {
        setBucket(s, b);
#if defined(QT_QSOUND_DEBUG)
        qDebug("QAuServerMMPM::init(): bucket=%p [%s]",
               b, b->fileName().data());
#endif
    } else {
        setBucket(s, 0);
        // b is deleted in setBucket()
    }
}

void QAuServerMMPM::play(const QString &filename)
{
    if (!okay())
        return;

    QAuBucketMMPM *b = new QAuBucketMMPM(this, filename);
    if (b->okay()) {
#if defined(QT_QSOUND_DEBUG)
        qDebug("play(QString): bucket=%p [%s]", b, b->fileName().data());
#endif
        if (b->play()) {
            // b will be deleted in WindowProc() or in onDeviceGained()
        } else {
            delete b;
        }
    } else {
        delete b;
    }
}

void QAuServerMMPM::play(QSound *s)
{
    Q_ASSERT(s);

    if (!okay() || !isRelevant(s)) {
        // no MMPM is available or a wrong sound, just decrease loops to zero
        while (decLoop(s) > 0) ;
        return;
    }

    QAuBucketMMPM *b = static_cast< QAuBucketMMPM * >(bucket(s));
    if (b) {
#if defined(QT_QSOUND_DEBUG)
        qDebug("play(QSound): bucket=%p [%s]", b, b->fileName().data());
#endif
        b->play();
    } else {
        // failed to create a bucket in init(), just decrease loops to zero
        while (decLoop(s) > 0) ;
    }
}

void QAuServerMMPM::stop(QSound *s)
{
    Q_ASSERT(s);

    if (!okay() || !isRelevant(s))
        return;

    QAuBucketMMPM *b = static_cast< QAuBucketMMPM * >(bucket(s));
    if (b)
        b->stop();
}

bool QAuServerMMPM::okay()
{
    return isOk;
}

MRESULT EXPENTRY QAuServerMMPM::WindowProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
    QAuServerMMPM * that =
        static_cast< QAuServerMMPM * >(WinQueryWindowPtr(hwnd, 0));

    switch (msg) {
        case WM_CREATE: {
            QAuServerMMPM * that = static_cast< QAuServerMMPM * >(mp1);
            if (!that)
                return (MRESULT) TRUE;
            WinSetWindowPtr(hwnd, 0, that);
            return (MRESULT) FALSE;
        }
        case WM_DESTROY: {
            return 0;
        }
        case MM_MCINOTIFY: {
            if (!that)
                return 0;

            USHORT code = SHORT1FROMMP(mp1);
            USHORT deviceId = SHORT1FROMMP(mp2);
            USHORT mcimsg = SHORT2FROMMP(mp2);
#if defined(QT_QSOUND_DEBUG)
            qDebug("MM_MCINOTIFY: code=0x%04hX, deviceId=%04hu, mcimsg=%04hu",
                   code, deviceId, mcimsg);
#endif
            Q_ASSERT(mcimsg == MCI_PLAY);
            if (mcimsg != MCI_PLAY)
                return 0;

            QAuBucketMMPM *b = that->bucketMap.value(deviceId);
            // There will be a late notification if the sound is
            // playing when close() happens. Just return silently.
            if (!b)
                return 0;

#if defined(QT_QSOUND_DEBUG)
            qDebug("MM_MCINOTIFY: bucket=%p [%s]", b, b->fileName().data());
#endif

            QSound *sound = b->sound();
            if (sound) {
                bool returnResource = false;
                if (b->state() == QAuBucketMMPM::Stopped) {
                    // It's possible that MCI_STOP is issued right after MCI_PLAY
                    // has successfilly finished and sent MM_MCINOTIFY but before
                    // we start it again here. Obey the STOP request.
                    returnResource = true;
                } else if (code == MCI_NOTIFY_SUCCESSFUL) {
                    // play the sound until there are no loops left
                    int loopsLeft = that->decLoop(sound);
                    if (loopsLeft != 0)
                        b->play();
                    else
                        returnResource = true;
                } else if (code != MCI_NOTIFY_SUPERSEDED) {
                    returnResource = true;
                }
                if (returnResource) {
                    // let infinitive sounds continue playing
                    mciSendCommand(deviceId, MCI_RELEASEDEVICE,
                                    MCI_RETURN_RESOURCE, NULL, 0);
                }
            } else {
                // delete QSound-less bucket when finished or stopped playing
                // (closing the instance will return the resource)
                delete b;
            }

            return 0;
        }
        case MM_MCIPASSDEVICE: {
            if (!that)
                return 0;

            USHORT deviceId = SHORT1FROMMP(mp1);
            USHORT event = SHORT1FROMMP(mp2);
#if defined(QT_QSOUND_DEBUG)
            qDebug("MM_MCIPASSDEVICE: deviceId=%04hu, event=0x%04hX",
                   deviceId, event);
#endif
            QAuBucketMMPM *b = that->bucketMap.value(deviceId);
            if (!b)
                return 0;

#if defined(QT_QSOUND_DEBUG)
            qDebug("MM_MCIPASSDEVICE: bucket=%p [%s]", b, b->fileName().data());
#endif
            // Note: this call may delete b
            b->onDeviceGained(event == MCI_GAINING_USE);

            return 0;
        }
    }

    return 0;
}

QAuServer* qt_new_audio_server()
{
    return new QAuServerMMPM(qApp);
}

QT_END_NAMESPACE

#include "qsound_pm.moc"

#endif // QT_NO_SOUND
