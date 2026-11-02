/*
 * second.c — a second screen: the pause menu (pause.c), live.
 *
 * On Android (the AYN Thor and other dual-screen devices) the Java side
 * (HarvestMoonActivity, SecondScreen) puts a Presentation on the other
 * display and tells us when it shows (nativeSecondShown). Every other frame
 * the menu is drawn here, 256 x 224, and handed over (secondFrame, an int[]
 * of 0xAARRGGBB, Android's own pixel order); the touches on it come back
 * (nativeSecondTouch, on the Java UI thread) through a small queue, and are
 * applied here, on the game's thread, between two frames.
 *
 * Elsewhere there is no second screen: nothing happens.
 */
#include <string.h>

#include <SDL3/SDL.h>

#include "second.h"
#include "config.h"
#include "pause.h"

enum { SW = 256, SH = 224, QMAX = 64 };

void second_render(uint32_t *dst) { pause_live_render(dst); }

#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>

static SDL_Mutex *s_mx;
static struct { int a, x, y; } s_q[QMAX];
static int s_qn;
static SDL_AtomicInt s_shown;
static jclass s_cls;
static jmethodID s_m_frame, s_m_enable;
static jintArray s_arr;
static uint32_t s_px[SW * SH];

JNIEXPORT void JNICALL Java_io_github_rodriloy_harvestmoon_HarvestMoonActivity_nativeSecondTouch(JNIEnv *env, jclass cls, jint a, jint x, jint y)
{
  (void)env; (void)cls;
  if (!s_mx) return;
  SDL_LockMutex(s_mx);
  if (a == 1 && s_qn && s_q[s_qn - 1].a == 1) { s_q[s_qn - 1].x = x; s_q[s_qn - 1].y = y; }   /* moves: the last one */
  else if (s_qn < QMAX) { s_q[s_qn].a = a; s_q[s_qn].x = x; s_q[s_qn].y = y; s_qn++; }
  SDL_UnlockMutex(s_mx);
}
JNIEXPORT void JNICALL Java_io_github_rodriloy_harvestmoon_HarvestMoonActivity_nativeSecondShown(JNIEnv *env, jclass cls, jboolean on)
{
  (void)env; (void)cls;
  SDL_SetAtomicInt(&s_shown, on ? 1 : 0);
}

static JNIEnv *jni(void)
{
  JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
  if (!env) return NULL;
  if (s_cls) return env;
  jobject act = (jobject)SDL_GetAndroidActivity();
  if (!act) return NULL;
  jclass c = (*env)->GetObjectClass(env, act);
  s_cls = (jclass)(*env)->NewGlobalRef(env, c);
  (*env)->DeleteLocalRef(env, c);
  (*env)->DeleteLocalRef(env, act);
  s_m_frame = (*env)->GetStaticMethodID(env, s_cls, "secondFrame", "([I)V");
  s_m_enable = (*env)->GetStaticMethodID(env, s_cls, "secondEnable", "(Z)V");
  if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); s_m_frame = s_m_enable = NULL; }
  jintArray a = (*env)->NewIntArray(env, SW * SH);
  if (a) { s_arr = (jintArray)(*env)->NewGlobalRef(env, a); (*env)->DeleteLocalRef(env, a); }
  return env;
}

void second_init(void)
{
  if (!s_mx) s_mx = SDL_CreateMutex();
  second_apply();
}
void second_apply(void)
{
  JNIEnv *env = jni();
  if (!env || !s_m_enable) return;
  (*env)->CallStaticVoidMethod(env, s_cls, s_m_enable, (jboolean)(g_cfg.second_screen ? JNI_TRUE : JNI_FALSE));
  if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}
bool second_shown(void) { return g_cfg.second_screen && SDL_GetAtomicInt(&s_shown); }

void second_frame(void)
{
  static unsigned n;
  if (!second_shown() || !s_mx) return;
  /* the touches, in order */
  int q[QMAX][3], k = 0;
  SDL_LockMutex(s_mx);
  for (int i = 0; i < s_qn; i++) { q[i][0] = s_q[i].a; q[i][1] = s_q[i].x; q[i][2] = s_q[i].y; }
  k = s_qn; s_qn = 0;
  SDL_UnlockMutex(s_mx);
  for (int i = 0; i < k; i++) pause_live_touch(q[i][0], q[i][1], q[i][2]);
  /* the picture, 30 times a second (or at once after a touch) */
  if ((++n & 1) && !k) return;
  JNIEnv *env = jni();
  if (!env || !s_m_frame || !s_arr) return;
  pause_live_render(s_px);
  (*env)->SetIntArrayRegion(env, s_arr, 0, SW * SH, (const jint *)s_px);
  (*env)->CallStaticVoidMethod(env, s_cls, s_m_frame, s_arr);
  if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

#else  /* no second screen here */

void second_init(void) {}
void second_apply(void) {}
void second_frame(void) {}
bool second_shown(void) { return false; }

#endif
