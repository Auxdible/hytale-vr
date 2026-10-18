package dev.hytalevr.plugin;

/**
 * The client's render axes may differ from the server's world axes by a flip
 * of X and/or Z. Rather than assume, pick the flip that best lines up the
 * client's look direction with the server's, and refuse when none does.
 */
public enum AxisMapping {
    IDENTITY(1, 1),
    FLIP_Z(1, -1),
    FLIP_X(-1, 1),
    FLIP_XZ(-1, -1);

    private static final double MIN_AGREEMENT = 0.8;

    private final int signX;
    private final int signZ;

    AxisMapping(int signX, int signZ) {
        this.signX = signX;
        this.signZ = signZ;
    }

    public double[] apply(double[] v) {
        return new double[] {v[0] * signX, v[1], v[2] * signZ};
    }

    /** Returns the mapping that turns clientLook into serverLook, or null if none fits. */
    public static AxisMapping find(double[] clientLook, double[] serverLook) {
        AxisMapping best = null;
        double bestDot = MIN_AGREEMENT;
        for (AxisMapping mapping : values()) {
            final double[] mapped = mapping.apply(clientLook);
            final double dot = mapped[0] * serverLook[0] + mapped[1] * serverLook[1] + mapped[2] * serverLook[2];
            if (dot > bestDot) {
                bestDot = dot;
                best = mapping;
            }
        }
        return best;
    }
}
