QMAKE_TARGET_COMPANY = Nokia Corporation and/or its subsidiary(-ies)
QMAKE_TARGET_PRODUCT = Qt4
QMAKE_TARGET_DESCRIPTION = C++ application development framework.
QMAKE_TARGET_COPYRIGHT = Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).

os2 {
    # too long company name does not fit the designated field
    QMAKE_TARGET_COMPANY = Nokia
    !isEmpty(QT_MAJOR_VERSION) {
        QMAKE_TARGET_VERSION=$${QT_MAJOR_VERSION}.$${QT_MINOR_VERSION}.$${QT_PATCH_VERSION}
        !isEmpty(QT_BUILD_VERSION) {
            # extend the version number embedded in EXEs/DLLs with the build number
            QMAKE_TARGET_VERSION = $${QMAKE_TARGET_VERSION}.$${QT_BUILD_VERSION}
        }
    }
}
