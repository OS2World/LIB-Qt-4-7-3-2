
TARGET = $$qtLibraryTarget($$TARGET$$QT_LIBINFIX) #do this towards the end
!isEmpty(TARGET_SHORT):TARGET_SHORT = $$qtLibraryTarget($$TARGET_SHORT$$QT_LIBINFIX) #do this towards the end
