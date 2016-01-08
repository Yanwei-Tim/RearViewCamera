/*
 * easycam.h
 *
 *  Created on: Nov 6, 2014
 *      Author: Eric
 */

#ifndef EASYCAM_H_
#define EASYCAM_H_

#include <jni.h>
#include "VideoDevice.h"
#include "FrameRenderer.h"

VideoDevice* vDevice = nullptr;
FrameRenderer* fRenderer = nullptr;

extern "C" {
    JNIEXPORT jint JNICALL Java_kg_delletenebre_rearviewcamera_NativeEasyCapture_startDevice(JNIEnv* jenv, jobject thisObj,
    		jstring rsPath, jstring deviceName, jint width, jint height, jint devType,
            jint regionStd, jint numBufs);
    JNIEXPORT void JNICALL Java_kg_delletenebre_rearviewcamera_NativeEasyCapture_getNextFrame(JNIEnv* jenv, jobject thisObj,
                                                                                   jobject surface);
    JNIEXPORT jboolean JNICALL Java_kg_delletenebre_rearviewcamera_NativeEasyCapture_isDeviceAttached(JNIEnv* jenv, jobject thisObj);
    JNIEXPORT void JNICALL Java_kg_delletenebre_rearviewcamera_NativeEasyCapture_stopDevice(JNIEnv* jenv, jobject thisObj);
    JNIEXPORT jstring JNICALL Java_kg_delletenebre_rearviewcamera_NativeEasyCapture_detectDevice(JNIEnv* jenv, jobject thisObj, jstring deviceName);
};


#endif /* EASYCAM_H_ */
