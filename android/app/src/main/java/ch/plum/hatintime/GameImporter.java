package ch.plum.hatintime;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.Inflater;
import java.util.zip.InflaterInputStream;

/**
 * Copies the player's own A Hat in Time PC installation into the app's storage,
 * or unpacks a Plum Hub ZIP/ZIP64 data pack via FileChannel.
 */
final class GameImporter {
    static final String[][] MODULES = {
        { "Binaries/Win64/HatinTimeGame.exe", "0197f98641c2150c5e90e5e911f4464e38527c241e50817736ca7072c0b99892" },
    };

    interface Progress { void update(String file, long done, long total, long bytesPerSecond); }

    static final class Result {
        final boolean ok; final String message;
        Result(boolean ok, String message) { this.ok = ok; this.message = message; }
    }

    private final ContentResolver cr;
    private final File dest;
    private final Progress progress;
    private volatile boolean cancelled;
    private long done, total, t0, lastUi;
    private final byte[] buf = new byte[1 << 20];

    GameImporter(ContentResolver cr, File dest, Progress progress) { this.cr = cr; this.dest = dest; this.progress = progress; }
    void cancel() { cancelled = true; }

    static boolean skipped(String rel) {
        String l = rel.toLowerCase(Locale.ROOT).replace('\\', '/');
        return l.startsWith("binaries/win32/") || l.startsWith("_commonredist/")
            || l.equals("binaries/win64/ue3shadercompileworker.exe")
            || l.equals("binaries/win64/curl.exe")
            || l.equals("binaries/win64/dxvk.dll")
            || l.equals("binaries/win64/crashreportclient.exe")
            || l.endsWith(".part");
    }

    static boolean installed(File dir) {
        return new File(dir, "Binaries/Win64/HatinTimeGame.exe").isFile()
            && new File(dir, "HatinTimeGame/CookedPC/Startup.upk").isFile()
            && new File(dir, "HatinTimeGame/CookedPC/RefShaderCache-PC-D3D-SM3.upk").isFile();
    }

    static String sha256(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] b = new byte[1 << 16]; int n;
            while ((n = in.read(b)) > 0) md.update(b, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte x : md.digest()) sb.append(String.format(Locale.ROOT, "%02x", x));
            return sb.toString();
        } catch (java.security.NoSuchAlgorithmException e) { throw new IOException(e); }
    }

    static long freeBytes(File dir) {
        dir.mkdirs();
        return new StatFs(dir.getAbsolutePath()).getAvailableBytes();
    }

    private static final class Doc { String id, rel; long size; }

    private Cursor children(Uri tree, String docId) {
        return cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId), new String[] {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE }, null, null, null);
    }

    private String findRoot(Uri tree, String docId, int depth) {
        boolean hasBinaries = false, hasGame = false;
        List<String> dirs = new ArrayList<>();
        try (Cursor c = children(tree, docId)) {
            if (c == null) return null;
            while (c.moveToNext()) {
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) {
                    String name = c.getString(1);
                    if ("Binaries".equalsIgnoreCase(name)) hasBinaries = true;
                    if ("HatinTimeGame".equalsIgnoreCase(name)) hasGame = true;
                    dirs.add(c.getString(0));
                }
            }
        }
        if (hasBinaries && hasGame) return docId;
        if (depth >= 2) return null;
        for (String d : dirs) { String r = findRoot(tree, d, depth + 1); if (r != null) return r; }
        return null;
    }

    private void list(Uri tree, String docId, String prefix, List<Doc> out) {
        List<String[]> dirs = new ArrayList<>();
        try (Cursor c = children(tree, docId)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                String rel = prefix + name;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!skipped(rel + "/")) dirs.add(new String[] { id, rel + "/" });
                } else if (!skipped(rel)) {
                    Doc d = new Doc(); d.id = id; d.rel = rel; d.size = c.isNull(3) ? 0 : c.getLong(3);
                    out.add(d);
                }
            }
        }
        for (String[] d : dirs) list(tree, d[0], d[1], out);
    }

    Result importTree(Uri tree) {
        try {
            String root = findRoot(tree, DocumentsContract.getTreeDocumentId(tree), 0);
            if (root == null)
                return new Result(false, "In diesem Ordner fehlen Binaries und HatinTimeGame. Wähle deinen A-Hat-in-Time-Ordner.");
            List<Doc> files = new ArrayList<>();
            list(tree, root, "", files);
            files.sort((a, b) -> Integer.compare(order(a.rel), order(b.rel)));
            total = 0;
            long need = 0;
            for (Doc d : files) {
                total += d.size;
                File f = new File(dest, d.rel);
                if (!(f.isFile() && f.length() == d.size)) need += d.size;
            }
            if (need > freeBytes(dest) - (500L << 20))
                return new Result(false, String.format(Locale.ROOT, "Zu wenig freier Speicher: %.1f GB nötig, %.1f GB frei.", need / 1e9, freeBytes(dest) / 1e9));
            t0 = System.nanoTime(); done = 0;
            for (Doc d : files) {
                if (cancelled) return new Result(false, "Import abgebrochen.");
                File out = new File(dest, d.rel);
                if (out.isFile() && out.length() == d.size) { done += d.size; tick(d.rel); }
                else try (InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, d.id))) {
                    copy(in, out, d.rel);
                }
                String want = moduleHash(d.rel);
                if (want != null) {
                    String h = sha256(out);
                    if (!h.equals(want)) {
                        out.delete();
                        return new Result(false, d.rel + " stammt aus einer anderen Spielversion (SHA-256 " + h.substring(0, 12) + "…).");
                    }
                }
            }
            return new Result(true, String.format(Locale.ROOT, "Import fertig: %d Dateien, %.2f GB.", files.size(), total / 1e9));
        } catch (Exception e) {
            return new Result(false, "Import fehlgeschlagen: " + e.getMessage());
        }
    }

    static final File HUB_FALLBACK = new File("/sdcard/recomp");

    static File findPack(File... dirs) {
        for (File dir : dirs) {
            File[] fs = dir == null ? null : dir.listFiles();
            if (fs == null) continue;
            for (File f : fs) {
                String n = f.getName().toLowerCase(Locale.ROOT);
                if (f.isFile() && f.canRead() && n.endsWith(".zip") && (dir != HUB_FALLBACK || n.startsWith("hatintime"))) return f;
            }
        }
        return null;
    }

    private static final class ZEntry {
        String name;
        int method;
        long compSize, size, localOff;
    }

    private static void readFully(FileChannel ch, long pos, ByteBuffer dst) throws IOException {
        while (dst.hasRemaining()) {
            int n = ch.read(dst, pos);
            if (n < 0) throw new IOException("unerwartetes Dateiende im ZIP");
            pos += n;
        }
    }

    private static List<ZEntry> readZipCentralDir(FileChannel ch) throws IOException {
        long fileSize = ch.size();
        if (fileSize < 22) throw new IOException("Datei zu klein für ZIP");
        int tailLen = (int) Math.min(fileSize, 65557L);
        long tailOff = fileSize - tailLen;
        ByteBuffer tail = ByteBuffer.allocate(tailLen).order(ByteOrder.LITTLE_ENDIAN);
        readFully(ch, tailOff, tail);
        int eocd = -1;
        for (int i = tailLen - 22; i >= 0; i--) {
            if (tail.getInt(i) == 0x06054b50) { eocd = i; break; }
        }
        if (eocd < 0) throw new IOException("Kein ZIP-Inhaltsverzeichnis gefunden");
        long cdEntries = tail.getShort(eocd + 10) & 0xFFFFL;
        long cdSize = tail.getInt(eocd + 12) & 0xFFFFFFFFL;
        long cdOff = tail.getInt(eocd + 16) & 0xFFFFFFFFL;
        long eocdFileOff = tailOff + eocd;
        if (eocdFileOff >= 20) {
            ByteBuffer loc = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN);
            readFully(ch, eocdFileOff - 20, loc);
            if (loc.getInt(0) == 0x07064b50) {
                long z64Off = loc.getLong(8);
                ByteBuffer z64 = ByteBuffer.allocate(56).order(ByteOrder.LITTLE_ENDIAN);
                readFully(ch, z64Off, z64);
                if (z64.getInt(0) != 0x06064b50) throw new IOException("Ungültiger ZIP64-Header");
                cdEntries = z64.getLong(32);
                cdSize = z64.getLong(40);
                cdOff = z64.getLong(48);
            }
        }
        if (cdSize <= 0 || cdSize > (64L << 20)) throw new IOException("ZIP-Verzeichnis zu groß: " + cdSize);
        ByteBuffer cd = ByteBuffer.allocate((int) cdSize).order(ByteOrder.LITTLE_ENDIAN);
        readFully(ch, cdOff, cd);
        List<ZEntry> out = new ArrayList<>((int) Math.min(cdEntries, 4096));
        int p = 0;
        for (long i = 0; i < cdEntries && p + 46 <= cd.capacity(); i++) {
            if (cd.getInt(p) != 0x02014b50) throw new IOException("Defekter ZIP-Eintrag bei Offset " + p);
            int method = cd.getShort(p + 10) & 0xFFFF;
            long csz = cd.getInt(p + 20) & 0xFFFFFFFFL;
            long usz = cd.getInt(p + 24) & 0xFFFFFFFFL;
            int nlen = cd.getShort(p + 28) & 0xFFFF;
            int elen = cd.getShort(p + 30) & 0xFFFF;
            int clen = cd.getShort(p + 32) & 0xFFFF;
            long loff = cd.getInt(p + 42) & 0xFFFFFFFFL;
            byte[] nb = new byte[nlen];
            cd.position(p + 46);
            cd.get(nb);
            String name = new String(nb, StandardCharsets.UTF_8);
            int ep = p + 46 + nlen, eend = ep + elen;
            while (ep + 4 <= eend) {
                int tag = cd.getShort(ep) & 0xFFFF;
                int sz = cd.getShort(ep + 2) & 0xFFFF;
                ep += 4;
                if (tag == 0x0001 && ep + sz <= eend) {
                    int q = ep;
                    if (usz == 0xFFFFFFFFL && q + 8 <= ep + sz) { usz = cd.getLong(q); q += 8; }
                    if (csz == 0xFFFFFFFFL && q + 8 <= ep + sz) { csz = cd.getLong(q); q += 8; }
                    if (loff == 0xFFFFFFFFL && q + 8 <= ep + sz) { loff = cd.getLong(q); }
                }
                ep += sz;
            }
            p += 46 + nlen + elen + clen;
            if (name.endsWith("/") || name.contains("..") || name.startsWith("/") || skipped(name)) continue;
            ZEntry ze = new ZEntry();
            ze.name = name; ze.method = method; ze.compSize = csz; ze.size = usz; ze.localOff = loff;
            out.add(ze);
        }
        return out;
    }

    private static InputStream openEntryStream(final FileChannel ch, ZEntry e) throws IOException {
        ByteBuffer lh = ByteBuffer.allocate(30).order(ByteOrder.LITTLE_ENDIAN);
        readFully(ch, e.localOff, lh);
        if (lh.getInt(0) != 0x04034b50) throw new IOException("Defekter ZIP-Local-Header für " + e.name);
        int nlen = lh.getShort(26) & 0xFFFF;
        int elen = lh.getShort(28) & 0xFFFF;
        final long start = e.localOff + 30L + nlen + elen;
        InputStream bounded = new InputStream() {
            long pos = start, rem = e.compSize;
            @Override public int read() throws IOException {
                byte[] b = new byte[1];
                int n = read(b, 0, 1);
                return n < 0 ? -1 : (b[0] & 0xFF);
            }
            @Override public int read(byte[] b, int off, int len) throws IOException {
                if (len == 0) return 0;
                if (rem <= 0) return -1;
                int want = (int) Math.min(len, rem);
                ByteBuffer bb = ByteBuffer.wrap(b, off, want);
                int n = ch.read(bb, pos);
                if (n < 0) return -1;
                pos += n; rem -= n;
                return n;
            }
        };
        if (e.method == 0) return bounded;
        if (e.method == 8) {
            final Inflater inf = new Inflater(true);
            return new InflaterInputStream(bounded, inf, 65536) {
                @Override public void close() throws IOException {
                    try { super.close(); } finally { inf.end(); }
                }
            };
        }
        throw new IOException("Nicht unterstützte ZIP-Kompression (" + e.method + ") für " + e.name);
    }

    Result extractPack(File zip, Runnable afterwards) {
        try (FileInputStream fis = new FileInputStream(zip)) {
            return extractPack(fis.getChannel(), afterwards);
        } catch (Exception e) {
            return new Result(false, "Entpacken fehlgeschlagen: " + e.getMessage());
        }
    }

    Result extractPack(FileChannel ch, Runnable afterwards) {
        try {
            List<ZEntry> es = readZipCentralDir(ch);
            boolean hasExe = false;
            for (ZEntry e : es) if ("Binaries/Win64/HatinTimeGame.exe".equalsIgnoreCase(e.name)) { hasExe = true; break; }
            if (!hasExe)
                return new Result(false, "Das Datenpaket enthält kein Binaries/Win64/HatinTimeGame.exe.");
            es.sort((a, b) -> Integer.compare(order(a.name), order(b.name)));
            total = 0;
            long need = 0;
            for (ZEntry e : es) {
                total += e.size;
                File f = new File(dest, e.name);
                if (!(f.isFile() && f.length() == e.size)) need += e.size;
            }
            if (need > freeBytes(dest) - (500L << 20))
                return new Result(false, String.format(Locale.ROOT, "Zu wenig freier Speicher zum Entpacken: %.1f GB nötig, %.1f GB frei.", need / 1e9, freeBytes(dest) / 1e9));
            t0 = System.nanoTime(); done = 0;
            for (ZEntry e : es) {
                if (cancelled) return new Result(false, "Entpacken abgebrochen.");
                String rel = e.name;
                File out = new File(dest, rel);
                if (out.isFile() && out.length() == e.size) { done += e.size; tick(rel); }
                else try (InputStream in = openEntryStream(ch, e)) { copy(in, out, rel); }
                String want = moduleHash(rel);
                if (want != null) {
                    String h = sha256(out);
                    if (!h.equals(want)) {
                        out.delete();
                        return new Result(false, rel + " stammt aus einer anderen Spielversion (SHA-256 " + h.substring(0, 12) + "…).");
                    }
                }
            }
        } catch (Exception e) {
            return new Result(false, "Entpacken fehlgeschlagen: " + e.getMessage());
        }
        if (!installed(dest)) return new Result(false, "Das Datenpaket ist unvollständig.");
        if (afterwards != null) try { afterwards.run(); } catch (RuntimeException ignored) { }
        return new Result(true, String.format(Locale.ROOT, "Datenpaket entpackt: %.2f GB.", total / 1e9));
    }

    private static String moduleHash(String rel) {
        for (String[] m : MODULES) if (m[0].equalsIgnoreCase(rel)) return m[1];
        return null;
    }

    private static int order(String rel) {
        for (int i = 0; i < MODULES.length; i++) if (MODULES[i][0].equalsIgnoreCase(rel)) return i;
        return MODULES.length;
    }

    private void copy(InputStream in, File out, String rel) throws IOException {
        out.getParentFile().mkdirs();
        File part = new File(out.getPath() + ".part");
        try (OutputStream o = new FileOutputStream(part)) {
            int k;
            while ((k = in.read(buf)) > 0) {
                if (cancelled) throw new IOException("abgebrochen");
                o.write(buf, 0, k); done += k; tick(rel);
            }
        }
        if (!part.renameTo(out)) { out.delete(); if (!part.renameTo(out)) throw new IOException("kann nicht schreiben: " + out); }
    }

    private void tick(String rel) {
        long now = System.nanoTime();
        if (now - lastUi < 200_000_000L && done < total) return;
        lastUi = now;
        double s = (now - t0) / 1e9;
        progress.update(rel, done, total, s > 0.5 ? (long) (done / s) : 0);
    }
}
