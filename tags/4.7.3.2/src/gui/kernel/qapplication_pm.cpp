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

#include "qdebug.h"

#include "qapplication.h"
#include "qapplication_p.h"

#include "qwidget.h"
#include "qpointer.h"
#include "qcolormap.h"
#include "qpixmapcache.h"
#include "qdesktopwidget.h"
#include "qsessionmanager.h"

#include "qset.h"

#include "private/qeventdispatcher_pm_p.h"
#include "private/qbackingstore_p.h"

#include "qwidget_p.h"
#include "qkeymapper_p.h"
#include "qcursor_p.h"
#include "qdnd_p.h"

#define WM_KBDLAYERCHANGED   0x0BD4 // defined in OS2TK45/h/pmbidi.h

//#define QT_DEBUGMSGFLOW

QT_BEGIN_NAMESPACE

/*****************************************************************************
  Internal variables and functions
 *****************************************************************************/

static HWND	 curWin		= 0;		// current window
static HPS	 displayPS	= 0;		// display presentation space

#if !defined (QT_NO_SESSIONMANAGER)

//#define DEBUG_SESSIONMANAGER

// Session management
static bool	sm_blockUserInput    = false;
static bool	sm_smActive          = false;
static bool	sm_cancel            = false;
static bool	sm_gracefulShutdown  = false;
static bool	sm_quitSkipped       = false;

extern QSessionManager *qt_session_manager_self; // defined in qapplication.cpp
extern bool qt_about_to_destroy_wnd; // defined in qwidget_pm.cpp

#endif

static bool replayPopupMouseEvent = false; // replay handling when popups close

// ignore the next release event if return from a modal widget
static bool ignoreNextMouseReleaseEvent = false;

#if defined(QT_DEBUG)
static bool appNoGrab = false; // mouse/keyboard grabbing
#endif

static bool app_do_modal = false; // modal mode
extern QWidgetList *qt_modal_stack;
extern QDesktopWidget *qt_desktopWidget;
static QPointer<QWidget> popupButtonFocus;
static bool	qt_try_modal(QWidget*, QMSG*, MRESULT&);

QWidget *qt_button_down = 0; // widget got last button-down
QPointer<QWidget> qt_last_mouse_receiver = 0;

static HWND foreignActiveWnd = NULLHANDLE;
static HWND foreignFocusWnd = NULLHANDLE;

static HWND autoCaptureWnd = NULLHANDLE;
static bool autoCaptureReleased = false;
static void setAutoCapture(HWND); // automatic capture
static void releaseAutoCapture();

extern QCursor *qt_grab_cursor();

#ifdef QT_PM_NATIVEWIDGETMASK
extern void qt_WinQueryClipRegionOrRect(HWND hwnd, HRGN hrgn); // qwidget_pm.cpp
#endif

extern bool qt_sendSpontaneousEvent(QObject*, QEvent*); // qapplication.cpp

extern QRegion qt_dirtyRegion(QWidget *); // qbackingstore.cpp

MRESULT EXPENTRY QtWndProc(HWND, ULONG, MPARAM, MPARAM);

class QETWidget : public QWidget                // event translator widget
{
public:
    QWExtra    *xtra() { return d_func()->extraData(); }
    QTLWExtra  *topData() { return d_func()->topData(); }
    QTLWExtra  *maybeTopData() { return d_func()->maybeTopData(); }
// @todo later
//  void syncBackingStore(const QRegion &rgn) { d_func()->syncBackingStore(rgn); }
//  void syncBackingStore() { d_func()->syncBackingStore(); }
    QWidgetData *dataPtr() { return data; }
    QWidgetPrivate *dptr() { return d_func(); }
    QRect frameStrut() const { return d_func()->frameStrut(); }
    HWND frameWinId() const { return d_func()->frameWinId(); }
    QETWidget* modalBlocker() const { return (QETWidget *)d_func()->modalBlocker(); }
    bool        pmEvent(QMSG *m, MRESULT *r) { return QWidget::pmEvent(m, r); }
//  void        markFrameStrutDirty() { data->fstrut_dirty = 1; }
    bool        translateMouseEvent(const QMSG &qmsg);
    void        translateNonClientMouseEvent(const QMSG &qmsg);
#ifndef QT_NO_WHEELEVENT
    bool        translateWheelEvent(const QMSG &qmsg);
#endif
    bool        translatePaintEvent(const QMSG &qmsg);
    bool        translateConfigEvent(const QMSG &qmsg);
    bool        translateCloseEvent(const QMSG &qmsg);
//  void        repolishStyle(QStyle &style);
//  inline void showChildren(bool spontaneous) { d_func()->showChildren(spontaneous); }
//  inline void hideChildren(bool spontaneous) { d_func()->hideChildren(spontaneous); }
//  inline void validateObstacles() { d_func()->validateObstacles(); }
//  inline uint testWindowState(uint teststate){ return dataPtr()->window_state & teststate; }
//  inline void forceUpdate() {
//      QTLWExtra *tlwExtra = window()->d_func()->maybeTopData();
//      if (tlwExtra && tlwExtra->backingStore)
//          tlwExtra->backingStore->markDirty(rect(), this, true, true);
//  }
};

static QRgb qt_sysclr2qrgb(LONG sysClr)
{
    // QRgb has the same RGB format (0xaarrggbb) as OS/2 uses (ignoring the
    // highest alpha byte) so we just cast the OS/2 LONG RGB value to qRgb
    // which is an unsigned int actually.
    return ((QRgb)WinQuerySysColor(HWND_DESKTOP, sysClr, 0)) & RGB_MASK;
}

static QFont qt_sysfont2qfont(PCSZ scope)
{
    // note: 10.System Proportional is de-facto the default font selected into
    // the presentation space

    static PCSZ app = "PM_SystemFonts";
    static PCSZ def = "10.System Proportional";

    ULONG keyLen = 0;
    QFont f(QLatin1String("System Proportional"), 10);

    if (PrfQueryProfileSize(HINI_USERPROFILE, app, scope, &keyLen) && keyLen) {
        keyLen++; // reserve space for the dot
        char *buf = new char[keyLen];
        ULONG realLen = PrfQueryProfileString(HINI_USERPROFILE, app, scope, def,
                                              buf, keyLen);
        realLen--; // excude terminating NULL

        // parse the font definition
        int height = 0;
        char *dot = strchr(buf, '.'), *dot2 = 0;
        if (dot) {
            *dot = 0;
            height = strtoul(buf, NULL, 10);
            dot2 = strchr(++ dot, '.');
            if (dot2) {
                // process simulated styles
                buf[realLen] = '.';
                buf[realLen+1] = 0;
                strupr( dot2 );
                // @todo currently, simulated bold and italic font styles are not
                // supported by Qt/OS2 because Qt doesn't support style simulation
                // explicitly. the code below is commented out to prevent selecting
                // true fonts when simulated ones are actually requested.
                // if (strstr(dot2, ".BOLD.")) f.setBold(true);
                // if (strstr(dot2, ".ITALIC.")) f.setItalic(true);
                if (strstr(dot2, ".UNDERSCORE.")) f.setUnderline(true);
                if (strstr(dot2, ".UNDERLINED.")) f.setUnderline(true);
                if (strstr(dot2, ".STRIKEOUT.")) f.setStrikeOut(true);
                *dot2 = 0;
            }
            // query non-simulated styles
            FONTMETRICS fm;
            LONG cnt = 1; // use the first match
            GpiQueryFonts(qt_display_ps(), QF_PUBLIC, dot, &cnt, sizeof(FONTMETRICS), &fm);
            if (cnt) {
                if (fm.fsSelection & FM_SEL_ITALIC) f.setItalic(true);
                if (fm.fsType & FM_TYPE_FIXED) f.setFixedPitch(true);
                USHORT weight = fm.usWeightClass;
                USHORT width = fm.usWidthClass;
                if (weight < 4) f.setWeight( QFont::Light );
                else if (weight < 6) f.setWeight(QFont::Normal);
                else if (weight < 7) f.setWeight(QFont::DemiBold);
                else if (weight < 8) f.setWeight(QFont::Bold);
                else f.setWeight(QFont::Black);
                switch (width) {
                    case 1: f.setStretch(QFont::UltraCondensed); break;
                    case 2: f.setStretch(QFont::ExtraCondensed); break;
                    case 3: f.setStretch(QFont::Condensed); break;
                    case 4: f.setStretch(QFont::SemiCondensed); break;
                    case 5: f.setStretch(QFont::Unstretched); break;
                    case 6: f.setStretch(QFont::SemiExpanded); break;
                    case 7: f.setStretch(QFont::Expanded); break;
                    case 8: f.setStretch(QFont::ExtraExpanded); break;
                    case 9: f.setStretch(QFont::UltraExpanded); break;
                    default: f.setStretch(QFont::Unstretched); break;
                }
                f.setFamily(QString::fromLocal8Bit(fm.szFamilyname));
                f.setPointSize(height);
            }
        }
        delete[] buf;
    }
    return f;
}

static void qt_set_pm_resources()
{
    QFont menuFont = qt_sysfont2qfont("Menus");
    QFont iconFont = qt_sysfont2qfont("IconText");
    QFont titleFont = qt_sysfont2qfont("WindowTitles");

    QApplication::setFont(menuFont, "QMenu");
    QApplication::setFont(menuFont, "QMenuBar");
    QApplication::setFont(titleFont, "Q3TitleBar");
    QApplication::setFont(titleFont, "QWorkspaceTitleBar");
    QApplication::setFont(iconFont, "QAbstractItemView");
    QApplication::setFont(iconFont, "QDockWidgetTitle");

    // let QPallette calculate all colors automatically based on the
    // base PM window background color -- to be on the safe side in case if we
    // don't set some role explicitly below (like QPalette::AlternateBase)
    QPalette pal = QPalette(QColor(qt_sysclr2qrgb(SYSCLR_DIALOGBACKGROUND)));

    pal.setColor(QPalette::WindowText,
                 QColor(qt_sysclr2qrgb(SYSCLR_WINDOWTEXT)));
    pal.setColor(QPalette::Window,
                 QColor(qt_sysclr2qrgb(SYSCLR_DIALOGBACKGROUND)));
    pal.setColor(QPalette::ButtonText,
                 QColor(qt_sysclr2qrgb(SYSCLR_MENUTEXT)));
    pal.setColor(QPalette::Button,
                 QColor(qt_sysclr2qrgb(SYSCLR_BUTTONMIDDLE)));
    pal.setColor(QPalette::Light,
                 QColor(qt_sysclr2qrgb(SYSCLR_BUTTONLIGHT)));
    pal.setColor(QPalette::Dark,
                 QColor(qt_sysclr2qrgb(SYSCLR_BUTTONDARK)));
    pal.setColor(QPalette::Midlight,
                 QColor((pal.light().color().red()   + pal.button().color().red())   / 2,
                        (pal.light().color().green() + pal.button().color().green()) / 2,
                        (pal.light().color().blue()  + pal.button().color().blue())  / 2));
    pal.setColor(QPalette::Mid,
                 QColor((pal.dark().color().red()    + pal.button().color().red())   / 2,
                        (pal.dark().color().green()  + pal.button().color().green()) / 2,
                        (pal.dark().color().blue()   + pal.button().color().blue())  / 2));
    pal.setColor(QPalette::Shadow, // note: SYSCLR_SHADOW often = SYSCLR_BUTTONDARK
                 QColor(qt_sysclr2qrgb(SYSCLR_BUTTONDEFAULT)));
    pal.setColor(QPalette::Text,
                 QColor(qt_sysclr2qrgb(SYSCLR_WINDOWTEXT)));
    pal.setColor(QPalette::Base,
                 QColor(qt_sysclr2qrgb(SYSCLR_ENTRYFIELD)));
    pal.setColor(QPalette::BrightText,
                 QColor(qt_sysclr2qrgb(SYSCLR_BUTTONLIGHT)));
    pal.setColor(QPalette::Highlight,
                 QColor(qt_sysclr2qrgb(SYSCLR_HILITEBACKGROUND)));
    pal.setColor(QPalette::HighlightedText,
                 QColor(qt_sysclr2qrgb(SYSCLR_HILITEFOREGROUND)));
    // these colors are not present in the PM system palette
    pal.setColor(QPalette::Link, Qt::blue);
    pal.setColor(QPalette::LinkVisited, Qt::magenta);

    // disabled colors
    // note: it should be SYSCLR_MENUDISABLEDTEXT but many styles use etched
    // appearance for disabled elements (in combination with QPalette::Light)
    // which gives weakly readable text. Make it somewhat darker.
    pal.setColor(QPalette::Disabled, QPalette::WindowText,
                  QColor(qt_sysclr2qrgb(SYSCLR_BUTTONDARK)));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,
                  QColor(qt_sysclr2qrgb(SYSCLR_BUTTONDARK)));
    pal.setColor(QPalette::Disabled, QPalette::Text,
                  QColor(qt_sysclr2qrgb(SYSCLR_BUTTONDARK)));

    QApplicationPrivate::setSystemPalette(pal);

    // special palete: menus
    QPalette spal = pal;
    spal.setColor(QPalette::Window,
                  QColor(qt_sysclr2qrgb(SYSCLR_MENU)));
    spal.setColor(QPalette::WindowText,
                  QColor(qt_sysclr2qrgb(SYSCLR_MENUTEXT)));
    spal.setColor(QPalette::Highlight,
                  QColor(qt_sysclr2qrgb( SYSCLR_MENUHILITEBGND)));
    spal.setColor(QPalette::HighlightedText,
                  QColor(qt_sysclr2qrgb(SYSCLR_MENUHILITE)));

    QApplication::setPalette(spal, "QMenu");
    QApplication::setPalette(spal, "QMenuBar");

    // special palete: static widget text
    spal = pal;
    QColor staticTextCol(qt_sysclr2qrgb( SYSCLR_WINDOWSTATICTEXT));
    spal.setColor(QPalette::WindowText, staticTextCol);
    spal.setColor(QPalette::Text, staticTextCol);

    QApplication::setPalette(spal, "QLabel");
    QApplication::setPalette(spal, "QGroupBox");
};

/*****************************************************************************
  qt_init() - initializes Qt for PM
 *****************************************************************************/

void qt_init(QApplicationPrivate *priv, int)
{
    int argc = priv->argc;
    char **argv = priv->argv;
    int i, j;

    // Get command line params

    j = argc ? 1 : 0;
    for (i=1; i<argc; i++) {
        if (argv[i] && *argv[i] != '-') {
            argv[j++] = argv[i];
            continue;
        }
#if defined(QT_DEBUG)
        if (qstrcmp(argv[i], "-nograb") == 0)
            appNoGrab = !appNoGrab;
        else
#endif // QT_DEBUG
            argv[j++] = argv[i];
    }
    if(j < priv->argc) {
        priv->argv[j] = 0;
        priv->argc = j;
    }

    displayPS = WinGetScreenPS(HWND_DESKTOP);

    // initialize key mapper
    QKeyMapper::changeKeyboard();

    QColormap::initialize();
    QFont::initialize();
#ifndef QT_NO_CURSOR
    QCursorData::initialize();
#endif
    qApp->setObjectName(priv->appName());

    // @todo search for QTPM_USE_WINDOWFONT in Qt3 for OS/2 sources for a
    // discussion on whether to use PM_Fonts/DefaultFont or WindowText as the
    // default one. So far, the latter is used.
    QApplicationPrivate::setSystemFont(qt_sysfont2qfont("WindowText"));

    // QFont::locale_init();  ### Uncomment when it does something on OS/2

    if (QApplication::desktopSettingsAware())
        qt_set_pm_resources();
}

/*****************************************************************************
  qt_cleanup() - cleans up when the application is finished
 *****************************************************************************/

void qt_cleanup()
{
    QPixmapCache::clear();

#ifndef QT_NO_CURSOR
    QCursorData::cleanup();
#endif
    QFont::cleanup();
    QColormap::cleanup();

    WinReleasePS(displayPS);
    displayPS = 0;

#ifdef QT_LOG_BLITSPEED
    extern unsigned long long qt_total_blit_ms;
    extern unsigned long long qt_total_blit_pixels;
    printf("*** qt_total_blit_ms      : %llu\n"
           "*** qt_total_blit_pixels  : %llu\n"
           "*** average speed, px/ms  : %llu\n",
           qt_total_blit_ms, qt_total_blit_pixels,
           qt_total_blit_pixels / qt_total_blit_ms);
#endif
}

/*****************************************************************************
  Platform specific global and internal functions
 *****************************************************************************/

Q_GUI_EXPORT HPS qt_display_ps()
{
    Q_ASSERT(qApp);
    if (!qApp)
        return NULLHANDLE;
    return displayPS;
}

Q_GUI_EXPORT int qt_display_width()
{
    // display resolution is constant for any given bootup
    static LONG displayWidth = 0;
    if (displayWidth == 0)
        displayWidth = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
    return displayWidth;
}

Q_GUI_EXPORT int qt_display_height()
{
    // display resolution is constant for any given bootup
    static LONG displayHeight = 0;
    if (displayHeight == 0)
        displayHeight = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    return displayHeight;
}

/*!
    Returns a QWidget pointer or 0 if there is no widget corresponding to the
    given HWND. As opposed to QWidget::find(), correctly handles WC_FRAME
    windows created for top level widgets.
 */
QWidget *qt_widget_from_hwnd(HWND hwnd)
{
    char buf[10];
    if (WinQueryClassName(hwnd, sizeof(buf), buf)) {
        if (!strcmp(buf, "#1")) // WC_FRAME
            hwnd = WinWindowFromID(hwnd, FID_CLIENT);
        return QWidget::find(hwnd);
    }
    return 0;
}

// application no-grab option
bool qt_nograb()
{
#if defined(QT_DEBUG)
    return appNoGrab;
#else
    return false;
#endif
}

/*****************************************************************************
  Safe configuration (move,resize,setGeometry) mechanism to avoid
  recursion when processing messages.
 *****************************************************************************/

struct QPMConfigRequest {
    WId         id; // widget to be configured
    int         req; // 0=move, 1=resize, 2=setGeo
    int         x, y, w, h; // request parameters
};

Q_GLOBAL_STATIC(QList<QPMConfigRequest*>, configRequests);

void qPMRequestConfig(WId id, int req, int x, int y, int w, int h)
{
    QPMConfigRequest *r = new QPMConfigRequest;
    r->id = id;
    r->req = req;
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
    configRequests()->append(r);
}

/*****************************************************************************
    GUI event dispatcher
 *****************************************************************************/

class QGuiEventDispatcherPM : public QEventDispatcherPM
{
public:
    QGuiEventDispatcherPM(QObject *parent = 0);
    bool processEvents(QEventLoop::ProcessEventsFlags flags);
};

QGuiEventDispatcherPM::QGuiEventDispatcherPM(QObject *parent)
    : QEventDispatcherPM(parent)
{
    // pre-create the message queue early as we'll need it anyway in GUI mode
    createMsgQueue();
}

bool QGuiEventDispatcherPM::processEvents(QEventLoop::ProcessEventsFlags flags)
{
    if (!QEventDispatcherPM::processEvents(flags))
        return false;

    QPMConfigRequest *r;
    for (;;) {
        if (configRequests()->isEmpty())
            break;
        r = configRequests()->takeLast();
        QWidget *w = QWidget::find(r->id);
        QRect rect(r->x, r->y, r->w, r->h);
        int req = r->req;
        delete r;

        if (w) { // widget exists
            if (w->testAttribute(Qt::WA_WState_ConfigPending))
                break; // biting our tail
            if (req == 0)
                w->move(rect.topLeft());
            else if (req == 1)
                w->resize(rect.size());
            else
                w->setGeometry(rect);
        }
    }

    return true;
}

void QApplicationPrivate::createEventDispatcher()
{
    Q_Q(QApplication);
    if (q->type() != QApplication::Tty)
        eventDispatcher = new QGuiEventDispatcherPM(q);
    else
        eventDispatcher = new QEventDispatcherPM(q);
}

/*****************************************************************************
  Platform specific QApplication members
 *****************************************************************************/

void QApplicationPrivate::initializeWidgetPaletteHash()
{
}

QString QApplicationPrivate::appName() const
{
    return QCoreApplicationPrivate::appName();
}

void  QApplication::setCursorFlashTime(int msecs)
{
    WinSetSysValue(HWND_DESKTOP, SV_CURSORRATE, msecs / 2);
    QApplicationPrivate::cursor_flash_time = msecs;
}

int QApplication::cursorFlashTime()
{
    int blink = (int)WinQuerySysValue(HWND_DESKTOP, SV_CURSORRATE);
    if (!blink)
        return QApplicationPrivate::cursor_flash_time;
    if (blink > 0)
        return 2 * blink;
    return 0;
}

void QApplication::setDoubleClickInterval(int ms)
{
    WinSetSysValue(HWND_DESKTOP, SV_DBLCLKTIME, ms);
    QApplicationPrivate::mouse_double_click_time = ms;
}

int QApplication::doubleClickInterval()
{
    int ms = (int) WinQuerySysValue(HWND_DESKTOP, SV_DBLCLKTIME);
    if (ms != 0)
        return ms;
    return QApplicationPrivate::mouse_double_click_time;
}

void QApplication::setKeyboardInputInterval(int ms)
{
    QApplicationPrivate::keyboard_input_time = ms;
}

int QApplication::keyboardInputInterval()
{
    // FIXME: get from the system
    return QApplicationPrivate::keyboard_input_time;
}

#ifndef QT_NO_WHEELEVENT
void QApplication::setWheelScrollLines(int n)
{
    QApplicationPrivate::wheel_scroll_lines = n;
}

int QApplication::wheelScrollLines()
{
    return QApplicationPrivate::wheel_scroll_lines;
}
#endif //QT_NO_WHEELEVENT

void QApplication::setEffectEnabled(Qt::UIEffect effect, bool enable)
{
    // @todo implement
}

bool QApplication::isEffectEnabled(Qt::UIEffect effect)
{
    // @todo implement
    return false;
}

void QApplication::beep()
{
    WinAlarm(HWND_DESKTOP, WA_WARNING);
}

void QApplication::alert(QWidget *widget, int duration)
{
    // @todo implement
}

/*****************************************************************************
  QApplication cursor stack
 *****************************************************************************/

#ifndef QT_NO_CURSOR

void QApplication::setOverrideCursor(const QCursor &cursor)
{
    qApp->d_func()->cursor_list.prepend(cursor);
    WinSetPointer(HWND_DESKTOP, qApp->d_func()->cursor_list.first().handle());
}

void QApplication::restoreOverrideCursor()
{
    if (qApp->d_func()->cursor_list.isEmpty())
        return;
    qApp->d_func()->cursor_list.removeFirst();

    if (!qApp->d_func()->cursor_list.isEmpty()) {
        WinSetPointer(HWND_DESKTOP, qApp->d_func()->cursor_list.first().handle());
    } else {
        QWidget *w = QWidget::find(curWin);
        if (w)
            WinSetPointer(HWND_DESKTOP, w->cursor().handle());
        else
            WinSetPointer(HWND_DESKTOP, QCursor(Qt::ArrowCursor).handle());
    }
}

/*
    Internal function called from QWidget::setCursor()

    force is true if this function is called from dispatchEnterLeave, it means
    that the mouse is actually directly under this widget.
*/
void qt_pm_set_cursor(QWidget *w, bool force)
{
    static QPointer<QWidget> lastUnderMouse = 0;
    if (force) {
        lastUnderMouse = w;
    } else if (w->testAttribute(Qt::WA_WState_Created) && lastUnderMouse
               && lastUnderMouse->effectiveWinId() == w->effectiveWinId()) {
        w = lastUnderMouse;
    }

    if (!curWin && w && w->internalWinId())
        return;
    QWidget* cW = w && !w->internalWinId() ? w : QWidget::find(curWin);
    if (!cW || cW->window() != w->window() ||
         !cW->isVisible() || !cW->underMouse() || QApplication::overrideCursor())
        return;

    WinSetPointer(HWND_DESKTOP, cW->cursor().handle());
}

#endif // QT_NO_CURSOR

/*****************************************************************************
  Routines to find a Qt widget from a screen position
 *****************************************************************************/

QWidget *QApplication::topLevelAt(const QPoint &pos)
{
    // flip y coordinate
    int y = qt_display_height() - (pos.y() + 1);
    POINTL ptl = { pos.x(), y };
    HWND hwnd = WinWindowFromPoint(HWND_DESKTOP, &ptl, FALSE);
    if (hwnd == NULLHANDLE)
        return 0;

    QWidget *w = qt_widget_from_hwnd(hwnd);
    return w ? w->window() : 0;
}

/*****************************************************************************
  Main event loop
 *****************************************************************************/

// sent to hwnd that has been entered to by a mouse pointer.
// FID_CLIENT also receives enter messages of its WC_FRAME.
// mp1 = hwnd that is entered, mp2 = hwnd that is left
#define WM_U_MOUSEENTER 0x41E
// sent to hwnd that has been left by a mouse pointer.
// FID_CLIENT also receives leave messages of its WC_FRAME.
// mp1 = hwnd that is left, mp2 = hwnd that is entered
#define WM_U_MOUSELEAVE 0x41F

// some undocumented system values
#define SV_WORKAREA_YTOP    51
#define SV_WORKAREA_YBOTTOM 52
#define SV_WORKAREA_XRIGHT  53
#define SV_WORKAREA_XLEFT   54

/*!
    \internal
    \since 4.1

    If \a gotFocus is true, \a widget will become the active window.
    Otherwise the active window is reset to 0.
*/
void QApplication::pmFocus(QWidget *widget, bool gotFocus)
{
    if (gotFocus) {
        setActiveWindow(widget);
        if (QApplicationPrivate::active_window
            && (QApplicationPrivate::active_window->windowType() == Qt::Dialog)) {
            // raise the entire application, not just the dialog
            QWidget* mw = QApplicationPrivate::active_window;
            while(mw->parentWidget() && (mw->windowType() == Qt::Dialog))
                mw = mw->parentWidget()->window();
            if (mw->testAttribute(Qt::WA_WState_Created) && mw != QApplicationPrivate::active_window)
                WinSetWindowPos(mw->d_func()->frameWinId(), HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
        }
    } else {
        setActiveWindow(0);
    }
}

// QMSG wrapper that translates message coordinates from PM to Qt
struct QtQmsg : public QMSG
{
    QtQmsg(HWND aHwnd, ULONG aMsg, MPARAM aMp1, MPARAM aMp2)
    {
        hwnd = aHwnd;
        msg = aMsg;
        mp1 = aMp1;
        mp2 = aMp2;
        time = WinQueryMsgTime(0);

        isTranslatableMouseEvent =
            (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            (msg >= WM_EXTMOUSEFIRST && msg <= WM_EXTMOUSELAST);

        if (isTranslatableMouseEvent || msg == WM_CONTEXTMENU) {
            ptl.x = (short)SHORT1FROMMP(mp1);
            ptl.y = (short)SHORT2FROMMP(mp1);
            WinMapWindowPoints(hwnd, HWND_DESKTOP, &ptl, 1);
        } else {
            WinQueryMsgPos(0, &ptl);
        }
        // flip y coordinate
        ptl.y = qt_display_height() - (ptl.y + 1);
    }

    bool isTranslatableMouseEvent;
};

// QtWndProc() receives all messages from the main event loop

MRESULT EXPENTRY QtWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    do {
        if (!qApp) // unstable app state
            break;
#if 0
        // make sure we show widgets (e.g. scrollbars) when the user resizes
        if (qApp->loopLevel())
            qApp->sendPostedEvents(0, QEvent::ShowWindowRequest);
#endif

        MRESULT rc = (MRESULT) FALSE;
        QETWidget *widget = 0;

        QtQmsg qmsg(hwnd, msg, mp1, mp2);

#if defined(QT_DEBUGMSGFLOW)
        if (qmsg.msg != WM_QUERYICON)
            qDebug() << "*** [W]" << qmsg;
#endif

        // send through app filter
        if (qApp->filterEvent(&qmsg, reinterpret_cast<long *>(&rc)))
            return rc;

        switch(msg) {

#if !defined(QT_NO_SESSIONMANAGER)
        case WM_SAVEAPPLICATION: {
#if defined(DEBUG_SESSIONMANAGER)
            qDebug("WM_SAVEAPPLICATION: sm_gracefulShutdown %d, "
                   "qt_about_to_destroy_wnd %d, (mp1 %p, mp2 %p)",
                   sm_gracefulShutdown, qt_about_to_destroy_wnd, mp1, mp2);
#endif
            // PM seems to post this message to all top-level windows on system
            // shutdown, so react only to the first one. Also, this message is
            // always sent by WinDestroyWindow(), where it must be also ignored.
            if (!qt_about_to_destroy_wnd && !sm_smActive &&
                 !sm_gracefulShutdown) {
                sm_smActive = true;
                sm_gracefulShutdown = true;
                sm_blockUserInput = true; // prevent user-interaction outside interaction windows
                sm_cancel = false;
                sm_quitSkipped = false;
                if (qt_session_manager_self)
                    qApp->commitData(*qt_session_manager_self);
                sm_smActive = false; // session management has been finished
                if (sm_cancel) {
#if defined(DEBUG_SESSIONMANAGER)
                    qDebug("WM_SAVEAPPLICATION: sm_cancel %d", sm_cancel);
#endif
                    // @todo propagate the patch that does the below to XWP
                    // and enable the code when it appears upstream (see #100)
#if 0
                    // Here we try to cancel the Extended XWorkplace shutdown.
                    // If it's XWorkplace who sent us WM_SAVEAPPLICATION, then
                    // it probably passed us non-NULL parameters so that
                    // mp1 = its window handle and mp2 = WM_COMMAND code to
                    // cancel the shutdown procedure.
                    HWND shutdownHwnd = HWNDFROMMP(mp1);
                    if (WinIsWindow(0, shutdownHwnd)) {
                        WinPostMsg(shutdownHwnd, WM_COMMAND, mp2, 0);
                        // Ensure we will get WM_QUIT anyway, even if XWP was
                        // not that fast to post it yet (we need it to correctly
                        // finish the graceful shutdown procedure)
                        sm_quitSkipped = true;
                    }
#endif
                }
                // repost WM_QUIT to ourselves because we might have ignored
                // it in qt_app_canQuit(), so will not get one anymore
                if (sm_quitSkipped)
                    WinPostMsg(hwnd, WM_QUIT, 0, 0);
            }
            // PMREF recommends to pass it to WinDefWindowProc()
            return WinDefWindowProc(hwnd, msg, mp1, mp2);
        }
#endif

        case WM_SYSVALUECHANGED: {
            // This message is sent to all top-level widgets, handle only once
            QWidgetList list = QApplication::topLevelWidgets();
            bool firstWidget = list.first()->winId() == hwnd;
            if (!firstWidget)
                break;
            LONG from = (LONG) mp1;
            LONG to = (LONG) mp2;
            #define MY_IS_SV(sv) (from >= (sv) && to <= (sv))
            if (MY_IS_SV(SV_WORKAREA_XLEFT) || MY_IS_SV(SV_WORKAREA_XRIGHT) ||
                MY_IS_SV(SV_WORKAREA_YBOTTOM) || MY_IS_SV(SV_WORKAREA_YTOP)) {
                 // send a special invalid resize event to QDesktopWidget
                 QApplication::sendEvent(qt_desktopWidget, 0);
                 // @todo enumerate all top-level widgets and
                 // remaximize those that are maximized
            } else {
                 /// @todo call qt_set_pm_resources() in the way it is
                 //  done in WM_SYSCOLORCHANGE for relevant SV_ values.
            }
            #undef MY_IS_SV
            break;
        }

        case WM_SYSCOLORCHANGE: {
            // This message is sent to all top-level widgets, handle only once
            QWidgetList list = QApplication::topLevelWidgets();
            bool firstWidget = list.first()->winId() == hwnd;
            if (!firstWidget)
                break;
            if (qApp->type() == QApplication::Tty)
                break;
            if (QApplication::desktopSettingsAware())
                    qt_set_pm_resources();
            break;
        }

        case WM_BUTTON1DOWN:
        case WM_BUTTON2DOWN:
        case WM_BUTTON3DOWN:
            if (ignoreNextMouseReleaseEvent)
                ignoreNextMouseReleaseEvent = false;
            break;
        case WM_BUTTON1UP:
        case WM_BUTTON2UP:
        case WM_BUTTON3UP:
            if (ignoreNextMouseReleaseEvent) {
                ignoreNextMouseReleaseEvent = false;
                if (qt_button_down && qt_button_down->internalWinId() == autoCaptureWnd) {
                    releaseAutoCapture();
                    qt_button_down = 0;
                }
                return (MRESULT)TRUE;
            }
            break;

        default:
            break;
        }

        if (!widget)
            widget = (QETWidget*)QWidget::find(hwnd);
        if (!widget) // don't know this widget
            break;

        if (app_do_modal) { // modal event handling
            if (!qt_try_modal(widget, &qmsg, rc))
                return rc;
        }

        if (widget->xtra()) { // send through widget filter list
            foreach(QWidget::PmEventFilter *filter, widget->xtra()->pmEventFilters) {
                if (filter->pmEventFilter(&qmsg, &rc))
                    return rc;
            }
        }

        if (widget->pmEvent(&qmsg, &rc)) // send through widget filter
            return rc;

        if (qmsg.isTranslatableMouseEvent) {
            if (qApp->activePopupWidget() != 0) { // in popup mode
                QWidget *w = QApplication::widgetAt(qmsg.ptl.x, qmsg.ptl.y);
                if (w)
                    widget = (QETWidget*)w;
            }
            if (widget->translateMouseEvent(qmsg)) // mouse event
                return (MRESULT)TRUE;
#ifndef QT_NO_WHEELEVENT
        } else if (msg == WM_VSCROLL || msg == WM_HSCROLL) {
            if (widget->translateWheelEvent(qmsg))
                return (MRESULT)TRUE;
#endif
#ifndef QT_NO_DRAGANDDROP
        } else if (msg >= WM_DRAGFIRST && msg <= WM_DRAGLAST) {
            return QDragManager::self()->dispatchDragAndDrop(widget, qmsg);
#endif
        } else {
            switch(msg) {

            case WM_TRANSLATEACCEL: {
                if (widget->isWindow()) {
                    rc = WinDefWindowProc(hwnd, msg, mp1, mp2);
                    if (rc) {
                        QMSG &qmsg = *(QMSG*)mp1;
                        if (qmsg.msg == WM_SYSCOMMAND &&
                            WinWindowFromID(widget->internalFrameWinId(),
                                            FID_SYSMENU)) {
                            switch (SHORT1FROMMP(qmsg.mp1)) {
                                case SC_CLOSE:
                                case SC_TASKMANAGER:
                                    return (MRESULT)TRUE;
                                default:
                                    break;
                            }
                        }
                    }
                }
                // return FALSE in all other cases to let Qt process keystrokes
                // that are in the system-wide frame accelerator table.
                return FALSE;
            }

            case WM_CHAR: { // keyboard event
                if (!(CHARMSG(&qmsg.msg)->fs & KC_KEYUP))
                    qt_keymapper_private()->updateKeyMap(qmsg);

                QWidget *g = QWidget::keyboardGrabber();
                if (g)
                    widget = (QETWidget*)g;
                else if (QApplication::activePopupWidget())
                    widget = (QETWidget*)QApplication::activePopupWidget()->focusWidget()
                           ? (QETWidget*)QApplication::activePopupWidget()->focusWidget()
                           : (QETWidget*)QApplication::activePopupWidget();
                else if (qApp->focusWidget())
                    widget = (QETWidget*)QApplication::focusWidget();
                else if (widget->internalWinId() == WinQueryFocus(HWND_DESKTOP)) {
                    // We faked the message to go to exactly that widget.
                    widget = (QETWidget*)widget->window();
                }
                if (widget->isEnabled()) {
                    bool result =
#if !defined (QT_NO_SESSIONMANAGER)
                        sm_blockUserInput ? true :
#endif
                        qt_keymapper_private()->translateKeyEvent(widget, qmsg, g != 0);
                    if (result)
                        return (MRESULT)TRUE;
                }
                break;
            }

            case WM_KBDLAYERCHANGED: { // Keyboard layout change
                QKeyMapper::changeKeyboard();
                break;
            }

            case WM_QUERYCONVERTPOS: { // IME input box position request
                // @todo the proper detection of the caret position in the
                // current widget requires implementing QInputContext. For now,
                // just send the IME box to the lower left corner of the
                // top-level window
                PRECTL pcp = (PRECTL)mp1;
                memset(pcp, 0xFF, sizeof(RECTL));
                pcp->xLeft = 0;
                pcp->yBottom = 0;
                return (MRESULT)QCP_CONVERT;
            }

            case WM_PAINT: { // paint event
                if (widget->translatePaintEvent(qmsg))
                    return (MRESULT)TRUE;
                break;
            }

            case WM_ERASEBACKGROUND: { // erase window background
                // flush WM_PAINT messages here to update window contents
                // instantly while tracking the resize frame (normally these
                // messages are delivered after the user has stopped resizing
                // for some time). this slows down resizing slightly but gives a
                // better look (no invalid window contents can be seen during
                // resize). the alternative could be to erase the background only,
                // but we need to do it for every non-toplevel window, which can
                // also be time-consuming (WM_ERASEBACKGROUND is sent to WC_FRAME
                // clients only, so we would have to do all calculations ourselves).
                WinUpdateWindow(widget->effectiveWinId());
                return FALSE;
            }

            case WM_CALCVALIDRECTS: {
                // we must always return this value here to cause PM to reposition
                // our children accordingly (othwerwise we would have to do it
                // ourselves to keep them top-left aligned).
                return (MRESULT)(CVR_ALIGNLEFT | CVR_ALIGNTOP);
            }

            case WM_SIZE: { // resize window
                if (widget->translateConfigEvent(qmsg))
                    return (MRESULT)TRUE;
                break;
            }

            case WM_ACTIVATE: {
                qApp->pmFocus(widget, SHORT1FROMMP(mp1));
                break;
            }

            case WM_FOCUSCHANGE: {
                HWND hwnd = (HWND)mp1;
                bool focus = SHORT1FROMMP(mp2);
                if (!focus) {
                    if (!QWidget::find(hwnd) && !QDragManager::self()->isInOwnDrag()) {
                        // we gave up focus to another application, unset Qt
                        // focus (exclude the situation when we've started the
                        // drag in which case the PM focus goes to the drag
                        // bitmap but the Qt focus should stay with us)
                        if (QApplication::activePopupWidget()) {
                            foreignFocusWnd = hwnd;
                            // Another application was activated while our popups are open,
                            // then close all popups.  In case some popup refuses to close,
                            // we give up after 1024 attempts (to avoid an infinite loop).
                            int maxiter = 1024;
                            QWidget *popup;
                            while ((popup=QApplication::activePopupWidget()) && maxiter--)
                                popup->close();
                        }
                        // non-Qt ownees of our WC_FRAME window (such as
                        // FID_SYSMENU) should not cause the focus to be lost.
                        if (WinQueryWindow(hwnd, QW_OWNER) ==
                                ((QETWidget*)widget->window())->frameWinId())
                            break;
                        if (!widget->isWindow())
                            qApp->pmFocus(widget, focus);
                    }
                }
                break;
            }

            case WM_SHOW: {
                // @todo there is some more processing in Qt4, see
                // WM_SHOWWINDOW in qapplication_win.cpp
                if (!SHORT1FROMMP(mp1) && autoCaptureWnd == widget->internalWinId())
                    releaseAutoCapture();
                break;
            }

            case WM_CLOSE: { // close window
                widget->translateCloseEvent(qmsg);
                return (MRESULT)TRUE;
            }

            case WM_DESTROY: { // destroy window
                if (hwnd == curWin) {
                    QWidget *enter = QWidget::mouseGrabber();
                    if (enter == widget)
                        enter = 0;
                    QApplicationPrivate::dispatchEnterLeave(enter, widget);
                    curWin = enter ? enter->effectiveWinId() : 0;
                    qt_last_mouse_receiver = enter;
                }
                if (widget == popupButtonFocus)
                    popupButtonFocus = 0;
                break;
            }

#ifndef QT_NO_CONTEXTMENU
            case WM_CONTEXTMENU: {
                if (SHORT2FROMMP(mp2)) {
                    // keyboard event
                    QWidget *fw = qApp->focusWidget();
                    if (fw && fw->isEnabled()) {
                        QContextMenuEvent e(QContextMenuEvent::Keyboard,
                                            QPoint(5, 5),
                                            fw->mapToGlobal(QPoint(5, 5)), 0);
                        if (qt_sendSpontaneousEvent(fw, &e))
                            return (MRESULT)TRUE;
                    }
                } else {
                    // mouse event
                    if (widget->translateMouseEvent(qmsg))
                        return (MRESULT)TRUE;
                }
                break;
            }
#endif

            case WM_U_MOUSELEAVE: {
                // mp1 = hwndFrom, mp2 = hwndTo
                if (hwnd != (HWND)mp1) {
                    // this must be a LEAVE message from one of the frame
                    // controls forwarded by WC_FRAME to FID_CLIENT; ignore it
                    // as it's doesn't actually belong to the given hwnd
                    break;
                }
                // We receive a mouse leave for curWin, meaning the mouse was
                // moved outside our widgets (in any other case curWin will be
                // already set to a different value in response to WM_MOUSEMOVE
                // in translateMouseEvent())
                if (widget->internalWinId() == curWin) {
                    bool dispatch = !widget->underMouse();
                    // hasMouse is updated when dispatching enter/leave,
                    // so test if it is actually up-to-date
                    if (!dispatch) {
                        QRect geom = widget->geometry();
                        if (widget->parentWidget() && !widget->isWindow()) {
                            QPoint gp = widget->parentWidget()->mapToGlobal(widget->pos());
                            geom.setX(gp.x());
                            geom.setY(gp.y());
                        }
                        QPoint cpos = QCursor::pos();
                        dispatch = !geom.contains(cpos);
                        if ( !dispatch && !QWidget::mouseGrabber()) {
                            QWidget *hittest = QApplication::widgetAt(cpos);
                            dispatch = !hittest || hittest->internalWinId() != curWin;
                        }
                        if (!dispatch) {
#ifdef QT_PM_NATIVEWIDGETMASK
                            HPS hps = qt_display_ps();
                            HRGN hrgn = GpiCreateRegion(hps, 0, NULL);
                            qt_WinQueryClipRegionOrRect(hwnd, hrgn);
                            QPoint lcpos = widget->mapFromGlobal(cpos);
                            // flip y coordinate
                            POINTL pt = { lcpos.x(), widget->height() - (lcpos.y() + 1) };
                            dispatch = !GpiPtInRegion(hps, hrgn, &pt);
                            GpiDestroyRegion(hps, hrgn);
#else
                            QPoint lcpos = widget->mapFromGlobal(cpos);
                            QRect rect(0, 0, widget->width(), widget->height());
                            dispatch = !rect.contains(lcpos);
#endif
                        }
                    }
                    if (dispatch) {
                        if (qt_last_mouse_receiver && !qt_last_mouse_receiver->internalWinId())
                            QApplicationPrivate::dispatchEnterLeave(0, qt_last_mouse_receiver);
                        else
                            QApplicationPrivate::dispatchEnterLeave(0, QWidget::find((WId)curWin));
                        curWin = 0;
                        qt_last_mouse_receiver = 0;
                    }
                }
                break;
            }

            case WM_MINMAXFRAME: {
                PSWP pswp = (PSWP)mp1;

                bool window_state_change = false;
                Qt::WindowStates oldstate = Qt::WindowStates(widget->dataPtr()->window_state);

                if (pswp->fl & SWP_MINIMIZE) {
                    window_state_change = true;
                    widget->dataPtr()->window_state |= Qt::WindowMinimized;
                    if (widget->isVisible()) {
                        QHideEvent e;
                        qt_sendSpontaneousEvent(widget, &e);
                        widget->dptr()->hideChildren(true);
                        const QString title = widget->windowIconText();
                        if (!title.isEmpty())
                            widget->dptr()->setWindowTitle_helper(title);
                    }
                } else if (pswp->fl & (SWP_MAXIMIZE | SWP_RESTORE)) {
                    window_state_change = true;
                    if (pswp->fl & SWP_MAXIMIZE) {
                        widget->topData()->normalGeometry = widget->geometry();
                        widget->dataPtr()->window_state |= Qt::WindowMaximized;
                    }
                    else if (!widget->isMinimized()) {
                        widget->dataPtr()->window_state &= ~Qt::WindowMaximized;
                    }

                    if (widget->isMinimized()) {
                        widget->dataPtr()->window_state &= ~Qt::WindowMinimized;
                        widget->dptr()->showChildren(true);
                        QShowEvent e;
                        qt_sendSpontaneousEvent(widget, &e);
                        const QString title = widget->windowTitle();
                        if (!title.isEmpty())
                            widget->dptr()->setWindowTitle_helper(title);
                    }
                }

                if (window_state_change) {
                    QWindowStateChangeEvent e(oldstate);
                    qt_sendSpontaneousEvent(widget, &e);
                }

                break;
            }

            default:
                break;
            }
        }

    } while(0);

    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

static void qt_pull_blocked_widgets(QETWidget *widget)
{
    QWidgetList list = QApplication::topLevelWidgets();
    foreach (QWidget *w, list) {
        QETWidget *ew = (QETWidget *)w;
        QETWidget *above = (QETWidget*)ew->modalBlocker();
        if (above == widget) {
            WinSetWindowPos(ew->frameWinId(), widget->frameWinId(),
                            0, 0, 0, 0, SWP_ZORDER);
            qt_pull_blocked_widgets(ew);
        }
    }
}

PFNWP QtOldFrameProc = 0;

MRESULT EXPENTRY QtFrameProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static bool pullingBlockedWigets = false;

    do {
        if (!qApp) // unstable app state
            break;
#if 0
        // make sure we show widgets (e.g. scrollbars) when the user resizes
        if (qApp->loopLevel())
            qApp->sendPostedEvents(0, QEvent::ShowWindowRequest);
#endif

        MRESULT rc = (MRESULT) FALSE;

        HWND hwndC = WinWindowFromID(hwnd, FID_CLIENT);
        QETWidget *widget = (QETWidget*)QWidget::find(hwndC);
        if (!widget) // don't know this widget
            break;

        Q_ASSERT(widget->isWindow());

        QtQmsg qmsg(hwnd, msg, mp1, mp2);

#if defined(QT_DEBUGMSGFLOW)
        if (qmsg.msg != WM_QUERYICON)
            qDebug() << "*** [F]" << qmsg;
#endif

        if (qmsg.isTranslatableMouseEvent)
            widget->translateNonClientMouseEvent(qmsg);

        switch(msg) {

        case WM_MOVE: { // move window
            // note that we handle it nere instead of having CS_MOVENOTIFY
            // on the client and handling in QtWndProc because we don't want
            // client inside frame window move messages that would appear during
            // minimize/maximize otherwise
            if (widget->translateConfigEvent(qmsg))
                return (MRESULT)TRUE;
            break;
        }

        case WM_ADJUSTWINDOWPOS: {
            // If PM tries to activate a modally blocked window, pass activation
            // over to the top modal window instead of letting it activate the
            // blocked one
            PSWP pswp =(PSWP)qmsg.mp1;
            if (app_do_modal &&
                ((pswp->fl & SWP_ACTIVATE) || (pswp->fl & SWP_ZORDER))) {
                QETWidget *above = (QETWidget *)widget->modalBlocker();
                if (above) {
                    if (pswp->fl & SWP_ACTIVATE) {
                        // find the blocker that isn't blocked itself and
                        // delegate activation to it (WM_WINDOWPOSCHANGED will
                        // care about the rest)
                        while (above->modalBlocker())
                            above = above->modalBlocker();
                        WinSetWindowPos(above->frameWinId(), HWND_TOP, 0, 0, 0, 0,
                                        SWP_ACTIVATE | SWP_ZORDER);
                        pswp->fl &= ~SWP_ACTIVATE;
                    }
                    if (pswp->fl & SWP_ZORDER) {
                        // forbid changing the Z-order of the blocked widget
                        // (except when it is moved right under its blocker)
                        if (pswp->hwndInsertBehind != above->frameWinId())
                            pswp->fl &= ~SWP_ZORDER;
                    }
                }
            }
            break;
        }

        case WM_WINDOWPOSCHANGED: {
            PSWP pswp =(PSWP)qmsg.mp1;
            // make sure blocked widgets stay behind the blocker on Z-order change
            if (app_do_modal && (pswp->fl & SWP_ZORDER) && !pullingBlockedWigets) {
                // protect against various kinds of recursion (e.g. when the
                // blocked widgets are PM owners of the blocker in which case PM
                // manages pulling on its own and will put us in a loop)
                pullingBlockedWigets = true;
                qt_pull_blocked_widgets(widget);
                pullingBlockedWigets = false;
            }
            break;
        }

        case WM_QUERYTRACKINFO: {
            QWExtra *x = widget->xtra();
            if (x) {
                rc = QtOldFrameProc(hwnd, msg, mp1, mp2);
                PTRACKINFO pti = (PTRACKINFO) mp2;
                int minw = 0, minh = 0;
                int maxw = QWIDGETSIZE_MAX, maxh = QWIDGETSIZE_MAX;
                QRect fs = widget->frameStrut();
                if (x->minw > 0)
                    minw = x->minw + fs.left() + fs.right();
                if (x->minh > 0)
                    minh = x->minh + fs.top() + fs.bottom();
                if (x->maxw < QWIDGETSIZE_MAX)
                    maxw = x->maxw > x->minw ? x->maxw + fs.left() + fs.right() : minw;
                if (x->maxh < QWIDGETSIZE_MAX)
                    maxh = x->maxh > x->minh ? x->maxh + fs.top() + fs.bottom() : minh;
                // obey system recommended minimum size (to emulate Qt/Win32)
                pti->ptlMinTrackSize.x = qMax<LONG>(minw, pti->ptlMinTrackSize.x);
                pti->ptlMinTrackSize.y = qMax<LONG>(minh, pti->ptlMinTrackSize.y);
                // we assume that PM doesn't restrict the maximum size by default
                // and use it as an absolute maximum (values above it will cause
                // PM to disable window sizing and moving for some reason)
                pti->ptlMaxTrackSize.x = qMin<LONG>(maxw, pti->ptlMaxTrackSize.x);
                pti->ptlMaxTrackSize.y = qMin<LONG>(maxh, pti->ptlMaxTrackSize.y);
                return rc;
            }
            break;
        }

        case WM_TRACKFRAME: {
            if (QApplication::activePopupWidget()) {
                // Another application was activated while our popups are open,
                // then close all popups.  In case some popup refuses to close,
                // we give up after 1024 attempts (to avoid an infinite loop).
                int maxiter = 1024;
                QWidget *popup;
                while ((popup=QApplication::activePopupWidget()) && maxiter--)
                    popup->close();
            }
            break;
        }

        default:
            break;
        }
    } while(0);

    return QtOldFrameProc(hwnd, msg, mp1, mp2);
}

PFNWP QtOldFrameCtlProcs[FID_HORZSCROLL - FID_SYSMENU + 1] = { 0 };

MRESULT EXPENTRY QtFrameCtlProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // sanity: this procedure is only for standard frame controls
    ULONG id = (ULONG)WinQueryWindowUShort(hwnd, QWS_ID);
    Q_ASSERT(id >= FID_SYSMENU && id <= FID_HORZSCROLL);
    Q_ASSERT(QtOldFrameCtlProcs[id - FID_SYSMENU]);
    if (id < FID_SYSMENU || id > FID_HORZSCROLL ||
        !QtOldFrameCtlProcs[id - FID_SYSMENU])
        return FALSE;

    do {
        if (!qApp) // unstable app state
            break;

        HWND hwndF = WinQueryWindow(hwnd, QW_PARENT);
        HWND hwndC = WinWindowFromID(hwndF, FID_CLIENT);
        QETWidget *widget = (QETWidget*)QWidget::find(hwndC);
        if (!widget) // don't know this widget
            break;

        Q_ASSERT(widget->isWindow());

        QtQmsg qmsg(hwnd, msg, mp1, mp2);

#if defined(QT_DEBUGMSGFLOW)
        if (qmsg.msg != WM_QUERYICON)
            qDebug() << "*** [FC]" << qmsg;
#endif

        if (qmsg.isTranslatableMouseEvent)
            widget->translateNonClientMouseEvent(qmsg);

    } while(0);

    // call the original window procedure of this frame control
    return QtOldFrameCtlProcs[id - FID_SYSMENU](hwnd, msg, mp1, mp2);
}

/*****************************************************************************
  Modal widgets; We have implemented our own modal widget mechanism
  to get total control.
  A modal widget without a parent becomes application-modal.
  A modal widget with a parent becomes modal to its parent and grandparents..

  QApplicationPrivate::enterModal()
        Enters modal state
        Arguments:
            QWidget *widget        A modal widget

  QApplicationPrivate::leaveModal()
        Leaves modal state for a widget
        Arguments:
            QWidget *widget        A modal widget
 *****************************************************************************/

bool QApplicationPrivate::modalState()
{
    return app_do_modal;
}

void QApplicationPrivate::enterModal_sys(QWidget *widget)
{
    if (!qt_modal_stack)
        qt_modal_stack = new QWidgetList;

    releaseAutoCapture();
    QWidget *leave = qt_last_mouse_receiver;
    if (!leave)
        leave = QWidget::find(curWin);
    QApplicationPrivate::dispatchEnterLeave(0, leave);
    qt_modal_stack->insert(0, widget);
    app_do_modal = true;
    curWin = 0;
    qt_last_mouse_receiver = 0;
    ignoreNextMouseReleaseEvent = false;

    // go through all top-level widgets and disable those that should be blocked
    // by the modality (this will effectively disable frame controls and make
    // it impossible to manipulate the window state, position and size, even
    // programmatically, e.g. from the Window List)
    QWidgetList list = QApplication::topLevelWidgets();
    foreach (QWidget *w, list) {
        if (w != widget && QApplicationPrivate::isBlockedByModal(w)) {
            if (!w->d_func()->modalBlocker()) {
                WinEnableWindow(w->d_func()->frameWinId(), FALSE);
                // also remember the blocker widget so that the blocked one can
                // pass activation attempts to it (note that the blocker is not
                // necessarily modal itself, it's just the window right above
                // the one being blocked, we will use this to restore z-order of
                // of blocked widgets in qt_pull_blocked_widgets())
                QWidget *widgetAbove = 0;
                HWND above = w->d_func()->frameWinId();
                while (!widgetAbove) {
                    above = WinQueryWindow(above, QW_PREV);
                    if (!above)
                        break;
                    widgetAbove = qt_widget_from_hwnd(above);
                }
                if (!widgetAbove)
                    widgetAbove = widget;
                w->d_func()->setModalBlocker(widgetAbove);
            }
        }
    }
}

void QApplicationPrivate::leaveModal_sys(QWidget *widget)
{
    if (qt_modal_stack) {
        if (qt_modal_stack->removeAll(widget)) {
            if (qt_modal_stack->isEmpty()) {
                delete qt_modal_stack;
                qt_modal_stack = 0;
                QPoint p(QCursor::pos());
                app_do_modal = false; // necessary, we may get recursively into qt_try_modal below
                QWidget* w = QApplication::widgetAt(p.x(), p.y());
                QWidget *leave = qt_last_mouse_receiver;
                if (!leave)
                    leave = QWidget::find(curWin);
                if (QWidget *grabber = QWidget::mouseGrabber()) {
                    w = grabber;
                    if (leave == w)
                        leave = 0;
                }
                QApplicationPrivate::dispatchEnterLeave(w, leave); // send synthetic enter event
                curWin = w ? w->effectiveWinId() : 0;
                qt_last_mouse_receiver = w;
            }
        }
        ignoreNextMouseReleaseEvent = true;

        // go through all top-level widgets and re-enable those that are not
        // blocked any more
        QWidgetList list = QApplication::topLevelWidgets();
        foreach(QWidget *w, list) {
            if (w != widget && !QApplicationPrivate::isBlockedByModal(w)) {
                if (w->d_func()->modalBlocker()) {
                    w->d_func()->setModalBlocker(0);
                    WinEnableWindow(w->d_func()->frameWinId(), TRUE);
                }
            }
        }
    }
    app_do_modal = qt_modal_stack != 0;
}

bool qt_try_modal(QWidget *widget, QMSG *qmsg, MRESULT &rc)
{
    QWidget *top = 0;

    if (QApplicationPrivate::tryModalHelper(widget, &top))
        return true;

    int type = qmsg->msg;

    bool block_event = false;
    if ((type >= WM_MOUSEFIRST && type <= WM_MOUSELAST) ||
        (type >= WM_EXTMOUSEFIRST && type <= WM_EXTMOUSELAST) ||
         type == WM_VSCROLL || type == WM_HSCROLL ||
         type == WM_U_MOUSELEAVE ||
         type == WM_CHAR) {
        if (type == WM_MOUSEMOVE) {
#ifndef QT_NO_CURSOR
            QCursor *c = qt_grab_cursor();
            if (!c)
                c = QApplication::overrideCursor();
            if (c) // application cursor defined
                WinSetPointer(HWND_DESKTOP, c->handle());
            else
                WinSetPointer(HWND_DESKTOP, QCursor(Qt::ArrowCursor).handle());
#endif // QT_NO_CURSOR
        } else if (type == WM_BUTTON1DOWN || type == WM_BUTTON2DOWN ||
                   type == WM_BUTTON3DOWN) {
            if (!top->isActiveWindow()) {
                top->activateWindow();
            } else {
                QApplication::beep();
            }
        }
        block_event = true;
    } else if (type == WM_CLOSE) {
        block_event = true;
    } else if (type == WM_SYSCOMMAND) {
        if (!(SHORT1FROMMP(qmsg->mp1) == SC_RESTORE && widget->isMinimized()))
            block_event = true;
    }

    return !block_event;
}

/*****************************************************************************
  Popup widget mechanism

  openPopup()
        Adds a widget to the list of popup widgets
        Arguments:
            QWidget *widget        The popup widget to be added

  closePopup()
        Removes a widget from the list of popup widgets
        Arguments:
            QWidget *widget        The popup widget to be removed
 *****************************************************************************/

void QApplicationPrivate::openPopup(QWidget *popup)
{
    if (!QApplicationPrivate::popupWidgets)
        QApplicationPrivate::popupWidgets = new QWidgetList;
    QApplicationPrivate::popupWidgets->append(popup);
    if (!popup->isEnabled())
        return;

    if (QApplicationPrivate::popupWidgets->count() == 1) {
        if (!qt_nograb()) {
            Q_ASSERT(popup->testAttribute(Qt::WA_WState_Created));
            setAutoCapture(popup->d_func()->frameWinId()); // grab mouse/keyboard
        }
        if (!qt_widget_from_hwnd(WinQueryActiveWindow(HWND_DESKTOP))) {
            // the popup is opened while another application is active. Steal
            // the focus (to receive keyboard input and to make sure we get
            // WM_FOCUSCHANGE when clicked outside) but not the active state.
            ULONG flags = FC_NOSETACTIVE | FC_NOLOSEACTIVE;
            WinFocusChange(HWND_DESKTOP, popup->d_func()->frameWinId(), flags);
            foreignActiveWnd = WinQueryActiveWindow(HWND_DESKTOP);
        }
    }

    // Popups are not focus-handled by the window system (the first
    // popup grabbed the keyboard), so we have to do that manually: A
    // new popup gets the focus
    if (popup->focusWidget()) {
        popup->focusWidget()->setFocus(Qt::PopupFocusReason);
    } else if (QApplicationPrivate::popupWidgets->count() == 1) { // this was the first popup
        if (QWidget *fw = q_func()->focusWidget()) {
            QFocusEvent e(QEvent::FocusOut, Qt::PopupFocusReason);
            q_func()->sendEvent(fw, &e);
        }
    }
}

void QApplicationPrivate::closePopup(QWidget *popup)
{
    if (!QApplicationPrivate::popupWidgets)
        return;
    QApplicationPrivate::popupWidgets->removeAll(popup);
    POINTL curPos;
    WinQueryPointerPos(HWND_DESKTOP, &curPos);
    // flip y coordinate
    curPos.y = qt_display_height() - (curPos.y + 1);

    if (QApplicationPrivate::popupWidgets->isEmpty()) {
        // this was the last popup
        if (foreignActiveWnd != NULLHANDLE && foreignFocusWnd != NULLHANDLE) {
            // we stole focus from another application so PM won't send it
            // WM_ACTIVATE(FALSE). Our duty is to do it for PM.
            HWND parent, desktop = WinQueryDesktopWindow(0, NULLHANDLE);
            // find the top-level window of the window actually getting focus
            while ((parent = WinQueryWindow(foreignFocusWnd, QW_PARENT)) != desktop)
                foreignFocusWnd = parent;
            // send deactivation to the old active window if it differs
            if (foreignFocusWnd != foreignActiveWnd)
                WinSendMsg(foreignActiveWnd, WM_ACTIVATE, (MPARAM)FALSE, (MPARAM)foreignActiveWnd);
        }
        foreignActiveWnd = NULLHANDLE;
        foreignFocusWnd = NULLHANDLE;

        delete QApplicationPrivate::popupWidgets;
        QApplicationPrivate::popupWidgets = 0;
        replayPopupMouseEvent = (!popup->geometry().contains(QPoint(curPos.x, curPos.y))
                                && !popup->testAttribute(Qt::WA_NoMouseReplay));
        if (!popup->isEnabled())
            return;
        if (!qt_nograb())                        // grabbing not disabled
            releaseAutoCapture();
        QWidget *fw = QApplicationPrivate::active_window ? QApplicationPrivate::active_window->focusWidget()
            : q_func()->focusWidget();
        if (fw) {
            if (fw != q_func()->focusWidget()) {
                fw->setFocus(Qt::PopupFocusReason);
            } else {
                QFocusEvent e(QEvent::FocusIn, Qt::PopupFocusReason);
                q_func()->sendEvent(fw, &e);
            }
        }
    } else {
        // Popups are not focus-handled by the window system (the
        // first popup grabbed the keyboard), so we have to do that
        // manually: A popup was closed, so the previous popup gets
        // the focus.
        QWidget* aw = QApplicationPrivate::popupWidgets->last();
        if (QApplicationPrivate::popupWidgets->count() == 1) {
            Q_ASSERT(aw->testAttribute(Qt::WA_WState_Created));
            setAutoCapture(aw->internalWinId());
        }
        if (QWidget *fw = aw->focusWidget())
            fw->setFocus(Qt::PopupFocusReason);
    }
}

/*****************************************************************************
  Event translation; translates PM events to Qt events
 *****************************************************************************/

static int mouseButtonState()
{
    int state = 0;

    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000)
        state |= Qt::LeftButton;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000)
        state |= Qt::RightButton;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000)
        state |= Qt::MidButton;

    return state;
}

//
// Auto-capturing for mouse press and mouse release
//

static void setAutoCapture(HWND h)
{
    if (autoCaptureWnd)
        releaseAutoCapture();
    autoCaptureWnd = h;

    if (!mouseButtonState()) {
        // all buttons released, we don't actually capture the mouse
        // (see QWidget::translateMouseEvent())
        autoCaptureReleased = true;
    } else {
        autoCaptureReleased = false;
        WinSetCapture(HWND_DESKTOP, h);
    }
}

static void releaseAutoCapture()
{
    if (autoCaptureWnd) {
        if (!autoCaptureReleased) {
            WinSetCapture(HWND_DESKTOP, NULLHANDLE);
            autoCaptureReleased = true;
        }
        autoCaptureWnd = NULLHANDLE;
    }
}

//
// Mouse event translation
//

static const ushort mouseTbl[] = {
    WM_MOUSEMOVE,	    QEvent::MouseMove,		        0,
    WM_BUTTON1DOWN,	    QEvent::MouseButtonPress,	    Qt::LeftButton,
    WM_BUTTON1UP,	    QEvent::MouseButtonRelease,	    Qt::LeftButton,
    WM_BUTTON1DBLCLK,	QEvent::MouseButtonDblClick,	Qt::LeftButton,
    WM_BUTTON2DOWN,	    QEvent::MouseButtonPress,	    Qt::RightButton,
    WM_BUTTON2UP,	    QEvent::MouseButtonRelease,	    Qt::RightButton,
    WM_BUTTON2DBLCLK,	QEvent::MouseButtonDblClick,	Qt::RightButton,
    WM_BUTTON3DOWN,	    QEvent::MouseButtonPress,	    Qt::MidButton,
    WM_BUTTON3UP,	    QEvent::MouseButtonRelease,	    Qt::MidButton,
    WM_BUTTON3DBLCLK,	QEvent::MouseButtonDblClick,	Qt::MidButton,
    WM_CONTEXTMENU,     QEvent::ContextMenu,            0,
    0,			        0,				                0
};

static const ushort mouseTblNC[] = {
    WM_MOUSEMOVE,	    QEvent::NonClientAreaMouseMove,		        0,
    WM_BUTTON1DOWN,	    QEvent::NonClientAreaMouseButtonPress,	    Qt::LeftButton,
    WM_BUTTON1UP,	    QEvent::NonClientAreaMouseButtonRelease,	Qt::LeftButton,
    WM_BUTTON1DBLCLK,	QEvent::NonClientAreaMouseButtonDblClick,	Qt::LeftButton,
    WM_BUTTON2DOWN,	    QEvent::NonClientAreaMouseButtonPress,	    Qt::RightButton,
    WM_BUTTON2UP,	    QEvent::NonClientAreaMouseButtonRelease,	Qt::RightButton,
    WM_BUTTON2DBLCLK,	QEvent::NonClientAreaMouseButtonDblClick,	Qt::RightButton,
    WM_BUTTON3DOWN,	    QEvent::NonClientAreaMouseButtonPress,	    Qt::MidButton,
    WM_BUTTON3UP,	    QEvent::NonClientAreaMouseButtonRelease,	Qt::MidButton,
    WM_BUTTON3DBLCLK,	QEvent::NonClientAreaMouseButtonDblClick,	Qt::MidButton,
    0,			        0,				                0
};

static int translateButtonState(USHORT s, int type, int button)
{
    Q_UNUSED(button);

    int bst = mouseButtonState();

    if (type == QEvent::ContextMenu) {
        if (WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & 0x8000)
            bst |= Qt::ShiftModifier;
        if (WinGetKeyState(HWND_DESKTOP, VK_ALT) & 0x8000)
            bst |= Qt::AltModifier;
        if (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000)
            bst |= Qt::ControlModifier;
    } else {
        if (s & KC_SHIFT)
            bst |= Qt::ShiftModifier;
        if ((s & KC_ALT))
            bst |= Qt::AltModifier;
        if (s & KC_CTRL)
            bst |= Qt::ControlModifier;
    }
    if (qt_keymapper_private()->extraKeyState & Qt::AltModifier)
        bst |= Qt::AltModifier;
    if (qt_keymapper_private()->extraKeyState & Qt::MetaModifier)
        bst |= Qt::MetaModifier;

    return bst;
}

bool QETWidget::translateMouseEvent(const QMSG &qmsg)
{
    if (!isWindow() && testAttribute(Qt::WA_NativeWindow))
        Q_ASSERT(internalWinId() != NULLHANDLE);

    static QPoint pos;                          // window pos (y flipped)
    static POINTL gpos = { -1, -1 };            // global pos (y flipped)
    QEvent::Type type;				            // event parameters
    int	   button;
    int	   state;
    int	   i;

    // candidate for the double click event
    static HWND dblClickCandidateWin = 0;

#if !defined (QT_NO_SESSIONMANAGER)
    if (sm_blockUserInput) //block user interaction during session management
        return true;
#endif

    for (i = 0; mouseTbl[i] && (ULONG)mouseTbl[i] != qmsg.msg; i += 3)
        ;
    if (!mouseTbl[i])
        return true;

    type   = (QEvent::Type)mouseTbl[++i];   // event type
    button = mouseTbl[++i];			        // which button
    state  = translateButtonState(SHORT2FROMMP(qmsg.mp2), type, button); // button state

    // It seems, that PM remembers only the WM_BUTTONxDOWN message (instead of
    // the WM_BUTTONxDOWN + WM_BUTTONxUP pair) to detect whether the next button
    // press should be converted to WM_BUTTONxDBLCLK or not. As a result, the
    // window gets WM_BUTTONxDBLCLK even if it didn't receive the preceeding
    // WM_BUTTONxUP (this happens if we issue WinSetCapture() on the first
    // WM_BUTTONxDOWN), which is obviously wrong and makes problems for QWorkspace
    // and QTitleBar system menu handlers that don't expect a double click after
    // they opened a popup menu. dblClickCandidateWin is reset to 0 (see a ***
    // remmark below) when WinSetCapture is issued that directs messages
    // to a window other than one received the first WM_BUTTONxDOWN,
    // so we can fix it here.  Note that if there is more than one popup window,
    // WinSetCapture is issued only for the first of them, so this code doesn't
    // prevent MouseButtonDblClick from being delivered to a popup when another
    // popup gets closed on the first WM_BUTTONxDOWN (Qt/Win32 behaves in the
    // same way, so it's left for compatibility).
    if (type == QEvent::MouseButtonPress) {
        dblClickCandidateWin = qmsg.hwnd;
    } else if (type == QEvent::MouseButtonDblClick) {
        if (dblClickCandidateWin != qmsg.hwnd)
            type = QEvent::MouseButtonPress;
        dblClickCandidateWin = 0;
    }

    const QPoint widgetPos = mapFromGlobal(QPoint(qmsg.ptl.x, qmsg.ptl.y));

    QWidget *alienWidget = !internalWinId() ? this : QApplication::widgetAt(qmsg.ptl.x, qmsg.ptl.y);
    if (alienWidget && alienWidget->internalWinId())
        alienWidget = 0;

    if (type == QEvent::MouseMove) {
        if (!(state & Qt::MouseButtonMask))
            qt_button_down = 0;
#ifndef QT_NO_CURSOR
        QCursor *c = qt_grab_cursor();
    	if (!c)
    	    c = QApplication::overrideCursor();
        if (c) // application cursor defined
            WinSetPointer(HWND_DESKTOP, c->handle());
        else if (!qt_button_down) {
            QWidget *w = alienWidget ? alienWidget : this;
            while (!w->isWindow() && !w->isEnabled())
                w = w->parentWidget();
            WinSetPointer(HWND_DESKTOP, w->cursor().handle());
        }
#else
        // pass the msg to the default proc to let it change the pointer shape
        WinDefWindowProc(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
#endif

        HWND id = effectiveWinId();
        QWidget *mouseGrabber = QWidget::mouseGrabber();
        QWidget *activePopupWidget = qApp->activePopupWidget();
        if (mouseGrabber) {
            if (!activePopupWidget || (activePopupWidget == this && !rect().contains(widgetPos)))
                id = mouseGrabber->effectiveWinId();
        } else if (type == QEvent::NonClientAreaMouseMove) {
            id = 0;
        }

        if (curWin != id) { // new current window
            // @todo
            // add CS_HITTEST to our window classes and handle WM_HITTEST,
            // otherwise disabled windows will not get mouse events?
            if (id == 0) {
                QWidget *leave = qt_last_mouse_receiver;
                if (!leave)
                    leave = QWidget::find(curWin);
                QApplicationPrivate::dispatchEnterLeave(0, leave);
                qt_last_mouse_receiver = 0;
                curWin = 0;
            } else {
                QWidget *leave = 0;
                if (curWin && qt_last_mouse_receiver)
                    leave = qt_last_mouse_receiver;
                else
                    leave = QWidget::find(curWin);
                QWidget *enter = alienWidget ? alienWidget : this;
                if (mouseGrabber && activePopupWidget) {
                    if (leave != mouseGrabber)
                        enter = mouseGrabber;
                    else
                        enter = activePopupWidget == this ? this : mouseGrabber;
                }
                QApplicationPrivate::dispatchEnterLeave(enter, leave);
                qt_last_mouse_receiver = enter;
                curWin = id;
            }
        }

        // *** PM posts a dummy WM_MOUSEMOVE message (with the same, uncahnged
        // pointer coordinates) after every WinSetCapture that actually changes
        // the capture target. I.e., if the argument of WinSetCapture is
        // NULLHANDLE, a window under the mouse pointer gets this message,
        // otherwise the specified window gets it unless it is already under the
        // pointer. We use this info to check whether the window can be a double
        // click candidate (see above).
        if (qmsg.ptl.x == gpos.x && qmsg.ptl.y == gpos.y) {
            if (dblClickCandidateWin != qmsg.hwnd)
                dblClickCandidateWin = 0;
            return true;
        }

        gpos = qmsg.ptl;
        pos = widgetPos;

        Q_ASSERT(testAttribute(Qt::WA_WState_Created));
    } else {
        if (type == QEvent::MouseButtonPress && !isActiveWindow())
            activateWindow();

        gpos = qmsg.ptl;
        pos = widgetPos;

        // mouse button pressed
        if (!qt_button_down && (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)) {
            QWidget *tlw = window();
            if (QWidget *child = tlw->childAt(mapTo(tlw, pos)))
                qt_button_down = child;
            else
                qt_button_down = this;
        }
    }

    // detect special button states
    enum { Other, SinglePressed, AllReleased } btnState = Other;
    int bs = state & Qt::MouseButtonMask;
    if ((type == QEvent::MouseButtonPress ||
         type == QEvent::MouseButtonDblClick) && bs == button) {
        btnState = SinglePressed;
    } else if (type == QEvent::MouseButtonRelease && bs == 0) {
        btnState = AllReleased;
    }

    bool res = false;

    if (qApp->d_func()->inPopupMode()) { // in popup mode
        if (!autoCaptureReleased && btnState == AllReleased) {
            // in order to give non-Qt windows the opportunity to see mouse
            // messages while our popups are active we need to release the
            // mouse capture which is absolute in OS/2. We do it directly
            // (not through releaseAutoCapture()) in order to keep
            // autoCaptureWnd nonzero so that mouse move events (actually sent
            // to one of Qt widgets) are forwarded to the active popup.
            autoCaptureReleased = true;
            WinSetCapture(HWND_DESKTOP, 0);
        } else if (autoCaptureReleased && btnState == SinglePressed) {
            // set the mouse capture back if a button is pressed.
            if ( autoCaptureWnd ) {
                autoCaptureReleased = false;
                WinSetCapture(HWND_DESKTOP, autoCaptureWnd);
            }
        }

        replayPopupMouseEvent = false;
        QWidget* activePopupWidget = qApp->activePopupWidget();
        QWidget *target = activePopupWidget;
        const QPoint globalPos(gpos.x, gpos.y);

        if (target != this) {
            if ((windowType() == Qt::Popup) && rect().contains(pos) && 0)
                target = this;
            else                                // send to last popup
                pos = target->mapFromGlobal(globalPos);
        }
        QWidget *popupChild = target->childAt(pos);
        bool releaseAfter = false;
        switch (type) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                popupButtonFocus = popupChild;
                break;
            case QEvent::MouseButtonRelease:
                releaseAfter = true;
                break;
            default:
                break;                                // nothing for mouse move
        }

        if (target->isEnabled()) {
            if (popupButtonFocus) {
                target = popupButtonFocus;
            } else if (popupChild) {
                // forward mouse events to the popup child. mouse move events
                // are only forwarded to popup children that enable mouse tracking.
                if (type != QEvent::MouseMove || popupChild->hasMouseTracking())
                    target = popupChild;
            }

            pos = target->mapFromGlobal(globalPos);
#ifndef QT_NO_CONTEXTMENU
            if (type == QEvent::ContextMenu) {
                QContextMenuEvent e(QContextMenuEvent::Mouse, pos, globalPos,
                                    Qt::KeyboardModifiers(state & Qt::KeyboardModifierMask));
                res = QApplication::sendSpontaneousEvent(target, &e);
                res = res && e.isAccepted();
            }
            else
#endif
            {
                QMouseEvent e(type, pos, globalPos,
                            Qt::MouseButton(button),
                            Qt::MouseButtons(state & Qt::MouseButtonMask),
                            Qt::KeyboardModifiers(state & Qt::KeyboardModifierMask));
                res = QApplicationPrivate::sendMouseEvent(target, &e, alienWidget, this, &qt_button_down,
                                                          qt_last_mouse_receiver);
                res = res && e.isAccepted();
            }
        } else {
            // close disabled popups when a mouse button is pressed or released
            switch (type) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::MouseButtonRelease:
                target->close();
                break;
            default:
                break;
            }
        }

        if (releaseAfter) {
            popupButtonFocus = 0;
            qt_button_down = 0;
        }

        if (type == QEvent::MouseButtonPress
            && qApp->activePopupWidget() != activePopupWidget
            && replayPopupMouseEvent) {
            // the popup dissappeared. Replay the event
            QWidget* w = QApplication::widgetAt(gpos.x, gpos.y);
            if (w && !QApplicationPrivate::isBlockedByModal(w)) {
                Q_ASSERT(w->testAttribute(Qt::WA_WState_Created));
                HWND hwndTarget = w->effectiveWinId();
                if (QWidget::mouseGrabber() == 0)
                    setAutoCapture(hwndTarget);
                if (!w->isActiveWindow())
                    w->activateWindow();
                POINTL ptl = gpos;
                // flip y coordinate
                ptl.y = qt_display_height() - (ptl.y + 1);
                WinMapWindowPoints(HWND_DESKTOP, hwndTarget, &ptl, 1);
                WinPostMsg(hwndTarget, qmsg.msg,
                           MPFROM2SHORT(ptl.x, ptl.y), qmsg.mp2);
            }
        }
    } else { // not popup mode
    	if (btnState == SinglePressed && QWidget::mouseGrabber() == 0) {
            Q_ASSERT(testAttribute(Qt::WA_WState_Created));
            setAutoCapture(internalWinId());
        } else if (btnState == AllReleased && QWidget::mouseGrabber() == 0) {
            releaseAutoCapture();
        }

        const QPoint globalPos(gpos.x,gpos.y);
        QWidget *widget = QApplicationPrivate::pickMouseReceiver(this, globalPos, pos, type,
                                                                 Qt::MouseButtons(bs),
                                                                 qt_button_down, alienWidget);
        if (!widget)
            return false; // don't send event

#ifndef QT_NO_CONTEXTMENU
        if (type == QEvent::ContextMenu) {
            QContextMenuEvent e(QContextMenuEvent::Mouse, pos, globalPos,
                                Qt::KeyboardModifiers(state & Qt::KeyboardModifierMask));
            res = QApplication::sendSpontaneousEvent(widget, &e);
            res = res && e.isAccepted();
        } else
#endif
        {
            QMouseEvent e(type, pos, globalPos, Qt::MouseButton(button),
                          Qt::MouseButtons(state & Qt::MouseButtonMask),
                          Qt::KeyboardModifiers(state & Qt::KeyboardModifierMask));

            res = QApplicationPrivate::sendMouseEvent(widget, &e, alienWidget, this, &qt_button_down,
                                                      qt_last_mouse_receiver);
            res = res && e.isAccepted();
        }
    }

    return res;
}

void QETWidget::translateNonClientMouseEvent(const QMSG &qmsg)
{
    // this is a greatly simplified version of translateMouseEvent() that
    // only sends informational non-client area mouse messages to the top-level
    // widget

    Q_ASSERT(isWindow());
    Q_ASSERT(internalWinId() != NULLHANDLE);

#if !defined (QT_NO_SESSIONMANAGER)
    if (sm_blockUserInput) //block user interaction during session management
        return;
#endif

    if (qApp->d_func()->inPopupMode()) {
        // don't report non-client area events in popup mode
        // (for compatibility with Windows)
        return;
    }

    int i;
    for (i = 0; mouseTblNC[i] && (ULONG)mouseTblNC[i] != qmsg.msg; i += 3)
        ;
    if (!mouseTblNC[i])
        return;

    QEvent::Type type   = (QEvent::Type)mouseTblNC[++i];    // event type
    int button          = mouseTblNC[++i];                  // which button
    int state = translateButtonState(SHORT2FROMMP(qmsg.mp2), type, button); // button state

    const QPoint globalPos(QPoint(qmsg.ptl.x, qmsg.ptl.y));
    const QPoint widgetPos = mapFromGlobal(globalPos);

    QMouseEvent e(type, widgetPos, globalPos, Qt::MouseButton(button),
                  Qt::MouseButtons(state & Qt::MouseButtonMask),
                  Qt::KeyboardModifiers(state & Qt::KeyboardModifierMask));

    QApplication::sendSpontaneousEvent(this, &e);
}

#ifndef QT_NO_WHEELEVENT
bool QETWidget::translateWheelEvent(const QMSG &qmsg)
{
    enum { WHEEL_DELTA = 120 };

#ifndef QT_NO_SESSIONMANAGER
    if (sm_blockUserInput) // block user interaction during session management
        return true;
#endif

    // consume duplicate wheel events sent by the AMouse driver to emulate
    // multiline scrolls. we need this since currently Qt (QScrollBar, for
    // instance) maintains the number of lines to scroll per wheel rotation
    // (including the special handling of CTRL and SHIFT modifiers) on its own
    // and doesn't have a setting to tell it to be aware of system settings
    // for the mouse wheel. if we had processed events as they are, we would
    // get a confusing behavior (too many lines scrolled etc.).
    {
        int devh = qt_display_height();
        QMSG wheelMsg;
        while (WinPeekMsg(0, &wheelMsg, qmsg.hwnd, qmsg.msg, qmsg.msg, PM_NOREMOVE)) {
            // PM bug: ptl contains SHORT coordinates although fields are LONG
            wheelMsg.ptl.x = (short) wheelMsg.ptl.x;
            wheelMsg.ptl.y = (short) wheelMsg.ptl.y;
            // flip y coordinate
            wheelMsg.ptl.y = devh - (wheelMsg.ptl.y + 1);
            if (wheelMsg.mp1 != qmsg.mp1 ||
                wheelMsg.mp2 != qmsg.mp2 ||
                wheelMsg.ptl.x != qmsg.ptl.x ||
                wheelMsg.ptl.y != qmsg.ptl.y)
                break;
            WinPeekMsg(0, &wheelMsg, qmsg.hwnd, qmsg.msg, qmsg.msg, PM_REMOVE);
        }
    }

    int delta;
    USHORT cmd = SHORT2FROMMP(qmsg.mp2);
    switch (cmd) {
        case SB_LINEUP:
        case SB_PAGEUP:
            delta = WHEEL_DELTA;
            break;
        case SB_LINEDOWN:
        case SB_PAGEDOWN:
            delta = -WHEEL_DELTA;
            break;
        default:
            return false;
    }

    int state = 0;
    if (WinGetKeyState(HWND_DESKTOP, VK_SHIFT ) & 0x8000)
        state |= Qt::ShiftModifier;
    if ((WinGetKeyState(HWND_DESKTOP, VK_ALT) & 0x8000) ||
        (qt_keymapper_private()->extraKeyState & Qt::AltModifier))
        state |= Qt::AltModifier;
    if (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000)
        state |= Qt::ControlModifier;
    if (qt_keymapper_private()->extraKeyState & Qt::MetaModifier)
        state |= Qt::MetaModifier;

    Qt::Orientation orient;
    // Alt inverts scroll orientation (Qt/Win32 behavior)
    if (state & Qt::AltModifier)
        orient = qmsg.msg == WM_VSCROLL ? Qt::Horizontal : Qt::Vertical;
    else
        orient = qmsg.msg == WM_VSCROLL ? Qt::Vertical : Qt::Horizontal;

    QPoint globalPos(qmsg.ptl.x, qmsg.ptl.y);

    // if there is a widget under the mouse and it is not shadowed
    // by modality, we send the event to it first
    MRESULT rc = FALSE;
    QWidget* w = QApplication::widgetAt(globalPos);
    if (!w || !qt_try_modal(w, (QMSG*)&qmsg, rc)) {
        //synaptics touchpad shows its own widget at this position
        //so widgetAt() will fail with that HWND, try child of this widget
        w = this->childAt(this->mapFromGlobal(globalPos));
        if (!w)
            w = this;
    }

    // send the event to the widget or its ancestors
    {
        QWidget* popup = qApp->activePopupWidget();
        if (popup && w->window() != popup)
            popup->close();
#ifndef QT_NO_WHEELEVENT
        QWheelEvent e(w->mapFromGlobal(globalPos), globalPos, delta,
                      Qt::MouseButtons(state & Qt::MouseButtonMask),
                      Qt::KeyboardModifier(state & Qt::KeyboardModifierMask), orient);

        if (QApplication::sendSpontaneousEvent(w, &e))
#else
        Q_UNUSED(orient);
#endif //QT_NO_WHEELEVENT
            return true;
    }

    // send the event to the widget that has the focus or its ancestors, if different
    if (w != qApp->focusWidget() && (w = qApp->focusWidget())) {
        QWidget* popup = qApp->activePopupWidget();
        if (popup && w->window() != popup)
            popup->close();
#ifndef QT_NO_WHEELEVENT
        QWheelEvent e(w->mapFromGlobal(globalPos), globalPos, delta,
                      Qt::MouseButtons(state & Qt::MouseButtonMask),
                      Qt::KeyboardModifier(state & Qt::KeyboardModifierMask), orient);
        if (QApplication::sendSpontaneousEvent(w, &e))
#endif //QT_NO_WHEELEVENT
            return true;
    }

    return false;
}
#endif

/*!
    \internal
    In DnD, the mouse release event never appears, so the
    mouse button state machine must be manually reset.
*/
void qt_pmMouseButtonUp()
{
    // release any stored mouse capture
    qt_button_down = 0;
    autoCaptureReleased = true;
    releaseAutoCapture();
}

//
// Paint event translation
//
bool QETWidget::translatePaintEvent(const QMSG &qmsg)
{
    if (!isWindow() && testAttribute(Qt::WA_NativeWindow))
        Q_ASSERT(internalWinId());

    HPS displayPS = qt_display_ps();

#ifdef QT_PM_NATIVEWIDGETMASK
    // Since we don't use WS_CLIPSIBLINGS and WS_CLIPCHILDREN bits (see
    // qwidget_pm.cpp), we have to validate areas that intersect with our
    // children and siblings, taking their clip regions into account.
    d_func()->validateObstacles();
#endif

    Q_ASSERT(testAttribute(Qt::WA_WState_Created));

    HRGN hrgn = GpiCreateRegion(displayPS, 0, NULL);
    LONG rc = WinQueryUpdateRegion(internalWinId(), hrgn);
    if (rc == RGN_ERROR) { // The update bounding rect is invalid
        GpiDestroyRegion(displayPS, hrgn);
        setAttribute(Qt::WA_PendingUpdate, false);
        return false;
    }

    setAttribute(Qt::WA_PendingUpdate, false);

    const QRegion dirtyInBackingStore(qt_dirtyRegion(this));
    // Make sure the invalidated region contains the region we're about to repaint.
    // BeginPaint will set the clip to the invalidated region and it is impossible
    // to enlarge it afterwards (only shrink it).
    if (!dirtyInBackingStore.isEmpty())
      WinInvalidateRegion(internalWinId(), dirtyInBackingStore.handle(height()), FALSE);

    RECTL rcl;
    d_func()->hd = WinBeginPaint(internalWinId(), 0, &rcl);

#if defined(QT_DEBUGMSGFLOW)
    qDebug() << "PAINT BEGIN:" << rcl << "hps" << qDebugFmtHex(d_func()->hd);
#endif

    // it's possible that the update rectangle is empty
    if (rcl.xRight <= rcl.xLeft || rcl.yTop <= rcl.yBottom) {
        WinEndPaint(d_func()->hd);
        d_func()->hd = NULLHANDLE;
        GpiDestroyRegion(displayPS, hrgn);
        setAttribute(Qt::WA_PendingUpdate, false);
        return true;
    }

    // flip y coordinate
    // note: right top point is exlusive in rcl
    QRect updRect(QPoint(rcl.xLeft, height() - rcl.yTop),
                  QPoint(rcl.xRight - 1, height() - (rcl.yBottom + 1)));

    // Mapping region from system to qt (32 bit) coordinate system.
    updRect.translate(data->wrect.topLeft());
#if defined(QT_DEBUGMSGFLOW)
    qDebug() << "PAINT updRect:" << updRect;
#endif

    // @todo use hrgn here converted to QRegion?
    d_func()->syncBackingStore(updRect);

    WinEndPaint(d_func()->hd);
    d_func()->hd = NULLHANDLE;

#if defined(QT_DEBUGMSGFLOW)
    qDebug() << "PAINT END";
#endif

    return true;
}

//
// Window move and resize (configure) events
//

bool QETWidget::translateConfigEvent(const QMSG &qmsg)
{
    if (!testAttribute(Qt::WA_WState_Created)) // in QWidget::create()
        return true;
    if (testAttribute(Qt::WA_WState_ConfigPending))
        return true;
    if (testAttribute(Qt::WA_DontShowOnScreen))
        return true;

    // @todo there are other isWindow() checks below (same in Windows code).
    // Either they or this return statement are leftovers. The assertion may
    // tell the truth.
    Q_ASSERT(isWindow());
    if (!isWindow())
        return true;

    // When the window is minimized, PM moves it to -32000,-32000 and resizes
    // to 48x50. We don't want these useless actions to be seen by Qt.
    if (isMinimized())
        return true;

    setAttribute(Qt::WA_WState_ConfigPending); // set config flag

    HWND fId = NULLHANDLE;
    ULONG fStyle = 0;
    if (isWindow()) {
        fId = d_func()->frameWinId();
        fStyle = WinQueryWindowULong(fId, QWL_STYLE);
    }

    // Note: due to the vertical coordinate space flip in PM, WM_SIZE events may
    // also mean moving the widget in Qt coordinates

    if (qmsg.msg == WM_MOVE || qmsg.msg == WM_SIZE) { // move event
        QPoint oldPos = data->crect.topLeft();
        SWP swp;
        if (isWindow()) {
            WinQueryWindowPos(fId, &swp);
            // flip y coordinate
            swp.y = qt_display_height() - (swp.y + swp.cy);
            QTLWExtra *top = d_func()->topData();
            swp.x += top->frameStrut.left();
            swp.y += top->frameStrut.top();
        } else {
            WinQueryWindowPos(internalWinId(), &swp);
            // flip y coordinate
            swp.y = parentWidget()->height() - (swp.y + swp.cy);
        }
        QPoint newCPos(swp.x, swp.y);
        if (newCPos != oldPos) {
            data->crect.moveTopLeft(newCPos);
            if (isVisible()) {
                QMoveEvent e(newCPos, oldPos); // cpos (client position)
                QApplication::sendSpontaneousEvent(this, &e);
            } else {
                QMoveEvent *e = new QMoveEvent(newCPos, oldPos);
                QApplication::postEvent(this, e);
            }
        }
    }
    if (qmsg.msg == WM_SIZE) { // resize event
        QSize oldSize = data->crect.size();
        QSize newSize = QSize(SHORT1FROMMP(qmsg.mp2), SHORT2FROMMP(qmsg.mp2));
        data->crect.setSize(newSize);
        if (isWindow()) {                        // update title/icon text
            d_func()->createTLExtra();
            QString title;
            if ((fStyle & WS_MINIMIZED))
                title = windowIconText();
            if (title.isEmpty())
                title = windowTitle();
            if (!title.isEmpty())
                d_func()->setWindowTitle_helper(title);
        }
        if (oldSize != newSize) {
            // Spontaneous (external to Qt) WM_SIZE messages should occur only
            // on top-level widgets. If we get them for a non top-level widget,
            // the result will most likely be incorrect because widget masks will
            // not be properly processed (i.e. in the way it is done in
            // QWidget::setGeometry_sys() when the geometry is changed from
            // within Qt). So far, I see no need to support this (who will ever
            // need to move a non top-level window of a foreign process?).
            Q_ASSERT(isWindow());
            if (isVisible()) {
                QTLWExtra *tlwExtra = d_func()->maybeTopData();
                static bool slowResize = qgetenv("QT_SLOW_TOPLEVEL_RESIZE").toInt();
                const bool hasStaticContents = tlwExtra && tlwExtra->backingStore
                                               && tlwExtra->backingStore->hasStaticContents();
                // If we have a backing store with static contents, we have to disable the top-level
                // resize optimization in order to get invalidated regions for resized widgets.
                // The optimization discards all invalidateBuffer() calls since we're going to
                // repaint everything anyways, but that's not the case with static contents.
                if (!slowResize && tlwExtra && !hasStaticContents)
                    tlwExtra->inTopLevelResize = true;
                QResizeEvent e(newSize, oldSize);
                QApplication::sendSpontaneousEvent(this, &e);
                if (d_func()->paintOnScreen()) {
                    QRegion updateRegion(rect());
                    if (testAttribute(Qt::WA_StaticContents))
                        updateRegion -= QRect(0, 0, oldSize.width(), oldSize.height());
                    // syncBackingStore() should have already flushed the widget
                    // contents to the screen, so no need to redraw the exposed
                    // areas in WM_PAINT once more
                    d_func()->syncBackingStore(updateRegion);
                    WinValidateRegion(internalWinId(),
                                      updateRegion.handle(newSize.height()), FALSE);
                } else {
                    d_func()->syncBackingStore();
                    // see above
                    RECTL rcl = { 0, 0, newSize.width(), newSize.height() };
                    WinValidateRect(internalWinId(), &rcl, FALSE);
                }
                if (!slowResize && tlwExtra)
                    tlwExtra->inTopLevelResize = false;
            } else {
                QResizeEvent *e = new QResizeEvent(newSize, oldSize);
                QApplication::postEvent(this, e);
            }
        }
    }
    setAttribute(Qt::WA_WState_ConfigPending, false);                // clear config flag
    return true;
}

//
// Close window event translation.
//
// This class is a friend of QApplication because it needs to emit the
// lastWindowClosed() signal when the last top level widget is closed.
//

bool QETWidget::translateCloseEvent(const QMSG &)
{
    return d_func()->close_helper(QWidgetPrivate::CloseWithSpontaneousEvent);
}

void QApplicationPrivate::initializeMultitouch_sys()
{
}

void QApplicationPrivate::cleanupMultitouch_sys()
{
}

/*****************************************************************************
  PM session management
 *****************************************************************************/

#if !defined(QT_NO_SESSIONMANAGER)

bool QApplicationPrivate::canQuit()
{
#if defined (DEBUG_SESSIONMANAGER)
    qDebug("QApplicationPrivate::canQuit: sm_smActive %d,"
           "qt_about_to_destroy_wnd %d, sm_gracefulShutdown %d, sm_cancel %d",
           sm_smActive, qt_about_to_destroy_wnd,
           sm_gracefulShutdown, sm_cancel);
#endif

    bool quit = false;

    // We can get multiple WM_QUIT messages while the "session termination
    // procedure" (i.e. the QApplication::commitData() call) is still in
    // progress. Ignore them.
    if (!sm_smActive) {
        if (sm_gracefulShutdown) {
            // this is WM_QUIT after WM_SAVEAPPLICATION (either posted by the OS
            // or by ourselves), confirm the quit depending on what the user wants
            sm_quitSkipped = false;
            quit = !sm_cancel;
            if (sm_cancel) {
                // the shutdown has been canceled, reset the flag to let the
                // graceful shutdown happen again later
                sm_gracefulShutdown = false;
            }
        } else {
            // sm_gracefulShutdown is false, so allowsInteraction() and friends
            // will return FALSE during commitData() (assuming that WM_QUIT w/o
            // WM_SAVEAPPLICATION is an emergency termination)
            sm_smActive = true;
            sm_blockUserInput = true; // prevent user-interaction outside interaction windows
            sm_cancel = false;
            if (qt_session_manager_self)
                qApp->commitData(*qt_session_manager_self);
            sm_smActive = false;
            quit = true; // ignore sm_cancel
        }
    } else {
        // if this is a WM_QUIT received during WM_SAVEAPPLICATION handling,
        // remember we've skipped (refused) it
        if (sm_gracefulShutdown)
            sm_quitSkipped = true;
    }

#if defined (DEBUG_SESSIONMANAGER)
    qDebug("QApplicationPrivate::canQuit: returns %d", quit);
#endif

    return quit;
}

bool QSessionManager::allowsInteraction()
{
    // Allow interation only when the system is being normally shutdown
    // and informs us using WM_SAVEAPPLICATION. When we receive WM_QUIT directly
    // (so sm_gracefulShutdown is false), interaction is disallowed.
    if (sm_smActive && sm_gracefulShutdown) {
        sm_blockUserInput = false;
        return true;
    }

    return false;
}

bool QSessionManager::allowsErrorInteraction()
{
    // Allow interation only when the system is being normally shutdown
    // and informs us using WM_SAVEAPPLICATION. When we receive WM_QUIT directly
    // (so sm_gracefulShutdown is false), interaction is disallowed.
    if (sm_smActive && sm_gracefulShutdown) {
        sm_blockUserInput = false;
        return true;
    }

    return false;
}

void QSessionManager::release()
{
    if (sm_smActive && sm_gracefulShutdown)
        sm_blockUserInput = true;
}

void QSessionManager::cancel()
{
    if (sm_smActive && sm_gracefulShutdown)
        sm_cancel = true;
}

#endif // QT_NO_SESSIONMANAGER

/*****************************************************************************
  PM struct/message debug helpers
 *****************************************************************************/

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QDebugHWND &d)
{
    QETWidget *w = (QETWidget *)qt_widget_from_hwnd(d.hwnd);
    debug << qDebugFmtHex(d.hwnd) << w;
    if (w && w->internalWinId() != w->frameWinId()) {
        if (w->internalWinId() == d.hwnd) {
            debug.nospace() << "(frame " << qDebugFmtHex(w->frameWinId()) << ")";
            debug.space();
        } else if (w->frameWinId() == d.hwnd) {
            debug.nospace() << "(client " << qDebugFmtHex(w->internalWinId()) << ")";
            debug.space();
        }
    }
    return debug;
}

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QDebugHRGN &d)
{
    RGNRECT ctl;
    ctl.ircStart = 1;
    ctl.crc = 0;
    ctl.crcReturned = 0;
    ctl.ulDirection = RECTDIR_LFRT_BOTTOP;
    GpiQueryRegionRects(qt_display_ps(), d.hrgn, NULL, &ctl, NULL);
    ctl.crc = ctl.crcReturned;
    int rclcnt = ctl.crcReturned;
    PRECTL rcls = new RECTL[rclcnt];
    GpiQueryRegionRects(qt_display_ps(), d.hrgn, NULL, &ctl, rcls);
    PRECTL rcl = rcls;
    debug.nospace() << "HRGN{";
    for (int i = 0; i < rclcnt; i++, rcl++)
        debug.nospace() << " " << *rcl;
    delete [] rcls;
    debug.nospace() << "}";
    return debug.space();
}

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const RECTL &rcl)
{
    debug.nospace() << "RECTL(" << rcl.xLeft << "," << rcl.yBottom
                    << " " << rcl.xRight << "," << rcl.yTop << ")";
    return debug.space();
}

#define myDefFlagEx(var,fl,varstr,flstr) if (var & fl) { \
    if (!varstr.isEmpty()) varstr += "|"; varstr += flstr; \
} else do {} while(0)

#define myDefFlag(var,fl,varstr) myDefFlagEx(var,fl,varstr,#fl)
#define myDefFlagCut(var,fl,varstr,pos) myDefFlagEx(var,fl,varstr,#fl + pos)

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const SWP &swp)
{
    QByteArray fl;
    myDefFlagEx(swp.fl, SWP_SIZE, fl, "SIZE");
    myDefFlagEx(swp.fl, SWP_MOVE, fl, "MOVE");
    myDefFlagEx(swp.fl, SWP_ZORDER, fl, "ZORD");
    myDefFlagEx(swp.fl, SWP_SHOW, fl, "SHOW");
    myDefFlagEx(swp.fl, SWP_HIDE, fl, "HIDE");
    myDefFlagEx(swp.fl, SWP_NOREDRAW, fl, "NORDR");
    myDefFlagEx(swp.fl, SWP_NOADJUST, fl, "NOADJ");
    myDefFlagEx(swp.fl, SWP_ACTIVATE, fl, "ACT");
    myDefFlagEx(swp.fl, SWP_DEACTIVATE, fl, "DEACT");
    myDefFlagEx(swp.fl, SWP_EXTSTATECHANGE, fl, "EXTST");
    myDefFlagEx(swp.fl, SWP_MINIMIZE, fl, "MIN");
    myDefFlagEx(swp.fl, SWP_MAXIMIZE, fl, "MAX");
    myDefFlagEx(swp.fl, SWP_RESTORE, fl, "REST");
    myDefFlagEx(swp.fl, SWP_FOCUSACTIVATE, fl, "FCSACT");
    myDefFlagEx(swp.fl, SWP_FOCUSDEACTIVATE, fl, "FCSDEACT");
    myDefFlagEx(swp.fl, SWP_NOAUTOCLOSE, fl, "NOACLOSE");

    debug.nospace() << "SWP(" << swp.x << "," << swp.y
                    << " " << swp.cx << "x" << swp.cy << " "
                    << qDebugFmtHex(swp.hwndInsertBehind) << " "
                    << fl.constData() << ")";
    return debug.space();
}

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QMSG &qmsg)
{
    #define myCaseBegin(a) case a: { \
        debug << #a": hwnd" << qDebugHWND(qmsg.hwnd);
    #define myCaseEnd() }

    bool isMouse = false;

    switch (qmsg.msg) {

#if 1
    myCaseBegin(WM_MOUSEMOVE)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON1DOWN)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON1UP)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON1DBLCLK)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON2DOWN)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON2UP)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON2DBLCLK)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON3DOWN)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON3UP)
        isMouse = true; break;
    myCaseEnd()
    myCaseBegin(WM_BUTTON3DBLCLK)
        isMouse = true; break;
    myCaseEnd()
#endif

    myCaseBegin(WM_CHAR)
        USHORT fl = SHORT1FROMMP(qmsg.mp1);
        UCHAR repeat = CHAR3FROMMP(qmsg.mp1);
        UCHAR scan = CHAR4FROMMP(qmsg.mp1);
        USHORT ch = SHORT1FROMMP(qmsg.mp2);
        USHORT vk = SHORT2FROMMP(qmsg.mp2);
        QByteArray flstr;
        myDefFlagEx(fl, KC_CHAR, flstr, "CHAR");
        myDefFlagEx(fl, KC_VIRTUALKEY, flstr, "VIRT");
        myDefFlagEx(fl, KC_SCANCODE, flstr, "SCAN");
        myDefFlagEx(fl, KC_SHIFT, flstr, "SHIFT");
        myDefFlagEx(fl, KC_CTRL, flstr, "CTRL");
        myDefFlagEx(fl, KC_ALT, flstr, "ALT");
        myDefFlagEx(fl, KC_KEYUP, flstr, "UP");
        myDefFlagEx(fl, KC_PREVDOWN, flstr, "PREVDWN");
        myDefFlagEx(fl, KC_LONEKEY, flstr, "LONE");
        myDefFlagEx(fl, KC_DEADKEY, flstr, "DEAD");
        myDefFlagEx(fl, KC_COMPOSITE, flstr, "COMP");
        myDefFlagEx(fl, KC_INVALIDCOMP, flstr, "INVCMP");
        myDefFlagEx(fl, KC_TOGGLE, flstr, "TGGL");
        myDefFlagEx(fl, KC_INVALIDCHAR, flstr, "INVCHR");

        debug << "rep" << qDebugFmt("%02d", repeat)
              << "scan" << qDebugFmtHex(scan) << "ch" << qDebugFmtHex(ch)
              << ((ch > 32 && ch < 254) ? QString::fromLocal8Bit((char *)&ch, 1) :
                                          QString(QLatin1String(" ")))
              << "vk" << qDebugFmtHex(vk);
        debug.nospace() << "KC(" << qDebugFmtHex(fl) << ","
                        << flstr.constData() << ")";
        debug.space();
        break;
    myCaseEnd()

    myCaseBegin(WM_KBDLAYERCHANGED)
        debug << "mp1" << qmsg.mp1 << "mp2" << qmsg.mp2;
        break;
    myCaseEnd()

    myCaseBegin(WM_PAINT)
        debug << "mp1" << qmsg.mp1 << "mp2" << qmsg.mp2;
        break;
    myCaseEnd()

    myCaseBegin(WM_SIZE)
        SWP swp;
        WinQueryWindowPos(qmsg.hwnd, &swp);
        debug << "old " << SHORT1FROMMP(qmsg.mp1) << SHORT2FROMMP(qmsg.mp1)
              << "new " << SHORT1FROMMP(qmsg.mp2) << SHORT2FROMMP(qmsg.mp2)
              << swp;
        HWND p = WinQueryWindow(qmsg.hwnd, QW_PARENT);
        if (p != NULLHANDLE && p != WinQueryDesktopWindow(0, 0)) {
            WinQueryWindowPos(p, &swp);
            debug << "parent" << swp;
        }
        break;
    myCaseEnd()

    myCaseBegin(WM_MOVE)
        SWP swp;
        WinQueryWindowPos(qmsg.hwnd, &swp);
        debug << swp;
        HWND p = WinQueryWindow(qmsg.hwnd, QW_PARENT);
        if (p != NULLHANDLE && p != WinQueryDesktopWindow(0, 0)) {
            WinQueryWindowPos(p, &swp);
            debug << "parent" << swp;
        }
        break;
    myCaseEnd()

    myCaseBegin(WM_ADJUSTWINDOWPOS)
        debug << *((PSWP) qmsg.mp1);
        debug.space();
        break;
    myCaseEnd()

    myCaseBegin(WM_WINDOWPOSCHANGED)
        debug << *((PSWP) qmsg.mp1);
        ULONG awp = LONGFROMMP(qmsg.mp2);
        QByteArray awpstr;
        myDefFlagEx(awp, AWP_MINIMIZED, awpstr, "MIN");
        myDefFlagEx(awp, AWP_MAXIMIZED, awpstr, "MAX");
        myDefFlagEx(awp, AWP_RESTORED, awpstr, "REST");
        myDefFlagEx(awp, AWP_ACTIVATE, awpstr, "ACT");
        myDefFlagEx(awp, AWP_DEACTIVATE, awpstr, "DEACT");
        debug.nospace() << "AWP(" << awpstr.constData() << ")";
        debug.space();
        break;
    myCaseEnd()

    myCaseBegin(WM_MINMAXFRAME)
        debug << *((PSWP) qmsg.mp1);
        break;
    myCaseEnd()

    myCaseBegin(WM_ACTIVATE)
        bool active = SHORT1FROMMP(qmsg.mp1);
        HWND hwnd = (HWND)qmsg.mp2;
        debug.nospace() << "Active(" << (active ? "TRUE)" : "FALSE)");
        debug.space() << "hwndActive" << qDebugHWND(hwnd);
        break;
    myCaseEnd()

    myCaseBegin(WM_SETFOCUS)
        HWND hwnd = (HWND)qmsg.mp1;
        bool focus = SHORT1FROMMP(qmsg.mp2);
        debug.nospace() << "Focus(" << (focus ? "TRUE) " : "FALSE)");
        debug.space() << "hwndFocus" << qDebugHWND(hwnd);
        break;
    myCaseEnd()

    myCaseBegin(WM_FOCUSCHANGE)
        HWND hwnd = (HWND)qmsg.mp1;
        bool focus = SHORT1FROMMP(qmsg.mp2);
        bool fl = SHORT2FROMMP(qmsg.mp2);
        QByteArray flstr;
        myDefFlagEx(fl, FC_NOSETFOCUS, flstr, "NOSETFCS");
        myDefFlagEx(fl, FC_NOLOSEFOCUS, flstr, "NOLOSEFCS");
        myDefFlagEx(fl, FC_NOSETACTIVE, flstr, "NOSETACT");
        myDefFlagEx(fl, FC_NOLOSEACTIVE, flstr, "NOLOSEACT");
        myDefFlagEx(fl, FC_NOSETSELECTION, flstr, "NOSETSEL");
        myDefFlagEx(fl, FC_NOLOSESELECTION, flstr, "NOSETSEL");
        debug.nospace() << "Focus(" << (focus ? "TRUE) " : "FALSE)");
        debug.space() << "hwndFocus" << qDebugHWND(hwnd);
        debug.nospace() << "FC(" << flstr.constData() << ")";
        debug.space();
        break;
    myCaseEnd()

    myCaseBegin(WM_VRNDISABLED)
        break;
    myCaseEnd()
    myCaseBegin(WM_VRNENABLED)
        break;
    myCaseEnd()

    myCaseBegin(WM_SHOW)
        break;
    myCaseEnd()

    myCaseBegin(WM_HITTEST)
        PPOINTS pt = (PPOINTS)&qmsg.mp1;
        debug.nospace() << "Point(" << pt->x << "," << pt->y << ")";
        debug.space();
        break;
    myCaseEnd()

    default:
        debug.nospace() << "WM_" << qDebugFmtHex(qmsg.msg) << ":";
        debug.space() << qDebugHWND(qmsg.hwnd);
        debug << "mp1" << qmsg.mp1 << "mp2" << qmsg.mp2;
        break;
    }

    if (isMouse) {
        SHORT x = SHORT1FROMMP(qmsg.mp1);
        SHORT y = SHORT2FROMMP(qmsg.mp1);
        USHORT hit = SHORT1FROMMP(qmsg.mp2);
        bool fl = SHORT2FROMMP(qmsg.mp2);
        QByteArray hitstr;
        myDefFlagEx(hit, HT_NORMAL, hitstr, "NORMAL");
        myDefFlagEx(hit, HT_TRANSPARENT, hitstr, "TRANSPARENT");
        myDefFlagEx(hit, HT_DISCARD, hitstr, "DISCARD");
        myDefFlagEx(hit, HT_ERROR, hitstr, "ERROR");
        QByteArray flstr;
        myDefFlagEx(fl, KC_CHAR, flstr, "CHAR");
        myDefFlagEx(fl, KC_VIRTUALKEY, flstr, "VIRT");
        myDefFlagEx(fl, KC_SCANCODE, flstr, "SCAN");
        myDefFlagEx(fl, KC_SHIFT, flstr, "SHIFT");
        myDefFlagEx(fl, KC_CTRL, flstr, "CTRL");
        myDefFlagEx(fl, KC_ALT, flstr, "ALT");
        myDefFlagEx(fl, KC_KEYUP, flstr, "UP");
        myDefFlagEx(fl, KC_PREVDOWN, flstr, "PREVDWN");
        myDefFlagEx(fl, KC_LONEKEY, flstr, "LONE");
        myDefFlagEx(fl, KC_DEADKEY, flstr, "DEAD");
        myDefFlagEx(fl, KC_COMPOSITE, flstr, "COMP");
        myDefFlagEx(fl, KC_INVALIDCOMP, flstr, "INVCMP");
        myDefFlagEx(fl, KC_TOGGLE, flstr, "TGGL");
        myDefFlagEx(fl, KC_INVALIDCHAR, flstr, "INVCHR");
        myDefFlagEx(fl, KC_NONE, flstr, "NONE");
        debug << "x" << x << "y" << y;
        debug.nospace() << "HT(" << hitstr.constData() << ")";
        debug.space();
        debug.nospace() << "KC(" << qDebugFmtHex(fl) << ","
                        << flstr.constData() << ")";
        debug.space();
    }

    return debug;

    #undef myCaseEnd
    #undef myCaseBegin
}

#undef myDefFlagCut
#undef myDefFlag
#undef myDefFlagEx

QT_END_NAMESPACE
