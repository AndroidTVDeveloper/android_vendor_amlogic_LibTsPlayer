LOCAL_PATH := $(call my-dir)

lib_dir :=

include $(CLEAR_VARS)
LOCAL_MODULE := libliveplayer
LOCAL_SRC_FILES := libliveplayer
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := $(lib_dir)$(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libCTC_AmMediaProcessor
LOCAL_SRC_FILES := libCTC_AmMediaProcessor
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := $(lib_dir)$(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libffmpeg30
LOCAL_SRC_FILES := libffmpeg30
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := $(lib_dir)$(LOCAL_MODULE)$(LOCAL_MODULE_SUFFIX)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/
include $(BUILD_PREBUILT)
