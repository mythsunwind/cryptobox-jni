// This Source Code Form is subject to the terms of
// the Mozilla Public License, v. 2.0. If a copy of
// the MPL was not distributed with this file, You
// can obtain one at http://mozilla.org/MPL/2.0/.

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <jni.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <cbox.h>

#define CBOXJNI_TAG "CryptoBox"

// Cache ////////////////////////////////////////////////////////////////////

jclass cboxjni_ex_class;
jclass cboxjni_box_class;
jclass cboxjni_sess_class;
jclass cboxjni_sessmsg_class;
jclass cboxjni_bytearr_class;
jclass cboxjni_pkbundle_class;

jmethodID cboxjni_ex_ctor;
jmethodID cboxjni_sess_ctor;
jmethodID cboxjni_box_ctor;
jmethodID cboxjni_sessmsg_ctor;
jmethodID cboxjni_pkbundle_ctor;

// Utilities ////////////////////////////////////////////////////////////////

void cboxjni_throw(JNIEnv * j_env, CBoxResult code) {
    jobject j_ex = (*j_env)->NewObject(j_env, cboxjni_ex_class, cboxjni_ex_ctor, code);
    if ((*j_env)->ExceptionCheck(j_env) == JNI_TRUE || j_ex == NULL) {
        return;
    }

    (*j_env)->Throw(j_env, j_ex);
}

bool cboxjni_check_error(JNIEnv * j_env, void const * val) {
    return (*j_env)->ExceptionCheck(j_env) == JNI_TRUE
            ||
            val == NULL;
}

jbyteArray cboxjni_vec2arr(JNIEnv * j_env, CBoxVec * v) {
    uint32_t v_len   = cbox_vec_len(v);
    jbyteArray j_arr = (*j_env)->NewByteArray(j_env, v_len);
    if (cboxjni_check_error(j_env, j_arr)) {
        cbox_vec_free(v);
        return NULL;
    }

    (*j_env)->SetByteArrayRegion(j_env, j_arr, 0, v_len, (jbyte *) cbox_vec_data(v));
    cbox_vec_free(v);
    if ((*j_env)->ExceptionCheck(j_env) == JNI_TRUE) {
        return NULL;
    }

    return j_arr;
}

jobject cboxjni_new_session(JNIEnv * j_env, CBoxSession * csess, jstring j_sid) {
    jlong j_sess_ptr = (jlong) (intptr_t) csess;
    jobject j_sess   = (*j_env)->NewObject(j_env, cboxjni_sess_class, cboxjni_sess_ctor, j_sess_ptr, j_sid);
    if (cboxjni_check_error(j_env, j_sess)) {
        return NULL;
    }

    return j_sess;
}

// CryptoBox ////////////////////////////////////////////////////////////////

JNIEXPORT jobject JNICALL
cboxjni_open(JNIEnv * j_env, jclass j_class, jstring j_dir) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Opening CryptoBox");
    #endif

    char const * dir = (*j_env)->GetStringUTFChars(j_env, j_dir, 0);
    if (cboxjni_check_error(j_env, dir)) {
        return NULL;
    }

    CBox * cbox = NULL;
    CBoxResult rc = cbox_file_open(dir, &cbox);
    (*j_env)->ReleaseStringUTFChars(j_env, j_dir, dir);

    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
        return NULL;
    }

    jlong ptr   = (jlong) (intptr_t) cbox;
    jobject obj = (*j_env)->NewObject(j_env, j_class, cboxjni_box_ctor, ptr);
    if (cboxjni_check_error(j_env, obj)) {
        return NULL;
    }

    return obj;
}

JNIEXPORT void JNICALL
cboxjni_close(JNIEnv * j_env, jclass j_class, jlong j_ptr) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Closing CryptoBox");
    #endif

    CBox * cbox = (CBox *) (intptr_t) j_ptr;
    cbox_close(cbox);
}

JNIEXPORT jobjectArray JNICALL
cboxjni_new_prekeys(JNIEnv * j_env, jclass j_class, jlong j_ptr, jint j_start, jint j_num) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Creating new prekeys");
    #endif

    CBox * cbox = (CBox *) (intptr_t) j_ptr;

    jobjectArray bundles = (*j_env)->NewObjectArray(j_env, j_num, cboxjni_pkbundle_class, 0);
    if (cboxjni_check_error(j_env, bundles)) {
        return NULL;
    }

    for (int i = 0; i < j_num; ++i) {
        jint j_id = j_start + i;
        CBoxVec * prekey = NULL;
        CBoxResult rc = cbox_new_prekey(cbox, j_id, &prekey);
        if (rc != CBOX_SUCCESS) {
            cboxjni_throw(j_env, rc);
            return NULL;
        }

        jbyteArray j_prekey = cboxjni_vec2arr(j_env, prekey);
        if (j_prekey == NULL) {
            return NULL;
        }

        jobject j_pkb = (*j_env)->NewObject(j_env, cboxjni_pkbundle_class, cboxjni_pkbundle_ctor, j_id, j_prekey);
        if (cboxjni_check_error(j_env, j_pkb)) {
            return NULL;
        }

        (*j_env)->SetObjectArrayElement(j_env, bundles, i, j_pkb);
        if ((*j_env)->ExceptionCheck(j_env) == JNI_TRUE) {
            return NULL;
        }
    }

    return bundles;
}

JNIEXPORT jbyteArray JNICALL
cboxjni_local_fingerprint(JNIEnv * j_env, jclass j_class, jlong j_ptr) {
    CBox * cbox = (CBox *) (intptr_t) j_ptr;

    CBoxVec * fp = NULL;
    cbox_fingerprint_local(cbox, &fp);

    return cboxjni_vec2arr(j_env, fp);
}

JNIEXPORT jobject JNICALL
cboxjni_init_from_prekey(JNIEnv * j_env, jclass j_class, jlong j_ptr, jstring j_sid, jbyteArray j_prekey) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Initialising session from prekey");
    #endif

    char const * sid = (*j_env)->GetStringUTFChars(j_env, j_sid, 0);
    if (cboxjni_check_error(j_env, sid)) {
        return NULL;
    }

    CBox * cbox = (CBox *) (intptr_t) j_ptr;

    uint16_t prekey_len = (*j_env)->GetArrayLength(j_env, j_prekey);
    jbyte *  prekey     = (*j_env)->GetByteArrayElements(j_env, j_prekey, NULL);

    if (prekey == NULL) {
        (*j_env)->ReleaseStringUTFChars(j_env, j_sid, sid);
        return NULL;
    }

    CBoxSession * sess = NULL;
    CBoxResult rc = cbox_session_init_from_prekey(cbox, sid, (uint8_t *) prekey, prekey_len, &sess);

    (*j_env)->ReleaseByteArrayElements(j_env, j_prekey, prekey, JNI_ABORT);
    (*j_env)->ReleaseStringUTFChars(j_env, j_sid, sid);

    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
        return NULL;
    }

    return cboxjni_new_session(j_env, sess, j_sid);
}

JNIEXPORT jobject JNICALL
cboxjni_init_from_message(JNIEnv * j_env, jclass j_class, jlong j_ptr, jstring j_sid, jbyteArray j_message) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Intialising session from message");
    #endif

    char const * sid = (*j_env)->GetStringUTFChars(j_env, j_sid, 0);
    if (cboxjni_check_error(j_env, sid)) {
        return NULL;
    }

    CBox * cbox = (CBox *) (intptr_t) j_ptr;

    uint32_t message_len = (*j_env)->GetArrayLength(j_env, j_message);
    jbyte *  message     = (*j_env)->GetByteArrayElements(j_env, j_message, NULL);

    if (message == NULL) {
        (*j_env)->ReleaseStringUTFChars(j_env, j_sid, sid);
        return NULL;
    }

    CBoxSession * sess = NULL;
    CBoxVec * plain = NULL;
    CBoxResult rc = cbox_session_init_from_message(cbox, sid, (uint8_t *) message, message_len, &sess, &plain);

    (*j_env)->ReleaseByteArrayElements(j_env, j_message, message, JNI_ABORT);
    (*j_env)->ReleaseStringUTFChars(j_env, j_sid, sid);

    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
        return NULL;
    }

    jobject j_sess = cboxjni_new_session(j_env, sess, j_sid);

    jbyteArray j_plaintext = cboxjni_vec2arr(j_env, plain);
    if (j_plaintext == NULL) {
        return NULL;
    }

    jobject j_swm = (*j_env)->NewObject(j_env, cboxjni_sessmsg_class, cboxjni_sessmsg_ctor, j_sess, j_plaintext);

    return j_swm;
}

JNIEXPORT jobject JNICALL
cboxjni_session_get(JNIEnv * j_env, jclass j_class, jlong j_ptr, jstring j_sid) {
    char const * sid = (*j_env)->GetStringUTFChars(j_env, j_sid, 0);
    if (cboxjni_check_error(j_env, sid)) {
        return NULL;
    }

    #ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Loading CryptoSession: %s", sid);
    #endif

    CBox * cbox = (CBox *) (intptr_t) j_ptr;

    CBoxSession * csess = NULL;
    CBoxResult rc = cbox_session_get(cbox, sid, &csess);

    (*j_env)->ReleaseStringUTFChars(j_env, j_sid, sid);

    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
        return NULL;
    }

    return cboxjni_new_session(j_env, csess, j_sid);
}

// CryptoSession ////////////////////////////////////////////////////////////

JNIEXPORT jbyteArray JNICALL
cboxjni_session_encrypt(JNIEnv * j_env, jclass j_class, jlong j_ptr, jbyteArray j_plain) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Encrypting message");
    #endif

    CBoxSession * csess = (CBoxSession *) (intptr_t) j_ptr;

    uint32_t plain_len = (*j_env)->GetArrayLength(j_env, j_plain);
    jbyte *  plain     = (*j_env)->GetByteArrayElements(j_env, j_plain, NULL);

    if (cboxjni_check_error(j_env, plain)) {
        return NULL;
    }

    CBoxVec * cipher = NULL;
    cbox_encrypt(csess, (uint8_t *) plain, plain_len, &cipher);

    (*j_env)->ReleaseByteArrayElements(j_env, j_plain, plain, JNI_ABORT);

    return cboxjni_vec2arr(j_env, cipher);
}

JNIEXPORT jbyteArray JNICALL
cboxjni_session_decrypt(JNIEnv * j_env, jclass j_class, jlong j_ptr, jbyteArray j_cipher) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Decrypting message");
    #endif

    CBoxSession * csess = (CBoxSession *) (intptr_t) j_ptr;

    uint32_t cipher_len = (*j_env)->GetArrayLength(j_env, j_cipher);
    jbyte *  cipher     = (*j_env)->GetByteArrayElements(j_env, j_cipher, NULL);

    if (cboxjni_check_error(j_env, cipher)) {
        return NULL;
    }

    CBoxVec * plain = NULL;
    CBoxResult rc = cbox_decrypt(csess, (uint8_t *) cipher, cipher_len, &plain);

    (*j_env)->ReleaseByteArrayElements(j_env, j_cipher, cipher, JNI_ABORT);

    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
        return NULL;
    }

    return cboxjni_vec2arr(j_env, plain);
}

JNIEXPORT void JNICALL
cboxjni_session_save(JNIEnv * j_env, jclass j_class, jlong j_ptr) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Saving CryptoSession");
    #endif

    CBoxSession * csess = (CBoxSession *) (intptr_t) j_ptr;

    CBoxResult rc = cbox_session_save(csess);
    if (rc != CBOX_SUCCESS) {
        cboxjni_throw(j_env, rc);
    }
}

JNIEXPORT void JNICALL
cboxjni_session_close(JNIEnv * j_env, jclass j_class, jlong j_ptr) {
    #ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_VERBOSE, CBOXJNI_TAG, "Closing CryptoSession");
    #endif

    CBoxSession * csess = (CBoxSession *) (intptr_t) j_ptr;

    cbox_session_close(csess);
}

JNIEXPORT jbyteArray JNICALL
cboxjni_remote_fingerprint(JNIEnv * j_env, jclass j_class, jlong j_ptr) {
    CBoxSession * csess = (CBoxSession *) (intptr_t) j_ptr;

    CBoxVec * fp = NULL;
    cbox_fingerprint_remote(csess, &fp);

    return cboxjni_vec2arr(j_env, fp);
}

// Bookkeeping //////////////////////////////////////////////////////////////

static JNINativeMethod cboxjni_box_methods[] = {
    { "jniOpen"                  , "(Ljava/lang/String;)Lorg/pkaboo/cryptobox/CryptoBox;"         , (void *) cboxjni_open              },
    { "jniClose"                 , "(J)V"                                                         , (void *) cboxjni_close             },
    { "jniNewPreKeys"            , "(JII)[Lorg/pkaboo/cryptobox/PreKey;"                          , (void *) cboxjni_new_prekeys       },
    { "jniGetLocalFingerprint"   , "(J)[B"                                                        , (void *) cboxjni_local_fingerprint },
    { "jniInitSessionFromPreKey" , "(JLjava/lang/String;[B)Lorg/pkaboo/cryptobox/CryptoSession;"  , (void *) cboxjni_init_from_prekey  },
    { "jniInitSessionFromMessage", "(JLjava/lang/String;[B)Lorg/pkaboo/cryptobox/SessionMessage;" , (void *) cboxjni_init_from_message },
    { "jniGetSession"            , "(JLjava/lang/String;)Lorg/pkaboo/cryptobox/CryptoSession;"    , (void *) cboxjni_session_get       }
};

static JNINativeMethod cboxjni_sess_methods[] = {
    { "jniEncrypt"              , "(J[B)[B" , (void *) cboxjni_session_encrypt    },
    { "jniDecrypt"              , "(J[B)[B" , (void *) cboxjni_session_decrypt    },
    { "jniSave"                 , "(J)V"    , (void *) cboxjni_session_save       },
    { "jniClose"                , "(J)V"    , (void *) cboxjni_session_close      },
    { "jniGetRemoteFingerprint" , "(J)[B"   , (void *) cboxjni_remote_fingerprint }
};

jmethodID cboxjni_find_method(JNIEnv * j_env, jclass cls, char const * name, char const * sig) {
    jmethodID mtd = (*j_env)->GetMethodID(j_env, cls, name, sig);
    if ((*j_env)->ExceptionCheck(j_env) == JNI_TRUE) {
        return NULL;
    }
    return mtd;
}

jclass cboxjni_find_class(JNIEnv * j_env, char const * name) {
    jclass j_class = (*j_env)->FindClass(j_env, name);
    if ((*j_env)->ExceptionCheck(j_env) == JNI_TRUE) {
        #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, CBOXJNI_TAG, "Failed to find class: %s", name);
        #endif
        return NULL;
    }

    jclass j_class_ref = (jclass) (*j_env)->NewGlobalRef(j_env, j_class);
    (*j_env)->DeleteLocalRef(j_env, j_class);

    return j_class_ref;
}

jint JNI_OnLoad(JavaVM * vm, void * reserved) {
    JNIEnv * j_env;
    if ((*vm)->GetEnv(vm, (void **) &j_env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_EVERSION;
    }

    cboxjni_ex_class = cboxjni_find_class(j_env, "org/pkaboo/cryptobox/CryptoException");
    if (cboxjni_ex_class == NULL) return JNI_ERR;

    cboxjni_box_class = cboxjni_find_class(j_env, "org/pkaboo/cryptobox/CryptoBox");
    if (cboxjni_box_class == NULL) return JNI_ERR;

    cboxjni_sess_class = cboxjni_find_class(j_env, "org/pkaboo/cryptobox/CryptoSession");
    if (cboxjni_sess_class == NULL) return JNI_ERR;

    cboxjni_sessmsg_class = cboxjni_find_class(j_env, "org/pkaboo/cryptobox/SessionMessage");
    if (cboxjni_sessmsg_class == NULL) return JNI_ERR;

    cboxjni_bytearr_class = cboxjni_find_class(j_env, "[B");
    if (cboxjni_bytearr_class == NULL) return JNI_ERR;

    cboxjni_pkbundle_class = cboxjni_find_class(j_env, "org/pkaboo/cryptobox/PreKey");
    if (cboxjni_pkbundle_class == NULL) return JNI_ERR;

    cboxjni_ex_ctor = cboxjni_find_method(j_env, cboxjni_ex_class, "<init>", "(I)V");
    if (cboxjni_ex_ctor == NULL) return JNI_ERR;

    cboxjni_sess_ctor = cboxjni_find_method(j_env, cboxjni_sess_class, "<init>", "(JLjava/lang/String;)V");
    if (cboxjni_sess_ctor == NULL) return JNI_ERR;

    cboxjni_box_ctor = cboxjni_find_method(j_env, cboxjni_box_class, "<init>", "(J)V");
    if (cboxjni_box_ctor == NULL) return JNI_ERR;

    cboxjni_sessmsg_ctor = cboxjni_find_method(j_env, cboxjni_sessmsg_class, "<init>", "(Lorg/pkaboo/cryptobox/CryptoSession;[B)V");
    if (cboxjni_sessmsg_ctor == NULL) return JNI_ERR;

    cboxjni_pkbundle_ctor = cboxjni_find_method(j_env, cboxjni_pkbundle_class, "<init>", "(I[B)V");
    if (cboxjni_sessmsg_ctor == NULL) return JNI_ERR;

    jint rc = (*j_env)->RegisterNatives(j_env, cboxjni_box_class, cboxjni_box_methods, sizeof(cboxjni_box_methods)/sizeof(cboxjni_box_methods[0]));
    if (rc < 0) return JNI_ERR;

    rc = (*j_env)->RegisterNatives(j_env, cboxjni_sess_class, cboxjni_sess_methods, sizeof(cboxjni_sess_methods)/sizeof(cboxjni_sess_methods[0]));
    if (rc < 0) return JNI_ERR;

    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM * vm, void * reserved) {
    JNIEnv * j_env;
    if ((*vm)->GetEnv(vm, (void **) &j_env, JNI_VERSION_1_6) != JNI_OK) {
        return;
    }
    if (cboxjni_ex_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_ex_class); }
    if (cboxjni_box_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_box_class); }
    if (cboxjni_sess_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_sess_class); }
    if (cboxjni_sessmsg_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_sessmsg_class); }
    if (cboxjni_bytearr_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_bytearr_class); }
    if (cboxjni_pkbundle_class != NULL) { (*j_env)->DeleteGlobalRef(j_env, cboxjni_pkbundle_class); }
}