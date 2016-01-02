LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_LDLIBS := -llog
LOCAL_MODULE    := EventMonitor
LOCAL_SRC_FILES := EventMonitor.c
include $(BUILD_SHARED_LIBRARY)
