package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Method;

import qing.albatross.nativehook.AlbNative;
import qing.albatross.nativehook.DlInfo;
import qing.albatross.nativehook.HookRecord;
import qing.albatross.nativehook.InstructionCallback;
import qing.albatross.nativehook.NativeFunction;
import qing.albatross.common.Const;

/**
 * جسر استدعاء دوال native من UI.
 *
 * يدعم:
 *   - سرد مكتبات .so
 *   - سرد symbols في مكتبة (عبر DlInfo.enumerateFunctions)
 *   - البحث عن symbol في مكتبة
 *   - استدعاء دالة native بأي توقيع (via NativeFunction)
 *   - hook دالة native (via AlbNative.hookInstruction)
 *
 * آمن: كل عمليات try/catch مع ErrorBridge.report.
 */
public final class NativeCallBridge {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private NativeCallBridge() {}

    // ─── library listing ───
    public static String[] listLibraries() {
        final java.util.List<String> libs = new java.util.ArrayList<>();
        try {
            AlbNative.enumerateModules((symbol, addr, size, idx) -> {
                if (libs.size() < 5000) libs.add(symbol);
                return true;
            });
        } catch (Throwable t) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.listLibs", "", t);
        }
        return libs.toArray(new String[0]);
    }

    // ─── symbol enumeration (batched) ───
    public static String[] listSymbols(String libName, int offset, int limit) {
        final java.util.List<String> syms = new java.util.ArrayList<>();
        DlInfo info = null;
        try {
            info = AlbNative.openLib(libName);
            if (info == null) return new String[0];
            final int[] seen = {0};
            final int off = offset;
            final int lim = limit;
            info.enumerateFunctions((symbol, addr, size, idx) -> {
                if (seen[0] < off) { seen[0]++; return true; }
                if (syms.size() >= lim) return false;
                syms.add(symbol + "|" + Long.toHexString(addr));
                seen[0]++;
                return true;
            });
        } catch (Throwable t) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.listSymbols", libName, t);
        } finally {
            if (info != null) {
                try { info.close(); } catch (Throwable ignored) {}
            }
        }
        return syms.toArray(new String[0]);
    }

    // ─── symbol address ───
    public static long findSymbol(String libName, String symbol) {
        try {
            DlInfo info = AlbNative.openLib(libName);
            if (info == null) return 0;
            long addr = info.getSymbolAddress(symbol);
            info.close();
            return addr;
        } catch (Throwable t) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.findSymbol", libName + "!" + symbol, t);
            return 0;
        }
    }

    // ─── call native by signature ───
    /**
     * @param addr  symbol address
     * @param retType  Const.ARG_*
     * @param argTypes  array of Const.ARG_*
     * @param args  Java objects (Integer/Long/String/byte[]/Boolean)
     */
    public static Object call(long addr, byte retType, byte[] argTypes, Object[] args) {
        if (addr == 0) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.call", "", new IllegalArgumentException("addr=0"));
            return null;
        }
        try {
            NativeFunction fn = new NativeFunction(addr, retType, argTypes);
            return fn.call(args);
        } catch (Throwable t) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.call", "addr=0x" + Long.toHexString(addr), t);
            return null;
        }
    }

    // ─── hook a native function ───
    public static long hookInstruction(String lib, String function,
                                       final java.util.function.BiConsumer<Object, Object> onEnter,
                                       Object userdata) {
        try {
            InstructionCallback cb = (ctx, ud) -> {
                if (onEnter != null) onEnter.accept(ctx, ud);
            };
            HookRecord rec = AlbNative.hookInstruction(lib, function, cb, null, userdata);
            if (rec == null) return 0;
            // HookRecord لديه nativePtr داخلي — نستخدم HashCode للتعريف
            long id = (long) System.identityHashCode(rec);
            sHookRecords.put(id, rec);
            return id;
        } catch (Throwable t) {
            com.yambz.util.ErrorBridge.report(
                com.yambz.util.ErrorBridge.DOM_NATIVE,
                "native.hook", lib + "!" + function, t);
            return 0;
        }
    }

    private static final java.util.Map<Long, HookRecord> sHookRecords =
        new java.util.concurrent.ConcurrentHashMap<>();

    public static boolean unhook(long id) {
        HookRecord rec = sHookRecords.remove(id);
        if (rec == null) return false;
        try { return rec.unHook(); }
        catch (Throwable t) { return false; }
    }

    // ─── Const accessors (للـ C++) ───
    public static byte ARG_INT()    { return Const.ARG_INT; }
    public static byte ARG_LONG()   { return Const.ARG_LONG; }
    public static byte ARG_BOOL()   { return Const.ARG_BOOL; }
    public static byte ARG_STR()    { return Const.ARG_STR; }
    public static byte ARG_VOID()   { return Const.ARG_VOID; }
    public static byte ARG_BYTES()  { return Const.ARG_BYTES; }
    public static byte ARG_FLOAT()  { return Const.ARG_FLOAT; }
    public static byte ARG_DOUBLE() { return Const.ARG_DOUBLE; }
    public static byte ARG_PTR()    { return Const.ARG_PTR; }
}
