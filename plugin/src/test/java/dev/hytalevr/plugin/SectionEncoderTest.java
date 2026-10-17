package dev.hytalevr.plugin;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class SectionEncoderTest {
    @Test
    void roundTripsTerrain() {
        final int[] ids = new int[SectionEncoder.CELLS];
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                for (int x = 0; x < 32; x++) {
                    final int index = (y << 10) | (z << 5) | x;
                    ids[index] = y < 10 ? 7 : y == 10 ? 300 + (x % 3) : 0;
                }
            }
        }
        final String encoded = SectionEncoder.encode(ids);
        assertArrayEquals(ids, SectionEncoder.decode(encoded));
        // Layered terrain should compress far below the raw 128 KiB.
        assertTrue(encoded.length() < 4000, "encoded length " + encoded.length());
    }

    @Test
    void roundTripsNoise() {
        final int[] ids = new int[SectionEncoder.CELLS];
        long seed = 12345;
        for (int i = 0; i < ids.length; i++) {
            seed = seed * 6364136223846793005L + 1442695040888963407L;
            ids[i] = (int) ((seed >>> 33) % 5000);
        }
        assertArrayEquals(ids, SectionEncoder.decode(SectionEncoder.encode(ids)));
    }
}
