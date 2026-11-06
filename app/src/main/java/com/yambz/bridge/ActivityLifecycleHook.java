package com.yambz.bridge;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.util.Log;

import qing.albatross.annotation.MethodHookBackup;
import qing.albatross.annotation.TargetClass;
import qing.albatross.core.Albatross;
import qing.albatross.exception.AlbatrossErr;

/**
 * يكتشف أي Activity تظهر — بدون انتظار.
 *
 * استراتيجية مزدوجة:
 *   1. hook على Activity.onCreate/onResume/onPause/onDestroy (مضمون)
 *   2. registerActivityLifecycleCallbacks في Application.onCreate (أسرع)
 *
 * الفائدة: لو حدث خطأ في (2)، (1) يغطي. ولا يعتمد على أي شيء خارجي.
 */
final class ActivityLifecycleHook {
    private static final String TAG = "YAMBZ";
    private static boolean sInstalled = false;

    private ActivityLifecycleHook() {}

    static void install() throws AlbatrossErr {
        if (sInstalled) return;
        sInstalled = true;

        Albatross.hookClass(ActivityHook.class);
        Albatross.hookClass(ApplicationHook.class);
        Log.i(TAG, "Lifecycle hooks installed");
    }

    @TargetClass(Activity.class)
    static class ActivityHook {
        @MethodHookBackup
        private void onCreate(Bundle savedInstanceState) {
            onCreate(savedInstanceState);
            ModManager.onActivityCreated(this);
        }

        @MethodHookBackup
        private void onResume() {
            onResume();
            ModManager.onActivityResumed(this);
        }

        @MethodHookBackup
        private void onPause() {
            ModManager.onActivityPaused(this);
            onPause();
        }

        @MethodHookBackup
        private void onDestroy() {
            ModManager.onActivityDestroyed(this);
            onDestroy();
        }
    }

    @TargetClass(Application.class)
    static class ApplicationHook {
        @MethodHookBackup
        private void onCreate() {
            onCreate();
            try {
                Application app = Albatross.currentApplication();
                if (app == null) app = (Application) this;
                app.registerActivityLifecycleCallbacks(new AppLifecycleBridge());
                Log.i(TAG, "ActivityLifecycleCallbacks registered");
            } catch (Throwable t) {
                Log.w(TAG, "registerActivityLifecycleCallbacks failed (fallback to hooks)", t);
            }
        }
    }

    static final class AppLifecycleBridge implements Application.ActivityLifecycleCallbacks {
        @Override public void onActivityCreated(Activity a, Bundle b) { ModManager.onActivityCreated(a); }
        @Override public void onActivityStarted(Activity a) {}
        @Override public void onActivityResumed(Activity a) { ModManager.onActivityResumed(a); }
        @Override public void onActivityPaused(Activity a) { ModManager.onActivityPaused(a); }
        @Override public void onActivityStopped(Activity a) {}
        @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
        @Override public void onActivityDestroyed(Activity a) { ModManager.onActivityDestroyed(a); }
    }
}
