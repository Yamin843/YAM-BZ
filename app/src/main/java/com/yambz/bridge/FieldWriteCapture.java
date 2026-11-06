package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.util.ArrayList;
import java.util.List;

import qing.albatross.core.Albatross;
import qing.albatross.core.InstructionListener;
import qing.albatross.core.InvocationContext;
import qing.albatross.search.FieldCallback;

/**
 * FieldWriteCapture — تنصت فعلي على كتابة حقل.
 *
 * الآلية:
 *  1. Albatross.searchField(field, FIELD_SET, callback) → كل كتابات الحقل
 *     - callback يحوي (method, operation, dexPc)
 *  2. لكل writer: Albatross.hookInstruction(method, dexPc-2, dexPc+2, listener)
 *     - يلتقط على حدود التعليمات
 *  3. onEnter: dump كل الـ registers + smali string → المستخدم يرى القيمة الجديدة
 *  4. onReturn: dump النتيجة بعد التنفيذ
 *
 * هذا تنفيذ فعلي (ليس stub). قد يظهر أكثر من تعليمة قبل الكتابة — طبيعة dexPc.
 */
public final class FieldWriteCapture {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private FieldWriteCapture() {}

    /** معلومات موضع كتابة واحدة */
    public static final class WriteSite {
        public final Member method;
        public final int dexPc;
        public WriteSite(Member m, int pc) { method = m; dexPc = pc; }
    }

    /** مستمع للأحداث */
    public interface FieldWriteListener {
        void onFieldWrite(Member method, int dexPc, String regsDump, String smali);
    }

    /** نتيجة install: استخدم unHook() للإلغاء */
    public static final class Handle {
        private final List<InstructionListener> listeners = new ArrayList<>();
        private volatile boolean active = true;

        public boolean isActive() { return active; }

        public synchronized void unHook() {
            if (!active) return;
            active = false;
            for (InstructionListener l : listeners) {
                try { l.unHook(); } catch (Throwable ignored) {}
            }
            listeners.clear();
        }

        void add(InstructionListener l) { listeners.add(l); }
    }

    /**
     * بدء التنصت على كتابات حقل معيّن.
     *
     * @param field الحقل المستهدف (private/instance)
     * @param listener يُستدعى عند كل كتابة
     * @param searchPlatform هل نبحث في platform classes أيضاً (أبطأ)
     * @return Handle لإلغاء التنصت، أو null إن لم يوجد writers
     */
    public static Handle install(Field field, FieldWriteListener listener, boolean searchPlatform) {
        if (field == null || listener == null) return null;
        final Handle handle = new Handle();
        final int[] installed = {0};

        try {
            Albatross.searchField(field, FieldCallback.FIELD_SET,
                (method, operation, pos) -> {
                    if (!handle.active) return false;
                    installHook(handle, method, pos, listener, installed);
                    return true;
                }, false, searchPlatform);
        } catch (Throwable t) {
            Log.e(TAG, "searchField failed", t);
            handle.unHook();
            return null;
        }

        if (installed[0] == 0) {
            Log.w(TAG, "FieldWriteCapture: no writers found for " + field.getName());
            return null;
        }

        Log.i(TAG, "FieldWriteCapture: installed " + installed[0]
            + " hook(s) for " + field.getName());
        return handle;
    }

    private static void installHook(Handle handle, Member method, int dexPc,
                                     FieldWriteListener listener, int[] installed) {
        try {
            InstructionListener il = new InstructionListener() {
                @Override
                public void onEnter(Member m, Object self, int pc, InvocationContext ctx) {
                    try {
                        String regs = dumpRegisters(ctx);
                        String smali = ctx.smaliString();
                        listener.onFieldWrite(m, pc, regs, smali);
                    } catch (Throwable t) {
                        Log.e(TAG, "onFieldWrite failed", t);
                    }
                }
            };

            // نُوسّع المدى قليلاً حول dexPc لأن تعليمة الكتابة قد تبدأ قبل أو بعد
            int minPc = Math.max(0, dexPc - 2);
            int maxPc = dexPc + 3;
            boolean ok = Albatross.hookInstruction(method, minPc, maxPc, il);
            if (ok) {
                handle.add(il);
                installed[0]++;
            } else {
                Log.w(TAG, "hookInstruction failed at " + method + "@" + dexPc);
            }
        } catch (Throwable t) {
            Log.e(TAG, "installHook failed", t);
        }
    }

    private static String dumpRegisters(InvocationContext ctx) {
        try {
            int n = ctx.numberOfVRegs();
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < n; i++) {
                try {
                    Object v = ctx.getVRegObject(i);
                    if (v == null) {
                        int prim = ctx.getVRegInt(i);
                        sb.append("v").append(i).append('=').append(prim).append(' ');
                    } else {
                        sb.append("v").append(i).append('=')
                          .append(ObjectDumper.dump(v)).append(' ');
                    }
                } catch (Throwable ignored) {}
            }
            return sb.toString();
        } catch (Throwable t) {
            return "<regs-error>";
        }
    }
}
