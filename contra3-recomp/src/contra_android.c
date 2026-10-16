/*
 * Contra III — Android glue.
 *
 * The on-screen touch pad (android/.../TouchPadView.java) reports the SNES
 * buttons it holds through nativeSetTouchPad; the desktop host ORs them into
 * player 1's pad every frame through the extra_inputs hook (src/main.c).
 */
#include "contra_android.h"

#include <jni.h>
#include <stdatomic.h>

static atomic_uint s_touch_pad;

JNIEXPORT void JNICALL
Java_com_contra3_recomp_Contra3Activity_nativeSetTouchPad(JNIEnv *env,
                                                          jclass cls,
                                                          jint buttons) {
  (void)env;
  (void)cls;
  atomic_store_explicit(&s_touch_pad, (unsigned)buttons & 0xfffu,
                        memory_order_relaxed);
}

uint32_t contra_android_touch_inputs(void) {
  return atomic_load_explicit(&s_touch_pad, memory_order_relaxed);
}
