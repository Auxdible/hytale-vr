package dev.hytalevr.plugin;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class RayMathTest {
    @Test
    void hitsBoxInFront() {
        final double t = RayMath.rayBoxEntry(0, 0.5, 0, 0, 0, -1, -0.5, 0, -3, 0.5, 1, -2);
        assertEquals(2.0, t, 1e-9);
    }

    @Test
    void missesBoxBehindOrBeside() {
        assertTrue(Double.isNaN(RayMath.rayBoxEntry(0, 0.5, 0, 0, 0, 1, -0.5, 0, -3, 0.5, 1, -2)));
        assertTrue(Double.isNaN(RayMath.rayBoxEntry(5, 0.5, 0, 0, 0, -1, -0.5, 0, -3, 0.5, 1, -2)));
    }

    @Test
    void leavesBoxOnFarSide() {
        assertEquals(3.0, RayMath.rayBoxExit(0, 0.5, 0, 0, 0, -1, -0.5, 0, -3, 0.5, 1, -2), 1e-6);
        assertTrue(Double.isNaN(RayMath.rayBoxExit(5, 0.5, 0, 0, 0, -1, -0.5, 0, -3, 0.5, 1, -2)));
    }

    @Test
    void insideBoxIsZero() {
        assertEquals(0.0, RayMath.rayBoxEntry(0, 0.5, -2.5, 1, 0, 0, -0.5, 0, -3, 0.5, 1, -2), 1e-9);
    }

    @Test
    void parsesDashboardSwing() {
        final SwingMessage swing = SwingMessage.parse(
                "SWING 1 0.2500 -0.3000 -0.1000 0.00000 -0.70711 -0.70711 3.500 0.00000 0.00000 -1.00000");
        assertNotNull(swing);
        assertEquals(1, swing.hand());
        assertEquals(-0.70711, swing.direction()[2], 1e-4);
        assertEquals(-1.0, swing.look()[2], 1e-9);
    }

    @Test
    void rejectsMalformedSwing() {
        assertNull(SwingMessage.parse("SWING 1 0 0 0 0 0 0 1 0 0 -1"));
        assertNull(SwingMessage.parse("SWING 1 99 0 0 0 0 -1 1 0 0 -1"));
        assertNull(SwingMessage.parse("SWING 1 0 0 0 0 0 -1 1"));
        assertNull(SwingMessage.parse("HELLO hytale-vr 1"));
        assertNull(SwingMessage.parse("SWING 1 a b c d e f g h i j"));
    }

    @Test
    void findsAxisFlipFromLooks() {
        assertEquals(AxisMapping.IDENTITY, AxisMapping.find(new double[] {0, 0, -1}, new double[] {0, 0, -1}));
        assertEquals(AxisMapping.FLIP_Z, AxisMapping.find(new double[] {0, 0, -1}, new double[] {0, 0, 1}));
        assertEquals(AxisMapping.FLIP_XZ, AxisMapping.find(new double[] {0.6, 0, -0.8}, new double[] {-0.6, 0, 0.8}));
        assertNull(AxisMapping.find(new double[] {0, 1, 0}, new double[] {0, 0, 1}));
    }
}
