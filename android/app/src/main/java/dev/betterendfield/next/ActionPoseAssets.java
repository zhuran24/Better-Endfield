package dev.betterendfield.next;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.function.Consumer;

/**
 * Materializes the sustained-dash pose and accessory banks where the game process can read
 * them.
 *
 * On desktop the module DLL finds {@code actions/pose_<codename>.bin} beside
 * itself. Android has no per-module directory inside the game, so the banks ship
 * in this APK's assets and are copied into the game's own files directory once.
 * The native side is told where through
 * {@code BETTER_ENDFIELD_NEXT_ACTIONS_ASSET_ROOT}.
 *
 * The optional pose and accessory overlays need them. Without them the module still
 * holds the dash through the native route and says so in its log.
 */
final class ActionPoseAssets {
    private static final String ASSET_DIRECTORY = "actions";
    private static final String[] BANKS = {"pose_aglina.bin", "pose_liino.bin", "accessory_jsspsi.bin"};

    private ActionPoseAssets() {}

    /**
     * @return the absolute directory the banks are in, or an empty string when
     *     none of them could be published.
     */
    static String materialize(Context game, Context module, Consumer<String> log) {
        File target = new File(game.getFilesDir(), "betterendfieldnext/actions");
        if (!target.isDirectory() && !target.mkdirs()) {
            log.accept("bone-pose directory could not be created: " + target);
            return "";
        }
        int published = 0;
        for (String bank : BANKS) {
            try {
                if (copy(module, bank, new File(target, bank))) ++published;
            } catch (IOException | RuntimeException error) {
                log.accept("bone-pose bank " + bank + " unavailable: " + error);
            }
        }
        if (published == 0) return "";
        log.accept("bone-pose banks ready: " + published + "/" + BANKS.length
                + " in " + target);
        return target.getAbsolutePath();
    }

    private static boolean sameContents(Context module, String asset, File target) throws IOException {
        try (InputStream source = module.getAssets().open(asset);
             InputStream existing = new FileInputStream(target)) {
            byte[] expected = new byte[64 * 1024];
            byte[] actual = new byte[64 * 1024];
            for (int count; (count = source.read(expected)) != -1;) {
                int offset = 0;
                while (offset < count) {
                    int read = existing.read(actual, offset, count - offset);
                    if (read < 0) return false;
                    offset += read;
                }
                for (int i = 0; i < count; ++i) if (expected[i] != actual[i]) return false;
            }
            return existing.read() == -1;
        }
    }

    private static boolean copy(Context module, String name, File target) throws IOException {
        String asset = ASSET_DIRECTORY + "/" + name;
        try (InputStream source = module.getAssets().open(asset)) {
            if (target.isFile() && sameContents(module, asset, target)) return true;
            File temporary = new File(target.getParentFile(), name + ".tmp");
            try (FileOutputStream sink = new FileOutputStream(temporary, false)) {
                byte[] buffer = new byte[64 * 1024];
                for (int read = source.read(buffer); read > 0; read = source.read(buffer)) {
                    sink.write(buffer, 0, read);
                }
                sink.getFD().sync();
            }
            if (!temporary.renameTo(target)) {
                temporary.delete();
                throw new IOException("could not publish " + target);
            }
            return true;
        }
    }
}
