#####################################################################
# Qt documentation build
#####################################################################

DOCS_GENERATION_DEFINES =
GENERATOR = $$QT_BUILD_TREE/bin/qhelpgenerator

win32:!win32-g++* {
    unixstyle = false
} else :win32-g++*:isEmpty(QMAKE_SH) {
    unixstyle = false
} else :os2:isEmpty(QMAKE_SH) {
    unixstyle = false
} else {
    unixstyle = true
}

$$unixstyle {
    QDOC = cd $$QT_SOURCE_TREE/tools/qdoc3/test && QT_BUILD_TREE=$$QT_BUILD_TREE QT_SOURCE_TREE=$$QT_SOURCE_TREE $$QT_BUILD_TREE/bin/qdoc3 $$DOCS_GENERATION_DEFINES
} else {
    QDOC = cd $$QT_SOURCE_TREE/tools/qdoc3/test && set QT_BUILD_TREE=$$QT_BUILD_TREE&& set QT_SOURCE_TREE=$$QT_SOURCE_TREE&& $$QT_BUILD_TREE/bin/qdoc3.exe $$DOCS_GENERATION_DEFINES
    QDOC = $$replace(QDOC, "/", "\\")
}

# overcome OS/2 1024 command line length limitation
os2:isEmpty(QMAKE_SH) {
    qdoc_cmd.target = $$QT_BUILD_TREE/qdoc.cmd
    qdoc_cmd.commands = echo "@echo off" > $${qdoc_cmd.target}$$escape_expand(\\n\\t)
    qdoc_cmd.commands += echo $$replace(QDOC, "&&", ">> $${qdoc_cmd.target}"$$escape_expand(\\n\\t)" echo") %1 %2 %3 %4 %5 %6 %7 %8 %9 ">> $${qdoc_cmd.target}"
    QDOC = cmd /c $$QT_BUILD_TREE/qdoc.cmd
    adp_docs.depends += qdoc_cmd
    QMAKE_EXTRA_TARGETS += qdoc_cmd
}

ADP_DOCS_QDOCCONF_FILE = -online qt-build-docs.qdocconf
QT_DOCUMENTATION = ($$QDOC -creator qt-api-only.qdocconf assistant.qdocconf designer.qdocconf \
                    linguist.qdocconf qmake.qdocconf qdeclarative.qdocconf) && \
               (cd $$QT_BUILD_TREE && \
                    $$GENERATOR doc-build/html-qt/qt.qhp -o doc/qch/qt.qch && \
                    $$GENERATOR doc-build/html-assistant/assistant.qhp -o doc/qch/assistant.qch && \
                    $$GENERATOR doc-build/html-designer/designer.qhp -o doc/qch/designer.qch && \
                    $$GENERATOR doc-build/html-linguist/linguist.qhp -o doc/qch/linguist.qch && \
                    $$GENERATOR doc-build/html-qmake/qmake.qhp -o doc/qch/qmake.qch && \
                    $$GENERATOR doc-build/html-qml/qml.qhp -o doc/qch/qml.qch \
               )

# overcome OS/2 1024 command line length limitation
os2:isEmpty(QMAKE_SH) {
    qt_documentation_cmd.target = $$QT_BUILD_TREE/qt_documentation.cmd
    qt_documentation_cmd.depends += qdoc_cmd
    qt_documentation_cmd.commands = echo "@echo off" > $${qt_documentation_cmd.target}$$escape_expand(\\n\\t)
    qt_documentation_cmd.tmp = $$replace(QT_DOCUMENTATION,"[\\x0028\\x0029]","")
    qt_documentation_cmd.commands += echo $$replace(qt_documentation_cmd.tmp, "&&", ">> $${qt_documentation_cmd.target}"$$escape_expand(\\n\\t)" echo") ">> $${qt_documentation_cmd.target}"
    QT_DOCUMENTATION = $$QT_BUILD_TREE/qt_documentation.cmd
    qch_docs.depends += qt_documentation_cmd
    QMAKE_EXTRA_TARGETS += qt_documentation_cmd
}

QT_ZH_CN_DOCUMENTATION = ($$QDOC qt-api-only_zh_CN.qdocconf) && \
               (cd $$QT_BUILD_TREE && \
                    $$GENERATOR doc-build/html-qt_zh_CN/qt.qhp -o doc/qch/qt_zh_CN.qch \
               )

QT_JA_JP_DOCUMENTATION = ($$QDOC qt-api-only_ja_JP.qdocconf) && \
               (cd $$QT_BUILD_TREE && \
                    $$GENERATOR doc-build/html-qt_ja_JP/qt.qhp -o doc/qch/qt_ja_JP.qch \
               )

win32-g++*:isEmpty(QMAKE_SH) {
	QT_DOCUMENTATION = $$replace(QT_DOCUMENTATION, "/", "\\\\")
	QT_ZH_CN_DOCUMENTATION = $$replace(QT_ZH_CN_DOCUMENTATION, "/", "\\\\")
	QT_JA_JP_DOCUMENTATION = $$replace(QT_JA_JP_DOCUMENTATION, "/", "\\\\")
}

# Build rules:
adp_docs.commands = ($$QDOC $$ADP_DOCS_QDOCCONF_FILE)
adp_docs.depends += sub-qdoc3 # qdoc3
qch_docs.commands = $$QT_DOCUMENTATION
qch_docs.depends += sub-qdoc3

docs.depends = sub-qdoc3 adp_docs qch_docs

docs_zh_CN.depends = docs
docs_zh_CN.commands = $$QT_ZH_CN_DOCUMENTATION

docs_ja_JP.depends = docs
docs_ja_JP.commands = $$QT_JA_JP_DOCUMENTATION

# Install rules
htmldocs.files = $$QT_BUILD_TREE/doc/html
htmldocs.path = $$[QT_INSTALL_DOCS]
htmldocs.CONFIG += no_check_exist directory

qchdocs.files= $$QT_BUILD_TREE/doc/qch
qchdocs.path = $$[QT_INSTALL_DOCS]
qchdocs.CONFIG += no_check_exist directory

docimages.files = $$QT_BUILD_TREE/doc/src/images
docimages.path = $$[QT_INSTALL_DOCS]/src

sub-qdoc3.depends = sub-corelib sub-xml
$$unixstyle:sub-qdoc3.commands += (cd tools/qdoc3 && $(MAKE))
else:sub-qdoc3.commands += (cd tools\\qdoc3 && $(MAKE))

QMAKE_EXTRA_TARGETS += sub-qdoc3 adp_docs qch_docs docs docs_zh_CN docs_ja_JP
INSTALLS += htmldocs qchdocs docimages
