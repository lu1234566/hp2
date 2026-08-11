package com.hp2.mobile;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class GameDataRepository {
    interface Callback<T> {
        void complete(T result);
    }

    static final class ScanResult {
        final int candidates;
        final int validPackages;
        final int maps;
        final boolean duel10;
        final long bytes;

        ScanResult(int candidates, int validPackages, int maps, boolean duel10, long bytes) {
            this.candidates = candidates;
            this.validPackages = validPackages;
            this.maps = maps;
            this.duel10 = duel10;
            this.bytes = bytes;
        }

        boolean canLaunch() {
            return validPackages > 0;
        }
    }

    static final class ImportResult {
        final boolean changed;
        final int files;
        final long bytes;
        final String message;

        ImportResult(boolean changed, int files, long bytes, String message) {
            this.changed = changed;
            this.files = files;
            this.bytes = bytes;
            this.message = message;
        }
    }

    private static final Set<String> GAME_FOLDERS = new HashSet<>(Arrays.asList(
        "maps", "textures", "system", "sounds", "music"
    ));
    private static final Set<String> PACKAGE_EXTENSIONS = new HashSet<>(Arrays.asList(
        "u", "utx", "unr", "uax", "umx"
    ));
    private static final Set<String> BLOCKED_EXTENSIONS = new HashSet<>(Arrays.asList(
        "exe", "dll", "com", "bat", "cmd", "msi", "scr"
    ));
    private static final long MAX_IMPORT_BYTES = 8L * 1024L * 1024L * 1024L;
    private static final int BUFFER_BYTES = 64 * 1024;

    private final Context context;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    GameDataRepository(Context context) {
        this.context = context.getApplicationContext();
    }

    void scanAsync(Callback<ScanResult> callback) {
        executor.execute(() -> callback.complete(scan()));
    }

    void importTreeAsync(Uri treeUri, Callback<ImportResult> callback) {
        executor.execute(() -> callback.complete(importTree(treeUri)));
    }

    void importDocumentAsync(Uri documentUri, Callback<ImportResult> callback) {
        executor.execute(() -> callback.complete(importDocument(documentUri)));
    }

    void close() {
        executor.shutdownNow();
    }

    private File gameRoot() throws IOException {
        File base = context.getExternalFilesDir(null);
        if (base == null) {
            base = context.getFilesDir();
        }
        final File root = new File(base, "game");
        if (!root.isDirectory() && !root.mkdirs()) {
            throw new IOException("Não foi possível criar a pasta privada do jogo.");
        }
        return root;
    }

    private ScanResult scan() {
        try {
            final MutableScan scan = new MutableScan();
            scanDirectory(gameRoot(), scan);
            return new ScanResult(scan.candidates, scan.valid, scan.maps, scan.duel10, scan.bytes);
        } catch (IOException error) {
            return new ScanResult(0, 0, 0, false, 0);
        }
    }

    private void scanDirectory(File directory, MutableScan scan) {
        final File[] children = directory.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (child.isDirectory()) {
                scanDirectory(child, scan);
                continue;
            }
            scan.bytes += Math.max(0L, child.length());
            final String extension = extensionOf(child.getName());
            if (!PACKAGE_EXTENSIONS.contains(extension)) {
                continue;
            }
            ++scan.candidates;
            if ("unr".equals(extension)) {
                ++scan.maps;
            }
            if (hasValidPackageHeader(child)) {
                ++scan.valid;
                if ("duel10.unr".equalsIgnoreCase(child.getName())) {
                    scan.duel10 = true;
                }
            }
        }
    }

    private boolean hasValidPackageHeader(File file) {
        final byte[] header = new byte[36];
        try (InputStream input = new BufferedInputStream(new FileInputStream(file))) {
            if (readFully(input, header, 0, header.length) != header.length) {
                return false;
            }
        } catch (IOException error) {
            return false;
        }
        if ((header[0] & 0xff) != 0xc1 || (header[1] & 0xff) != 0x83
            || (header[2] & 0xff) != 0x2a || (header[3] & 0xff) != 0x9e) {
            return false;
        }
        final long fileSize = file.length();
        return tableLooksSane(readI32(header, 12), readI32(header, 16), fileSize)
            && tableLooksSane(readI32(header, 20), readI32(header, 24), fileSize)
            && tableLooksSane(readI32(header, 28), readI32(header, 32), fileSize);
    }

    private ImportResult importTree(Uri treeUri) {
        final MutableImport imported = new MutableImport();
        try {
            final String rootDocumentId = DocumentsContract.getTreeDocumentId(treeUri);
            final Uri rootDocument = DocumentsContract.buildDocumentUriUsingTree(treeUri, rootDocumentId);
            final String rootName = queryDisplayName(rootDocument);
            final boolean rootIsGameFolder = GAME_FOLDERS.contains(rootName.toLowerCase(Locale.ROOT));
            final String relative = rootIsGameFolder ? sanitizeSegment(rootName) : "";
            copyDocumentTree(treeUri, rootDocumentId, relative, rootIsGameFolder, imported, gameRoot());
            if (imported.files == 0) {
                return new ImportResult(false, 0, 0,
                    "Nenhuma pasta Maps, Textures, System, Sounds ou Music foi encontrada.");
            }
            return new ImportResult(true, imported.files, imported.bytes,
                "Dados importados da pasta selecionada.");
        } catch (Exception error) {
            return new ImportResult(false, imported.files, imported.bytes,
                "Falha ao importar pasta: " + safeMessage(error));
        }
    }

    private void copyDocumentTree(
        Uri treeUri,
        String parentDocumentId,
        String relative,
        boolean insideGameFolder,
        MutableImport imported,
        File targetRoot
    ) throws IOException {
        final Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocumentId);
        final String[] projection = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = context.getContentResolver().query(childrenUri, projection, null, null, null)) {
            if (cursor == null) {
                throw new IOException("O provedor de arquivos não retornou a pasta.");
            }
            while (cursor.moveToNext()) {
                final String documentId = cursor.getString(0);
                final String name = sanitizeSegment(cursor.getString(1));
                final String mimeType = cursor.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
                    final boolean startsGameFolder = GAME_FOLDERS.contains(name.toLowerCase(Locale.ROOT));
                    final boolean childInside = insideGameFolder || startsGameFolder;
                    final String childRelative;
                    if (insideGameFolder) {
                        childRelative = join(relative, name);
                    } else if (startsGameFolder) {
                        childRelative = name;
                    } else {
                        childRelative = relative;
                    }
                    copyDocumentTree(treeUri, documentId, childRelative, childInside, imported, targetRoot);
                } else if (insideGameFolder && !isBlocked(name)) {
                    final Uri documentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
                    copyStreamToGame(documentUri, join(relative, name), imported, targetRoot);
                }
            }
        }
    }

    private ImportResult importDocument(Uri documentUri) {
        final MutableImport imported = new MutableImport();
        try {
            final String name = queryDisplayName(documentUri);
            final String extension = extensionOf(name);
            if ("mdf".equals(extension) || "iso".equals(extension)) {
                final boolean iso = containsIso9660Descriptor(documentUri);
                final String message = iso
                    ? "Imagem original reconhecida. O extrator InstallShield nativo ainda está no próximo módulo."
                    : "A imagem foi aberta, mas não contém um descritor ISO9660 reconhecível.";
                return new ImportResult(false, 0, 0, message);
            }
            if (!"zip".equals(extension)) {
                return new ImportResult(false, 0, 0,
                    "Formato não suportado nesta compilação. Selecione uma pasta instalada, ZIP, MDF ou ISO.");
            }
            try (InputStream raw = context.getContentResolver().openInputStream(documentUri)) {
                if (raw == null) {
                    throw new IOException("Não foi possível abrir o arquivo.");
                }
                importZip(raw, imported, gameRoot());
            }
            if (imported.files == 0) {
                return new ImportResult(false, 0, 0,
                    "O ZIP não contém as pastas de dados esperadas.");
            }
            return new ImportResult(true, imported.files, imported.bytes, "Dados importados do ZIP.");
        } catch (Exception error) {
            return new ImportResult(false, imported.files, imported.bytes,
                "Falha ao importar arquivo: " + safeMessage(error));
        }
    }

    private void importZip(InputStream raw, MutableImport imported, File targetRoot) throws IOException {
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(raw))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entry.isDirectory()) {
                    zip.closeEntry();
                    continue;
                }
                final String relative = gameRelativeZipPath(entry.getName());
                if (relative != null && !isBlocked(relative)) {
                    copyStream(zip, relative, imported, targetRoot);
                }
                zip.closeEntry();
            }
        }
    }

    private String gameRelativeZipPath(String rawPath) {
        final String normalized = rawPath.replace('\\', '/');
        final String[] rawParts = normalized.split("/");
        int gameFolderIndex = -1;
        for (int index = 0; index < rawParts.length; ++index) {
            final String part = rawParts[index];
            if ("..".equals(part) || part.isEmpty()) {
                if ("..".equals(part)) {
                    return null;
                }
                continue;
            }
            if (GAME_FOLDERS.contains(part.toLowerCase(Locale.ROOT))) {
                gameFolderIndex = index;
                break;
            }
        }
        if (gameFolderIndex < 0) {
            return null;
        }
        final StringBuilder relative = new StringBuilder();
        for (int index = gameFolderIndex; index < rawParts.length; ++index) {
            final String part = sanitizeSegment(rawParts[index]);
            if (part.isEmpty()) {
                continue;
            }
            if (relative.length() > 0) {
                relative.append(File.separatorChar);
            }
            relative.append(part);
        }
        return relative.length() == 0 ? null : relative.toString();
    }

    private void copyStreamToGame(Uri uri, String relative, MutableImport imported, File targetRoot)
        throws IOException {
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) {
                throw new IOException("Não foi possível abrir " + relative);
            }
            copyStream(input, relative, imported, targetRoot);
        }
    }

    private void copyStream(InputStream input, String relative, MutableImport imported, File targetRoot)
        throws IOException {
        final File target = safeTarget(targetRoot, relative);
        final File parent = target.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            throw new IOException("Não foi possível criar " + relative);
        }
        final File temporary = new File(parent, target.getName() + ".part");
        long copied = 0;
        try (OutputStream output = new BufferedOutputStream(new FileOutputStream(temporary))) {
            final byte[] buffer = new byte[BUFFER_BYTES];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read == 0) {
                    continue;
                }
                copied += read;
                if (imported.bytes + copied > MAX_IMPORT_BYTES) {
                    throw new IOException("A importação ultrapassou o limite de segurança de 8 GB.");
                }
                output.write(buffer, 0, read);
            }
        } catch (IOException error) {
            Files.deleteIfExists(temporary.toPath());
            throw error;
        }
        try {
            Files.move(temporary.toPath(), target.toPath(),
                StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException ignored) {
            Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
        ++imported.files;
        imported.bytes += copied;
    }

    private File safeTarget(File root, String relative) throws IOException {
        final File target = new File(root, relative);
        final String rootPath = root.getCanonicalPath() + File.separator;
        final String targetPath = target.getCanonicalPath();
        if (!targetPath.startsWith(rootPath)) {
            throw new IOException("Caminho inválido dentro do pacote.");
        }
        return target;
    }

    private boolean containsIso9660Descriptor(Uri uri) throws IOException {
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) {
                return false;
            }
            long remaining = 16L * 2048L;
            while (remaining > 0) {
                final long skipped = input.skip(remaining);
                if (skipped > 0) {
                    remaining -= skipped;
                } else if (input.read() >= 0) {
                    --remaining;
                } else {
                    return false;
                }
            }
            final byte[] descriptor = new byte[6];
            if (readFully(input, descriptor, 0, descriptor.length) != descriptor.length) {
                return false;
            }
            return descriptor[0] == 1 && descriptor[1] == 'C' && descriptor[2] == 'D'
                && descriptor[3] == '0' && descriptor[4] == '0' && descriptor[5] == '1';
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = context.getContentResolver().query(
            uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null
        )) {
            if (cursor != null && cursor.moveToFirst()) {
                final String value = cursor.getString(0);
                if (value != null && !value.isEmpty()) {
                    return value;
                }
            }
        }
        return "dados";
    }

    private static int readFully(InputStream input, byte[] buffer, int offset, int length) throws IOException {
        int total = 0;
        while (total < length) {
            final int read = input.read(buffer, offset + total, length - total);
            if (read < 0) {
                break;
            }
            total += read;
        }
        return total;
    }

    private static int readI32(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff)
            | ((bytes[offset + 1] & 0xff) << 8)
            | ((bytes[offset + 2] & 0xff) << 16)
            | ((bytes[offset + 3] & 0xff) << 24);
    }

    private static boolean tableLooksSane(int count, int offset, long fileSize) {
        if (count < 0 || count > 2_000_000 || offset < 0) {
            return false;
        }
        return count == 0 ? offset <= fileSize : offset >= 36 && offset < fileSize;
    }

    private static String join(String parent, String child) {
        return parent == null || parent.isEmpty() ? child : parent + File.separator + child;
    }

    private static String sanitizeSegment(String value) {
        if (value == null) {
            return "sem-nome";
        }
        String cleaned = value.replace('/', '_').replace('\\', '_').replace('\0', '_').trim();
        if (cleaned.isEmpty() || ".".equals(cleaned) || "..".equals(cleaned)) {
            return "sem-nome";
        }
        return cleaned.length() > 180 ? cleaned.substring(0, 180) : cleaned;
    }

    private static String extensionOf(String name) {
        final int dot = name.lastIndexOf('.');
        return dot < 0 || dot + 1 >= name.length()
            ? "" : name.substring(dot + 1).toLowerCase(Locale.ROOT);
    }

    private static boolean isBlocked(String name) {
        return BLOCKED_EXTENSIONS.contains(extensionOf(name));
    }

    private static String safeMessage(Exception error) {
        final String message = error.getMessage();
        return message == null || message.trim().isEmpty() ? error.getClass().getSimpleName() : message;
    }

    private static final class MutableScan {
        int candidates;
        int valid;
        int maps;
        boolean duel10;
        long bytes;
    }

    private static final class MutableImport {
        int files;
        long bytes;
    }
}
