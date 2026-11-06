package com.yambz.bridge;

import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.FileReader;
import java.io.BufferedReader;
import java.util.ArrayList;
import java.util.List;

/**
 * PresetBridge — save/load/list/delete Call presets.
 *
 * Storage: <cacheDir>/presets/<name>.json
 * Format:
 *   {"spec":"com.Foo#bar(int)","args":"[...]","label":"..."}
 */
public final class PresetBridge {
    private static final String TAG = "YAMBZ";

    private PresetBridge() {}

    private static File presetDir() {
        File dir = new File(UIBridge.getCacheDir(), "presets");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    // ══════════════════════════════════════════════════════════
    //  Save
    // ══════════════════════════════════════════════════════════
    public static boolean save(String name, String spec, String args) {
        if (name == null || name.isEmpty()) return false;
        try {
            File f = new File(presetDir(), sanitize(name) + ".json");
            FileWriter w = new FileWriter(f);
            w.write("{\"name\":");
            jsonStr(w, name);
            w.write(",\"spec\":");
            jsonStr(w, spec);
            w.write(",\"args\":");
            jsonStr(w, args);
            w.write("}");
            w.close();
            Log.i(TAG, "Preset saved: " + name);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "save preset failed", t);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  List
    // ══════════════════════════════════════════════════════════
    public static String[] list() {
        List<String> out = new ArrayList<>();
        File[] files = presetDir().listFiles();
        if (files == null) return new String[0];
        for (File f : files) {
            if (!f.getName().endsWith(".json")) continue;
            try {
                String content = readFile(f);
                String name = extractField(content, "name");
                String spec = extractField(content, "spec");
                out.add(name + "|" + spec);
            } catch (Throwable t) {
                Log.e(TAG, "read preset failed", t);
            }
        }
        return out.toArray(new String[0]);
    }

    // ══════════════════════════════════════════════════════════
    //  Load — returns "spec|args" or "" if not found
    // ══════════════════════════════════════════════════════════
    public static String load(String name) {
        try {
            File f = new File(presetDir(), sanitize(name) + ".json");
            if (!f.exists()) return "";
            String content = readFile(f);
            String spec = extractField(content, "spec");
            String args = extractField(content, "args");
            return spec + "|" + args;
        } catch (Throwable t) {
            Log.e(TAG, "load preset failed", t);
            return "";
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Delete
    // ══════════════════════════════════════════════════════════
    public static boolean delete(String name) {
        try {
            File f = new File(presetDir(), sanitize(name) + ".json");
            return f.delete();
        } catch (Throwable t) {
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Helpers
    // ══════════════════════════════════════════════════════════
    private static String sanitize(String name) {
        StringBuilder sb = new StringBuilder();
        for (char c : name.toCharArray()) {
            if (Character.isLetterOrDigit(c) || c == '_' || c == '-' || c == '.')
                sb.append(c);
            else sb.append('_');
        }
        String s = sb.toString();
        return s.length() > 80 ? s.substring(0, 80) : s;
    }

    private static String readFile(File f) throws Exception {
        StringBuilder sb = new StringBuilder();
        BufferedReader r = new BufferedReader(new FileReader(f));
        String line;
        while ((line = r.readLine()) != null) sb.append(line);
        r.close();
        return sb.toString();
    }

    private static void jsonStr(FileWriter w, String s) throws Exception {
        w.write('"');
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"':  w.write("\\\""); break;
                case '\\': w.write("\\\\"); break;
                case '\n': w.write("\\n"); break;
                case '\r': w.write("\\r"); break;
                case '\t': w.write("\\t"); break;
                default: w.write(c);
            }
        }
        w.write('"');
    }

    private static String extractField(String json, String field) {
        String key = "\"" + field + "\":\"";
        int i = json.indexOf(key);
        if (i < 0) return "";
        int s = i + key.length();
        int e = s;
        StringBuilder sb = new StringBuilder();
        while (e < json.length()) {
            char c = json.charAt(e);
            if (c == '\\' && e + 1 < json.length()) {
                char n = json.charAt(e + 1);
                switch (n) {
                    case '"':  sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case 'n':  sb.append('\n'); break;
                    case 'r':  sb.append('\r'); break;
                    case 't':  sb.append('\t'); break;
                    default: sb.append(n);
                }
                e += 2;
                continue;
            }
            if (c == '"') break;
            sb.append(c);
            e++;
        }
        return sb.toString();
    }
}
