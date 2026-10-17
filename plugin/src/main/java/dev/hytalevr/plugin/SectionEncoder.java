package dev.hytalevr.plugin;

import java.io.ByteArrayOutputStream;
import java.util.Base64;
import java.util.HashMap;
import java.util.Map;

/**
 * Packs one 32x32x32 section of block ids for the VR viewer: a palette of the
 * distinct ids, then run-length pairs of palette indexes, all as varints, then
 * Base64. Cells are ordered like Hytale's ChunkUtil.indexBlock:
 * index = (y << 10) | (z << 5) | x.
 */
public final class SectionEncoder {
    public static final int SIZE = 32;
    public static final int CELLS = SIZE * SIZE * SIZE;

    private SectionEncoder() {}

    public static String encode(int[] ids) {
        if (ids.length != CELLS) throw new IllegalArgumentException("section must have " + CELLS + " cells");
        final Map<Integer, Integer> paletteIndex = new HashMap<>();
        final int[] palette = new int[CELLS];
        int paletteSize = 0;
        final int[] indexes = new int[CELLS];
        for (int i = 0; i < CELLS; i++) {
            Integer index = paletteIndex.get(ids[i]);
            if (index == null) {
                index = paletteSize;
                palette[paletteSize++] = ids[i];
                paletteIndex.put(ids[i], index);
            }
            indexes[i] = index;
        }
        final ByteArrayOutputStream out = new ByteArrayOutputStream(1024);
        writeVarint(out, paletteSize);
        for (int i = 0; i < paletteSize; i++) writeVarint(out, palette[i]);
        int start = 0;
        while (start < CELLS) {
            int end = start + 1;
            while (end < CELLS && indexes[end] == indexes[start]) end++;
            writeVarint(out, end - start);
            writeVarint(out, indexes[start]);
            start = end;
        }
        return Base64.getEncoder().encodeToString(out.toByteArray());
    }

    /** Inverse of {@link #encode}; used by tests and as the reference for the viewer's decoder. */
    public static int[] decode(String base64) {
        final byte[] bytes = Base64.getDecoder().decode(base64);
        final int[] cursor = {0};
        final int paletteSize = readVarint(bytes, cursor);
        final int[] palette = new int[paletteSize];
        for (int i = 0; i < paletteSize; i++) palette[i] = readVarint(bytes, cursor);
        final int[] ids = new int[CELLS];
        int written = 0;
        while (written < CELLS) {
            final int run = readVarint(bytes, cursor);
            final int id = palette[readVarint(bytes, cursor)];
            for (int i = 0; i < run; i++) ids[written++] = id;
        }
        return ids;
    }

    static void writeVarint(ByteArrayOutputStream out, int value) {
        int v = value;
        while ((v & ~0x7F) != 0) {
            out.write((v & 0x7F) | 0x80);
            v >>>= 7;
        }
        out.write(v);
    }

    static int readVarint(byte[] bytes, int[] cursor) {
        int result = 0;
        int shift = 0;
        while (true) {
            final int b = bytes[cursor[0]++] & 0xFF;
            result |= (b & 0x7F) << shift;
            if ((b & 0x80) == 0) return result;
            shift += 7;
        }
    }
}
