TARGET     = QtGui
QPRO_PWD   = $$PWD
QT = core
DEFINES   += QT_BUILD_GUI_LIB QT_NO_USING_NAMESPACE
win32-msvc*|win32-icc:QMAKE_LFLAGS += /BASE:0x65000000
irix-cc*:QMAKE_CXXFLAGS += -no_prelink -ptused

!win32:!embedded:!mac:!symbian:!os2:CONFIG += x11

unix:QMAKE_PKGCONFIG_REQUIRES = QtCore

include(../qbase.pri)

contains(QT_CONFIG, x11sm):CONFIG += x11sm

#platforms
x11:include(kernel/x11.pri)
mac:include(kernel/mac.pri)
win32:include(kernel/win.pri)
embedded:include(embedded/embedded.pri)
symbian {
    include(kernel/symbian.pri)
    include(s60framework/s60framework.pri)
}

#modules
include(animation/animation.pri)
include(kernel/kernel.pri)
include(image/image.pri)
include(painting/painting.pri)
include(text/text.pri)
include(styles/styles.pri)
include(widgets/widgets.pri)
include(dialogs/dialogs.pri)
include(accessible/accessible.pri)
include(itemviews/itemviews.pri)
include(inputmethod/inputmethod.pri)
include(graphicsview/graphicsview.pri)
include(util/util.pri)
include(statemachine/statemachine.pri)
include(math3d/math3d.pri)
include(effects/effects.pri)

include(egl/egl.pri)
win32:!wince*: DEFINES += QT_NO_EGL
embedded: QT += network

QMAKE_LIBS += $$QMAKE_LIBS_GUI

contains(DEFINES,QT_EVAL):include($$QT_SOURCE_TREE/src/corelib/eval.pri)

QMAKE_DYNAMIC_LIST_FILE = $$PWD/QtGui.dynlist

DEFINES += Q_INTERNAL_QAPP_SRC
symbian {
    TARGET.UID3=0x2001B2DD

    symbian-abld|symbian-sbsv2 {
        # ro-section in gui can exceed default allocated space, so move rw-section a little further
        QMAKE_LFLAGS.ARMCC += --rw-base 0x800000
        QMAKE_LFLAGS.GCCE += -Tdata 0xC00000
    }
}

neon:*-g++* {
    DEFINES += QT_HAVE_NEON
    HEADERS += $$NEON_HEADERS
    SOURCES += $$NEON_SOURCES

    DRAWHELPER_NEON_ASM_FILES = $$NEON_ASM

    neon_compiler.commands = $$QMAKE_CXX -c -mfpu=neon
    neon_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    neon_compiler.dependency_type = TYPE_C
    neon_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
    neon_compiler.input = DRAWHELPER_NEON_ASM_FILES
    neon_compiler.variable_out = OBJECTS
    neon_compiler.name = compiling[neon] ${QMAKE_FILE_IN}
    silent:neon_compiler.commands = @echo compiling[neon] ${QMAKE_FILE_IN} && $$neon_compiler.commands
    QMAKE_EXTRA_COMPILERS += neon_compiler
}

contains(QMAKE_MAC_XARCH, no) {
    DEFINES += QT_NO_MAC_XARCH
} else {
    win32-g++*|!win32:!win32-icc*:!macx-icc* {
        os2 {
            DRAWHELPER_OS2_COMPILER = $$QMAKE_RUN_CXX -Winline
            DRAWHELPER_OS2_COMPILER = $$replace(DRAWHELPER_OS2_COMPILER, \\$src, ${QMAKE_FILE_IN})
            DRAWHELPER_OS2_COMPILER = $$replace(DRAWHELPER_OS2_COMPILER, \\$obj, ${QMAKE_FILE_OUT})
        }
        mmx {
            os2:mmx_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:mmx_compiler.commands = $$QMAKE_CXX -c -Winline

            mac {
                mmx_compiler.commands += -Xarch_i386 -mmmx
                mmx_compiler.commands += -Xarch_x86_64 -mmmx
            } else {
                mmx_compiler.commands += -mmmx
            }

            !os2:mmx_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            mmx_compiler.dependency_type = TYPE_C
            mmx_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            mmx_compiler.input = MMX_SOURCES
            mmx_compiler.variable_out = OBJECTS
            mmx_compiler.name = compiling[mmx] ${QMAKE_FILE_IN}
            silent:mmx_compiler.commands = @echo compiling[mmx] ${QMAKE_FILE_IN} && $$mmx_compiler.commands
            QMAKE_EXTRA_COMPILERS += mmx_compiler
        }
        3dnow {
            os2:mmx3dnow_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:mmx3dnow_compiler.commands = $$QMAKE_CXX -c -Winline

            mac {
                mmx3dnow_compiler.commands += -Xarch_i386 -m3dnow -Xarch_i386 -mmmx
                mmx3dnow_compiler.commands += -Xarch_x86_64 -m3dnow -Xarch_x86_64 -mmmx
            } else {
                mmx3dnow_compiler.commands += -m3dnow -mmmx
            }

            !os2:mmx3dnow_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            mmx3dnow_compiler.dependency_type = TYPE_C
            mmx3dnow_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            mmx3dnow_compiler.input = MMX3DNOW_SOURCES
            mmx3dnow_compiler.variable_out = OBJECTS
            mmx3dnow_compiler.name = compiling[mmx3dnow] ${QMAKE_FILE_IN}
            silent:mmx3dnow_compiler.commands = @echo compiling[mmx3dnow] ${QMAKE_FILE_IN} && $$mmx3dnow_compiler.commands
            QMAKE_EXTRA_COMPILERS += mmx3dnow_compiler

            sse {
                os2:sse3dnow_compiler.commands = $$DRAWHELPER_OS2_COMPILER
                else:sse3dnow_compiler.commands = $$QMAKE_CXX -c -Winline

                mac {
                    sse3dnow_compiler.commands += -Xarch_i386 -m3dnow -Xarch_i386 -msse
                    sse3dnow_compiler.commands += -Xarch_x86_64 -m3dnow -Xarch_x86_64 -msse
                } else:os2 {
                    # SSE data types (__m128i etc.) don't get 16-byte alignment on stack with GCC 4.4.4
                    # (compiler bug?), force it with -mstackrealign to avoid spontaneous SIGSEGVs
                    sse3dnow_compiler.commands += -m3dnow -msse -mstackrealign
                } else {
                    sse3dnow_compiler.commands += -m3dnow -msse
                }

                !os2:sse3dnow_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

                sse3dnow_compiler.dependency_type = TYPE_C
                sse3dnow_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
                sse3dnow_compiler.input = SSE3DNOW_SOURCES
                sse3dnow_compiler.variable_out = OBJECTS
                sse3dnow_compiler.name = compiling[sse3dnow] ${QMAKE_FILE_IN}
                silent:sse3dnow_compiler.commands = @echo compiling[sse3dnow] ${QMAKE_FILE_IN} && $$sse3dnow_compiler.commands
                QMAKE_EXTRA_COMPILERS += sse3dnow_compiler
            }
        }
        sse {
            os2:sse_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:sse_compiler.commands = $$QMAKE_CXX -c -Winline

            mac {
                sse_compiler.commands += -Xarch_i386 -msse
                sse_compiler.commands += -Xarch_x86_64 -msse
            } else:os2 {
                # SSE data types (__m128i etc.) don't get 16-byte alignment on stack with GCC 4.4.4
                # (compiler bug?), force it with -mstackrealign to avoid spontaneous SIGSEGVs
                sse_compiler.commands += -msse -mstackrealign
            } else {
                sse_compiler.commands += -msse
            }

            !os2:sse_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            sse_compiler.dependency_type = TYPE_C
            sse_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            sse_compiler.input = SSE_SOURCES
            sse_compiler.variable_out = OBJECTS
            sse_compiler.name = compiling[sse] ${QMAKE_FILE_IN}
            silent:sse_compiler.commands = @echo compiling[sse] ${QMAKE_FILE_IN} && $$sse_compiler.commands
            QMAKE_EXTRA_COMPILERS += sse_compiler
        }
        sse2 {
            os2:sse2_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:sse2_compiler.commands = $$QMAKE_CXX -c -Winline

            mac {
                sse2_compiler.commands += -Xarch_i386 -msse2
                sse2_compiler.commands += -Xarch_x86_64 -msse2
            } else:os2 {
                # SSE data types (__m128i etc.) don't get 16-byte alignment on stack with GCC 4.4.4
                # (compiler bug?), force it with -mstackrealign to avoid spontaneous SIGSEGVs
                sse2_compiler.commands += -msse2 -mstackrealign
            } else {
                sse2_compiler.commands += -msse2
            }

            !os2:sse2_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            sse2_compiler.dependency_type = TYPE_C
            sse2_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            sse2_compiler.input = SSE2_SOURCES
            sse2_compiler.variable_out = OBJECTS
            sse2_compiler.name = compiling[sse2] ${QMAKE_FILE_IN}
            silent:sse2_compiler.commands = @echo compiling[sse2] ${QMAKE_FILE_IN} && $$sse2_compiler.commands
            QMAKE_EXTRA_COMPILERS += sse2_compiler
        }
        ssse3 {
            os2:ssse3_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:ssse3_compiler.commands = $$QMAKE_CXX -c -Winline

            mac {
                ssse3_compiler.commands += -Xarch_i386 -mssse3
                ssse3_compiler.commands += -Xarch_x86_64 -mssse3
            } else {
                ssse3_compiler.commands += -mssse3
            }

            !os2:ssse3_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            ssse3_compiler.dependency_type = TYPE_C
            ssse3_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            ssse3_compiler.input = SSSE3_SOURCES
            ssse3_compiler.variable_out = OBJECTS
            ssse3_compiler.name = compiling[ssse3] ${QMAKE_FILE_IN}
            silent:ssse3_compiler.commands = @echo compiling[ssse3] ${QMAKE_FILE_IN} && $$ssse3_compiler.commands
            QMAKE_EXTRA_COMPILERS += ssse3_compiler
        }
        iwmmxt {
            os2:iwmmxt_compiler.commands = $$DRAWHELPER_OS2_COMPILER
            else:iwmmxt_compiler.commands = $$QMAKE_CXX -c -Winline

            iwmmxt_compiler.commands += -mcpu=iwmmxt

            !os2:iwmmxt_compiler.commands += $(CXXFLAGS) $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}

            iwmmxt_compiler.dependency_type = TYPE_C
            iwmmxt_compiler.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
            iwmmxt_compiler.input = IWMMXT_SOURCES
            iwmmxt_compiler.variable_out = OBJECTS
            iwmmxt_compiler.name = compiling[iwmmxt] ${QMAKE_FILE_IN}
            silent:iwmmxt_compiler.commands = @echo compiling[iwmmxt] ${QMAKE_FILE_IN} && $$iwmmxt_compiler.commands
            QMAKE_EXTRA_COMPILERS += iwmmxt_compiler
        }
    } else {
        mmx: SOURCES += $$MMX_SOURCES
        3dnow: SOURCES += $$MMX3DNOW_SOURCES
        3dnow:sse: SOURCES += $$SSE3DNOW_SOURCES
        sse: SOURCES += $$SSE_SOURCES
        sse2: SOURCES += $$SSE2_SOURCES
        ssse3: SOURCES += $$SSSE3_SOURCES
        iwmmxt: SOURCES += $$IWMMXT_SOURCES
    }
}
