LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := Kontur
LOCAL_SRC_FILES := kontur.cpp
LOCAL_LDLIBS := -llog -ldl
LOCAL_CFLAGS := -O2 -fvisibility=hidden -ffunction-sections -fdata-sections
LOCAL_CPPFLAGS := -std=c++17
LOCAL_ARM_MODE := arm

include $(BUILD_SHARED_LIBRARY)
