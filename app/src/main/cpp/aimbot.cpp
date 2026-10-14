#include <jni.h>

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_nexusvpn_secure_AimbotService_nativeStart(JNIEnv *env, jobject thiz) {
    return true;
}

JNIEXPORT void JNICALL
Java_com_nexusvpn_secure_AimbotService_nativeStop(JNIEnv *env, jobject thiz) {}

JNIEXPORT void JNICALL
Java_com_nexusvpn_secure_AimbotService_nativeSetActive(JNIEnv *env, jobject thiz, jboolean a) {}

JNIEXPORT void JNICALL
Java_com_nexusvpn_secure_AimbotService_nativeSetSmooth(JNIEnv *env, jobject thiz, jint s) {}

}
