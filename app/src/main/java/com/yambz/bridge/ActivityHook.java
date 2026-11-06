package com.yambz.bridge;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.util.Log;

import com.yambz.ModBootstrap;

import java.lang.reflect.Method;

import qing.albatross.core.Albatross;

/**
 * ActivityHook — يلتقط أي Activity تظهر عبر Albatross.hookMethod (dynamic).
 *
 * لا mirror classes، لا annotations. فقط dynamic hooks على:
 * - Activity.onCreate
 * - Activity.onResume
 * - Activity.onPause
 * - Activity.onDestroy
 *
 * عند onResume → ModBootstrap.setCurrentActivity(a) + UIBridge.onActivityResumed(a)
 */
public final class ActivityHook {
    private static final String TAG = "YAMBZ";
    private static volatile boolean sInstalled = false;

    private ActivityHook() {}

    public static synchronized void install() throws Exception {
        if (sInstalled) return;
        sInstalled = true;

        hookActivity("onCreate", new Class<?>[]{ Bundle.class }, "onCreate");
        hookActivity("onResume", new Class<?>[]{}, "onResume");
        hookActivity("onPause",  new Class<?>[]{}, "onPause");
        hookActivity("onDestroy", new Class<?>[]{}, "onDestroy");

        Log.i(TAG, "ActivityHook: installed 4 hooks");
    }

    private static void hookActivity(String methodName, Class<?>[] params, String tag) throws Exception {
        Method m = Activity.class.getDeclaredMethod(methodName, params);
        Albatross.hookMethod(m, new qing.albatross.core.MethodHookCallback() {
            @Override
            public boolean beforeCall(qing.albatross.core.CallFrame frame) {
                return true;
            }

            @Override
            public void afterCall(qing.albatross.core.CallFrame frame) {
                try {
                    Object self = frame.getThis();
                    if (!(self instanceof Activity)) return;
                    Activity a = (Activity) self;

                    switch (tag) {
                        case "onCreate":
                            UIBridge.onActivityCreated(a);
                            break;
                        case "onResume":
                            ModBootstrap.setCurrentActivity(a);
                            UIBridge.onActivityResumed(a);
                            break;
                        case "onPause":
                            UIBridge.onActivityPaused(a);
                            break;
                        case "onDestroy":
                            UIBridge.onActivityDestroyed(a);
                            break;
                    }
                } catch (Throwable t) {
                    Log.e(TAG, "ActivityHook." + tag + " afterCall", t);
                }
            }
        });
    }
}
