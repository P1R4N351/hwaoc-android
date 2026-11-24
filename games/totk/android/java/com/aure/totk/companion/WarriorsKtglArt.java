// SPDX-License-Identifier: MIT
package com.aure.totk.companion;

import android.content.Context;
import android.graphics.Bitmap;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * Pure-Java BYOG KTGL reader for TOTKRFS1 (runtime-index.bin + romfs.bin) and ScreenLayout.rdb*.
 * Uses windowed IDRK0000 chunk decompression (<= 2 MB heap overhead), Tegra GOB deswizzle for
 * 0x60/0x62 (linear for 0x59/0x5b/RGBA8), decodes BC1/BC3/RGBA8, and crops by KSS UVs.
 */
public final class WarriorsKtglArt {
    public static final String SRC_TAG = "KTGL_PAD_V2";
    private static final String TAG = "WarriorsKtglArt";
    private static final int TYPE_GT1G = 0xafbec60c, TYPE_KSS = 0xf20de437;
    private static final int MAX_IDX = 16384, MAX_RDB = 8192, MAX_CHUNKS = 32768;
    private static final int MAX_SPRITES = 2048, MAX_WIN = 2 * 1024 * 1024;
    private static final int MODE_RAW = 0, MODE_BACKDROP = 1, MODE_LUMA_ALPHA = 2;

    private static final class Source {
        File romfsBin;
        long rdbOff, rdbSize;
        final long[] binOffs = new long[4], binSizes = new long[4];
        File rdbFile;
        final File[] binFiles = new File[4];
    }

    private static final class RdbTable {
        final int[] fids = new int[MAX_RDB], types = new int[MAX_RDB], cszs = new int[MAX_RDB];
        final byte[] bins = new byte[MAX_RDB];
        final long[] offs = new long[MAX_RDB];
        int count;
    }

    private static final class SpriteRef {
        int texIdx, fmt, w, h, dataOff;
        float u0, v0, u1, v1;
    }

    private WarriorsKtglArt() { }

    public static boolean harvest(Context ctx, File[] roots, File outDir) {
        if (outDir == null || (!outDir.isDirectory() && !outDir.mkdirs())) return false;
        Source src = findSource(ctx, roots);
        if (src == null) return false;
        RdbTable tbl = loadRdbTable(src);
        if (tbl == null || tbl.count <= 0) return false;
        boolean ok = extractToFile(src, tbl, 0x0e415987, "CmnTexWallBoxPartsFiberB_00",
                MODE_BACKDROP, new File(outDir, "backdrop.png"))
                || extractToFile(src, tbl, 0x7e1eb920, "main_parts_gra",
                MODE_RAW, new File(outDir, "backdrop.png"));
        ok &= extractToFile(src, tbl, 0xe50b4ec0, "gra_fade", MODE_RAW, new File(outDir, "filter.png"));
        ok &= extractToFile(src, tbl, 0x0144b0c6, "IconSheikah_20CSlim", MODE_LUMA_ALPHA,
                new File(outDir, "glyph.png"));
        ok &= extractToFile(src, tbl, 0x0e415987, "main_parts_txt_sheikah", MODE_RAW,
                new File(outDir, "border_frieze.png"));
        ok &= extractToFile(src, tbl, 0x0e415987, "main_frame_5", MODE_RAW, new File(outDir, "border_edge.png"));
        ok &= extractToFile(src, tbl, 0x0e415987, "main_bg_line", MODE_RAW, new File(outDir, "border_side.png"));
        return ok && writeText(new File(outDir, "source.txt"), SRC_TAG);
    }

    private static Source findSource(Context ctx, File[] roots) {
        File[] bases = buildBases(ctx, roots);
        String[] subs = {"", "romfs", "extracted/hwaoc130/romfs", "hwaoc130/romfs"};
        for (File base : bases) {
            if (base == null) continue;
            for (String sub : subs) {
                File dir = sub.isEmpty() ? base : new File(base, sub);
                Source s = tryOpenDir(dir);
                if (s != null) return s;
            }
        }
        return null;
    }

    private static File[] buildBases(Context ctx, File[] roots) {
        String env = System.getenv("TOTK_DATA_DIR");
        int n = roots != null ? roots.length : 0;
        File[] out = new File[n + 3];
        out[0] = (env != null && !env.isEmpty()) ? new File(env) : null;
        out[1] = ctx != null ? ctx.getExternalFilesDir(null) : null;
        out[2] = ctx != null ? ctx.getFilesDir() : null;
        for (int i = 0; i < n; i++) out[3 + i] = roots[i];
        return out;
    }

    private static Source tryOpenDir(File dir) {
        if (!dir.isDirectory()) return null;
        File idx = new File(dir, "runtime-index.bin");
        File rfs = new File(dir, "romfs.bin");
        if (idx.isFile() && rfs.isFile()) return parseRuntimeIndex(idx, rfs);
        File rdb = new File(dir, "asset/ScreenLayout.rdb");
        File b0 = new File(dir, "asset/ScreenLayout.rdb.bin");
        if (!rdb.isFile() || !b0.isFile()) return null;
        Source s = new Source();
        s.rdbFile = rdb;
        s.rdbSize = rdb.length();
        for (int i = 0; i < 4; i++) {
            File f = new File(dir, i == 0 ? "asset/ScreenLayout.rdb.bin" : ("asset/ScreenLayout.rdb.bin" + i));
            if (f.isFile()) { s.binFiles[i] = f; s.binSizes[i] = f.length(); }
        }
        return s;
    }

    private static Source parseRuntimeIndex(File idxFile, File romfsBin) {
        byte[] buf = readSmallFile(idxFile, 4 * 1024 * 1024);
        if (buf == null || buf.length < 24 || !matchAscii(buf, 0, "TOTKRFS1")) return null;
        long count = le64(buf, 8), strBase = le64(buf, 16);
        if (count <= 0 || count > MAX_IDX || strBase < 24L + count * 24L || strBase > buf.length) return null;
        Source s = new Source();
        s.romfsBin = romfsBin;
        for (int i = 0; i < (int) count; i++) {
            int row = 24 + i * 24;
            int soff = le32(buf, row + 16), slen = le32(buf, row + 20);
            if (slen <= 0 || strBase + soff + slen > buf.length) continue;
            int nlen = (buf[(int) strBase + soff + slen - 1] == 0) ? (slen - 1) : slen;
            String name = new String(buf, (int) strBase + soff, nlen, StandardCharsets.UTF_8);
            assignRfsEntry(s, name, le64(buf, row), le64(buf, row + 8));
        }
        return (s.rdbSize > 0 && s.binSizes[0] > 0) ? s : null;
    }

    private static void assignRfsEntry(Source s, String name, long off, long sz) {
        if ("asset/ScreenLayout.rdb".equals(name)) { s.rdbOff = off; s.rdbSize = sz; }
        else if ("asset/ScreenLayout.rdb.bin".equals(name)) { s.binOffs[0] = off; s.binSizes[0] = sz; }
        else if ("asset/ScreenLayout.rdb.bin1".equals(name)) { s.binOffs[1] = off; s.binSizes[1] = sz; }
        else if ("asset/ScreenLayout.rdb.bin2".equals(name)) { s.binOffs[2] = off; s.binSizes[2] = sz; }
        else if ("asset/ScreenLayout.rdb.bin3".equals(name)) { s.binOffs[3] = off; s.binSizes[3] = sz; }
    }

    private static RdbTable loadRdbTable(Source src) {
        if (src.rdbSize <= 32 || src.rdbSize > 4 * 1024 * 1024) return null;
        byte[] rdb = new byte[(int) src.rdbSize];
        File f = src.romfsBin != null ? src.romfsBin : src.rdbFile;
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            raf.seek(src.romfsBin != null ? src.rdbOff : 0L);
            if (!readFully(raf, rdb, rdb.length)) return null;
        } catch (IOException e) {
            Log.w(TAG, "rdb read failed", e);
            return null;
        }
        if (!matchAscii(rdb, 0, "_DRK0000")) return null;
        return parseRdbEntries(rdb);
    }

    private static RdbTable parseRdbEntries(byte[] rdb) {
        RdbTable tbl = new RdbTable();
        int pos = le32(rdb, 8);
        for (int i = 0; i < MAX_RDB && pos >= 32 && pos + 48 <= rdb.length; i++) {
            if (!matchAscii(rdb, pos, "IDRK0000")) break;
            long eszL = le64(rdb, pos + 8);
            if (eszL < 48 || pos + eszL > rdb.length) break;
            int esz = (int) eszL;
            if (esz > 48 && parseLocTail(rdb, pos + 48, pos + esz, tbl, tbl.count)) {
                tbl.fids[tbl.count] = le32(rdb, pos + 36);
                tbl.types[tbl.count] = le32(rdb, pos + 40);
                tbl.count++;
            }
            pos = (pos + esz + 3) & ~3;
        }
        return tbl;
    }

    private static boolean parseLocTail(byte[] rdb, int s, int e, RdbTable tbl, int idx) {
        int at = -1;
        for (int i = s; i < e; i++) if (rdb[i] == '@') at = i;
        if (at <= s) return false;
        int start = at;
        for (int i = at - 1; i >= s; i--) {
            if (rdb[i] == 0) break;
            start = i;
        }
        int end = at + 1;
        for (int i = at + 1; i < e && rdb[i] != 0; i++) end = i + 1;
        String loc = new String(rdb, start, end - start, StandardCharsets.US_ASCII);
        if (loc.indexOf('&') >= 0) return false;
        return storeLocFields(loc, tbl, idx);
    }

    private static boolean storeLocFields(String loc, RdbTable tbl, int idx) {
        int at = loc.indexOf('@'), hash = loc.indexOf('#');
        try {
            long off = Long.parseLong(loc.substring(0, at), 16);
            int csz = Integer.parseInt(hash >= 0 ? loc.substring(at + 1, hash) : loc.substring(at + 1), 16);
            int bin = hash >= 0 ? Integer.parseInt(loc.substring(hash + 1)) : 0;
            if (bin < 0 || bin >= 4 || csz < 48) return false;
            tbl.offs[idx] = off;
            tbl.cszs[idx] = csz;
            tbl.bins[idx] = (byte) bin;
            return true;
        } catch (NumberFormatException ex) {
            return false;
        }
    }

    private static boolean extractToFile(
        Source src, RdbTable tbl, int kssFid, String spriteName, int mode, File dst) {
        int kIdx = findEntry(tbl, TYPE_KSS, kssFid);
        if (kIdx < 0) return false;
        int gIdx = findPrecedingGt1g(tbl, kIdx);
        if (gIdx < 0) return false;
        SpriteRef sp = findSpriteInKss(src, tbl, kIdx, spriteName);
        if (sp == null || !readGt1gSubheader(src, tbl, gIdx, sp)) return false;
        Bitmap bmp = decodeSprite(src, tbl, gIdx, sp, mode);
        if (bmp == null) return false;
        boolean saved = savePng(dst, bmp);
        bmp.recycle();
        return saved;
    }

    private static int findEntry(RdbTable tbl, int type, int fid) {
        for (int i = 0; i < tbl.count; i++) {
            if (tbl.types[i] == type && tbl.fids[i] == fid) return i;
        }
        return -1;
    }

    private static int findPrecedingGt1g(RdbTable tbl, int kIdx) {
        byte bin = tbl.bins[kIdx];
        long kOff = tbl.offs[kIdx], bestOff = -1L;
        int best = -1;
        for (int i = 0; i < tbl.count; i++) {
            if (tbl.types[i] == TYPE_GT1G && tbl.bins[i] == bin && tbl.offs[i] < kOff && tbl.offs[i] > bestOff) {
                bestOff = tbl.offs[i];
                best = i;
            }
        }
        return best;
    }

    private static SpriteRef findSpriteInKss(Source src, RdbTable tbl, int kIdx, String name) {
        byte[] kss = readIdrkWindow(src, tbl, kIdx, 0, 262144, true);
        if (kss == null || kss.length < 4) return null;
        int lim = Math.min(le32(kss, 0), MAX_SPRITES);
        for (int i = 0; i < lim; i++) {
            int base = 4 + i * 104;
            if (base + 100 > kss.length) break;
            if (matchCstr(kss, base, 64, name)) return parseSpriteRef(kss, base);
        }
        return null;
    }

    private static SpriteRef parseSpriteRef(byte[] kss, int base) {
        SpriteRef sp = new SpriteRef();
        sp.texIdx = le32(kss, base + 64);
        float minU = 1f, minV = 1f, maxU = 0f, maxV = 0f;
        for (int i = 0; i < 4; i++) {
            float u = Float.intBitsToFloat(le32(kss, base + 68 + i * 8));
            float v = Float.intBitsToFloat(le32(kss, base + 72 + i * 8));
            if (u < minU) minU = u;
            if (u > maxU) maxU = u;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        sp.u0 = minU; sp.v0 = minV; sp.u1 = maxU; sp.v1 = maxV;
        return sp;
    }

    private static boolean readGt1gSubheader(Source src, RdbTable tbl, int gIdx, SpriteRef sp) {
        byte[] hdr = readIdrkWindow(src, tbl, gIdx, 0, 256, false);
        if (hdr == null || hdr.length < 24 || !matchAscii(hdr, 0, "GT1G0600")) return false;
        int hdrSz = le32(hdr, 12), texCnt = le32(hdr, 16);
        if (sp.texIdx < 0 || sp.texIdx >= texCnt || hdrSz + texCnt * 4 > 4096) return false;
        if (hdrSz + texCnt * 4 > hdr.length) {
            hdr = readIdrkWindow(src, tbl, gIdx, 0, hdrSz + texCnt * 4, false);
            if (hdr == null) return false;
        }
        int subBase = hdrSz + le32(hdr, hdrSz + sp.texIdx * 4);
        byte[] sub = readIdrkWindow(src, tbl, gIdx, subBase, subBase + 20, false);
        if (sub == null || sub.length < 20) return false;
        sp.fmt = sub[1] & 0xFF;
        int dim = sub[2] & 0xFF;
        int ext = (sub[7] & 0xFF) != 0 ? (8 + le32(sub, 8)) : 8;
        if (ext < 8 || ext > 64) ext = 20;
        sp.w = 1 << (dim & 0xF);
        sp.h = 1 << ((dim >> 4) & 0xF);
        sp.dataOff = subBase + ext;
        return sp.w > 0 && sp.h > 0 && sp.w <= 4096 && sp.h <= 4096;
    }

    private static Bitmap decodeSprite(Source src, RdbTable tbl, int gIdx, SpriteRef sp, int mode) {
        int bw = isRgbaFmt(sp.fmt) ? 1 : 4, bpp = fmtBpp(sp.fmt);
        if (bpp <= 0) return null;
        int x0 = clamp(Math.round(sp.u0 * sp.w), 0, sp.w), x1 = clamp(Math.round(sp.u1 * sp.w), 0, sp.w);
        int y0 = clamp(Math.round(sp.v0 * sp.h), 0, sp.h), y1 = clamp(Math.round(sp.v1 * sp.h), 0, sp.h);
        if (x1 <= x0 || y1 <= y0) return null;
        int wb = (sp.w + bw - 1) / bw, hb = (sp.h + bw - 1) / bw, rowBytes = wb * bpp;
        boolean swz = (sp.fmt == 0x60 || sp.fmt == 0x62);
        int by0 = swz ? 0 : (y0 / bw), by1 = swz ? hb : ((y1 + bw - 1) / bw);
        byte[] raw = readIdrkWindow(src, tbl, gIdx, sp.dataOff + by0 * rowBytes, sp.dataOff + by1 * rowBytes, false);
        if (raw == null) return null;
        if (swz) raw = deswizzle(raw, wb, hb, bpp);
        if (raw == null) return null;
        return decodeCrop(raw, sp.fmt, sp.w, x0, y0, x1, y1, by0 * bw, mode);
    }

    private static Bitmap decodeCrop(
        byte[] src, int fmt, int texW, int x0, int y0, int x1, int y1, int baseY, int mode) {
        int cw = x1 - x0, ch = y1 - y0;
        int[] out = new int[cw * ch];
        if (isRgbaFmt(fmt)) decodeRgba(src, texW, x0, y0, x1, y1, baseY, out);
        else if (fmt == 0x59 || fmt == 0x60) decodeBc1(src, texW, x0, y0, x1, y1, baseY, out);
        else if (fmt == 0x5b || fmt == 0x62) decodeBc3(src, texW, x0, y0, x1, y1, baseY, out);
        else return null;
        applyMode(out, mode);
        return Bitmap.createBitmap(out, cw, ch, Bitmap.Config.ARGB_8888);
    }

    private static void applyMode(int[] px, int mode) {
        if (mode == MODE_RAW) return;
        for (int i = 0; i < px.length; i++) {
            int c = px[i], r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            if (mode == MODE_BACKDROP) {
                px[i] = (0xFF << 24) | ((r * 42 / 255) << 16) | ((g * 50 / 255) << 8) | (b * 52 / 255);
            } else if (mode == MODE_LUMA_ALPHA) {
                int a = Math.max(r, Math.max(g, b));
                px[i] = (a << 24) | 0x00FFFFFF;
            }
        }
    }

    private static void decodeRgba(byte[] src, int texW, int x0, int y0, int x1, int y1, int baseY, int[] out) {
        int cw = x1 - x0;
        for (int y = y0; y < y1; y++) {
            int sRow = (y - baseY) * texW * 4, dRow = (y - y0) * cw;
            for (int x = x0; x < x1; x++) {
                int s = sRow + x * 4;
                if (s + 4 <= src.length) {
                    out[dRow + (x - x0)] = ((src[s + 3] & 0xFF) << 24) | ((src[s] & 0xFF) << 16)
                            | ((src[s + 1] & 0xFF) << 8) | (src[s + 2] & 0xFF);
                }
            }
        }
    }

    private static void decodeBc1(byte[] src, int texW, int x0, int y0, int x1, int y1, int baseY, int[] out) {
        int wb = (texW + 3) / 4, bx0 = x0 / 4, bx1 = (x1 + 3) / 4;
        int by0 = (y0 - baseY) / 4, by1 = (y1 - baseY + 3) / 4;
        int[] cols = new int[4];
        for (int by = by0; by < by1; by++) {
            for (int bx = bx0; bx < bx1; bx++) {
                int off = (by * wb + bx) * 8;
                if (off + 8 > src.length) continue;
                bc1Colors(src, off, cols, true);
                blitBlock(bx * 4, baseY + by * 4, x0, y0, x1, y1, le32(src, off + 4), cols, null, out, 0L);
            }
        }
    }

    private static void decodeBc3(byte[] src, int texW, int x0, int y0, int x1, int y1, int baseY, int[] out) {
        int wb = (texW + 3) / 4, bx0 = x0 / 4, bx1 = (x1 + 3) / 4;
        int by0 = (y0 - baseY) / 4, by1 = (y1 - baseY + 3) / 4;
        int[] cols = new int[4], alphas = new int[8];
        for (int by = by0; by < by1; by++) {
            for (int bx = bx0; bx < bx1; bx++) {
                int off = (by * wb + bx) * 16;
                if (off + 16 > src.length) continue;
                bc3Alphas(src[off] & 0xFF, src[off + 1] & 0xFF, alphas);
                bc1Colors(src, off + 8, cols, false);
                long ad = le48(src, off + 2);
                blitBlock(bx * 4, baseY + by * 4, x0, y0, x1, y1, le32(src, off + 12), cols, alphas, out, ad);
            }
        }
    }

    private static void bc1Colors(byte[] d, int off, int[] cols, boolean alphaZero) {
        int q0 = le16(d, off), q1 = le16(d, off + 2);
        int r0 = ((q0 >> 8) & 0xF8) | (q0 >> 13), g0 = ((q0 >> 3) & 0xFC) | ((q0 >> 9) & 3);
        int b0 = ((q0 << 3) | ((q0 >> 2) & 7)) & 0xFF;
        int r1 = ((q1 >> 8) & 0xF8) | (q1 >> 13), g1 = ((q1 >> 3) & 0xFC) | ((q1 >> 9) & 3);
        int b1 = ((q1 << 3) | ((q1 >> 2) & 7)) & 0xFF;
        cols[0] = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;
        cols[1] = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;
        if (q0 > q1 || !alphaZero) {
            cols[2] = (0xFF << 24) | (((r0 * 2 + r1) / 3) << 16) | (((g0 * 2 + g1) / 3) << 8) | ((b0 * 2 + b1) / 3);
            cols[3] = (0xFF << 24) | (((r0 + r1 * 2) / 3) << 16) | (((g0 + g1 * 2) / 3) << 8) | ((b0 + b1 * 2) / 3);
        } else {
            cols[2] = (0xFF << 24) | (((r0 + r1) / 2) << 16) | (((g0 + g1) / 2) << 8) | ((b0 + b1) / 2);
            cols[3] = 0;
        }
    }

    private static void bc3Alphas(int a0, int a1, int[] a) {
        a[0] = a0; a[1] = a1;
        if (a0 > a1) {
            for (int k = 1; k <= 6; k++) a[1 + k] = (a0 * (7 - k) + a1 * k) / 7;
        } else {
            for (int k = 1; k <= 4; k++) a[1 + k] = (a0 * (5 - k) + a1 * k) / 5;
            a[6] = 0; a[7] = 255;
        }
    }

    private static void blitBlock(
        int px0, int py0, int x0, int y0, int x1, int y1, int idx, int[] cols, int[] alphas, int[] out, long ad) {
        int cw = x1 - x0;
        for (int i = 0; i < 16; i++) {
            int px = px0 + (i & 3), py = py0 + (i >> 2);
            if (px >= x0 && px < x1 && py >= y0 && py < y1) {
                int c = cols[(idx >>> (i * 2)) & 3];
                if (alphas != null) {
                    int a = alphas[(int) ((ad >>> (i * 3)) & 7L)];
                    c = (a << 24) | (c & 0x00FFFFFF);
                }
                out[(py - y0) * cw + (px - x0)] = c;
            }
        }
    }

    private static byte[] deswizzle(byte[] data, int wb, int hb, int bpp) {
        int bh = 16;
        for (int s = 0; s < 5 && bh > 1 && (hb + 7) / 8 <= bh / 2; s++) bh /= 2;
        int gobs = (wb * bpp + 63) / 64, row = wb * bpp;
        if (row * hb > MAX_WIN) return null;
        byte[] out = new byte[row * hb];
        for (int y = 0; y < hb; y++) {
            int gy = (y / (8 * bh)) * 512 * bh * gobs + ((y % (8 * bh)) / 8) * 512;
            int yp = ((y % 8) / 2) * 64 + (y % 2) * 16;
            int dRow = y * row;
            for (int x = 0; x < row; x += bpp) {
                int src = gy + (x / 64) * 512 * bh + ((x % 64) / 32) * 256 + yp + ((x % 32) / 16) * 32 + (x % 16);
                if (src + bpp <= data.length) System.arraycopy(data, src, out, dRow + x, bpp);
            }
        }
        return out;
    }

    private static byte[] readIdrkWindow(
        Source src, RdbTable tbl, int idx, int winStart, int winEnd, boolean clampToFsize) {
        if (winStart < 0 || winEnd <= winStart || (winEnd - winStart) > MAX_WIN) return null;
        int bin = tbl.bins[idx] & 0xFF;
        File f = src.romfsBin != null ? src.romfsBin : src.binFiles[bin];
        if (f == null || !f.isFile()) return null;
        long absOff = (src.romfsBin != null ? src.binOffs[bin] : 0L) + tbl.offs[idx];
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            return readIdrkFromRaf(raf, absOff, tbl.cszs[idx], winStart, winEnd, clampToFsize);
        } catch (IOException e) {
            Log.w(TAG, "idrk window read failed", e);
            return null;
        }
    }

    private static byte[] readIdrkFromRaf(
        RandomAccessFile raf, long absOff, int maxSz, int winStart, int winEnd, boolean clampToFsize)
        throws IOException {
        byte[] hdr = new byte[48];
        raf.seek(absOff);
        if (!readFully(raf, hdr, 48) || !matchAscii(hdr, 0, "IDRK0000")) return null;
        long esz = le64(hdr, 8), csz = le64(hdr, 16), fsize = le64(hdr, 24);
        int rflags = le32(hdr, 44);
        if (esz < 48 || esz > maxSz || csz > esz) return null;
        int end = clampToFsize ? (int) Math.min((long) winEnd, fsize) : winEnd;
        if (end <= winStart || (end - winStart) > MAX_WIN) return null;
        long streamOff = absOff + (esz - csz);
        if ((rflags & 0x100000) == 0) {
            if (end > csz) return null;
            byte[] out = new byte[end - winStart];
            raf.seek(streamOff + winStart);
            return readFully(raf, out, out.length) ? out : null;
        }
        return decompressWindow(raf, streamOff, (int) csz, winStart, end);
    }

    private static byte[] decompressWindow(
        RandomAccessFile raf, long streamOff, int csz, int winStart, int winEnd) throws IOException {
        byte[] out = new byte[winEnd - winStart];
        byte[] cbuf = new byte[65536], dbuf = new byte[32768], len4 = new byte[4];
        Inflater inf = new Inflater();
        int streamPos = 0, rawPos = 0;
        raf.seek(streamOff);
        try {
            for (int c = 0; c < MAX_CHUNKS && streamPos + 4 <= csz && rawPos < winEnd; c++) {
                if (!readFully(raf, len4, 4)) return null;
                int rawLen = le32(len4, 0), cl = rawLen & 0x7FFFFFFF;
                if (cl <= 0 || cl > cbuf.length || streamPos + 4 + cl > csz) break;
                if (!readFully(raf, cbuf, cl)) return null;
                streamPos += 4 + cl;
                if ((rawLen & 0x80000000) != 0 || !isZlibHdr(cbuf, cl)) {
                    copyOverlap(cbuf, cl, rawPos, winStart, winEnd, out);
                    rawPos += cl;
                } else {
                    rawPos = inflateChunk(inf, cbuf, cl, dbuf, rawPos, winStart, winEnd, out);
                    if (rawPos < 0) return null;
                }
            }
        } finally {
            inf.end();
        }
        return rawPos >= winEnd ? out : null;
    }

    private static boolean isZlibHdr(byte[] b, int cl) {
        if (cl < 2 || cl > b.length) return false;
        int b0 = b[0] & 0xFF, b1 = b[1] & 0xFF;
        return (b0 & 0x0F) == 8 && (((b0 << 8) | b1) % 31) == 0;
    }

    private static int inflateChunk(
        Inflater inf, byte[] cbuf, int cl, byte[] dbuf, int rawPos, int winStart, int winEnd, byte[] out) {
        inf.reset();
        inf.setInput(cbuf, 0, cl);
        try {
            for (int i = 0; i < 64 && !inf.finished(); i++) {
                int n = inf.inflate(dbuf, 0, dbuf.length);
                if (n > 0) {
                    copyOverlap(dbuf, n, rawPos, winStart, winEnd, out);
                    rawPos += n;
                    if (rawPos >= winEnd) break;
                } else if (inf.needsInput() || inf.needsDictionary()) {
                    break;
                }
            }
            return rawPos;
        } catch (DataFormatException e) {
            return -1;
        }
    }

    private static void copyOverlap(byte[] src, int len, int rawPos, int winStart, int winEnd, byte[] out) {
        int s0 = Math.max(rawPos, winStart), s1 = Math.min(rawPos + len, winEnd);
        if (s1 > s0) System.arraycopy(src, s0 - rawPos, out, s0 - winStart, s1 - s0);
    }

    private static boolean savePng(File dst, Bitmap bmp) {
        File tmp = new File(dst.getParentFile(), dst.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            if (!bmp.compress(Bitmap.CompressFormat.PNG, 100, out)) {
                deleteQuiet(tmp);
                return false;
            }
            out.flush();
        } catch (IOException e) {
            deleteQuiet(tmp);
            return false;
        }
        if (tmp.renameTo(dst)) return true;
        deleteQuiet(tmp);
        return false;
    }

    private static boolean writeText(File dst, String val) {
        File tmp = new File(dst.getParentFile(), dst.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(val.getBytes(StandardCharsets.UTF_8));
            out.flush();
        } catch (IOException e) {
            deleteQuiet(tmp);
            return false;
        }
        if (tmp.renameTo(dst)) return true;
        deleteQuiet(tmp);
        return false;
    }

    private static void deleteQuiet(File f) {
        if (f.exists() && !f.delete()) Log.w(TAG, "delete failed: " + f.getName());
    }

    private static byte[] readSmallFile(File f, int maxBytes) {
        long len = f.length();
        if (len <= 0 || len > maxBytes) return null;
        byte[] buf = new byte[(int) len];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            for (int i = 0; i < 4096 && off < buf.length; i++) {
                int n = in.read(buf, off, buf.length - off);
                if (n <= 0) break;
                off += n;
            }
            return off == buf.length ? buf : null;
        } catch (IOException e) {
            return null;
        }
    }

    private static boolean readFully(RandomAccessFile raf, byte[] dst, int len) throws IOException {
        int off = 0;
        for (int i = 0; i < 4096 && off < len; i++) {
            int n = raf.read(dst, off, len - off);
            if (n <= 0) break;
            off += n;
        }
        return off == len;
    }

    private static boolean isRgbaFmt(int fmt) {
        return fmt == 0x00 || fmt == 0x01 || fmt == 0x03 || fmt == 0x04 || fmt == 0x09;
    }

    private static int fmtBpp(int fmt) {
        if (isRgbaFmt(fmt)) return 4;
        if (fmt == 0x59 || fmt == 0x60) return 8;
        if (fmt == 0x5b || fmt == 0x62) return 16;
        return 0;
    }

    private static boolean matchAscii(byte[] b, int off, String s) {
        if (off + s.length() > b.length) return false;
        for (int i = 0; i < s.length(); i++) if ((b[off + i] & 0xFF) != s.charAt(i)) return false;
        return true;
    }

    private static boolean matchCstr(byte[] b, int off, int maxLen, String s) {
        if (s.length() >= maxLen || off + maxLen > b.length) return false;
        for (int i = 0; i < s.length(); i++) if ((b[off + i] & 0xFF) != s.charAt(i)) return false;
        return b[off + s.length()] == 0;
    }

    private static int le16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }
    private static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }
    private static long le48(byte[] b, int o) {
        return (le32(b, o) & 0xFFFFFFFFL) | (((long) le16(b, o + 4)) << 32);
    }
    private static long le64(byte[] b, int o) {
        return (le32(b, o) & 0xFFFFFFFFL) | (((long) le32(b, o + 4)) << 32);
    }
    private static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}
