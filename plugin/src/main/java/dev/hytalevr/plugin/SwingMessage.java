package dev.hytalevr.plugin;

/**
 * A swing reported by the dashboard: which hand, where it points (origin
 * relative to the player's eye), how fast it moved, and Hytale's own look
 * direction in the same axes so the axes can be checked against the server's.
 */
public record SwingMessage(int hand, double[] origin, double[] direction, double speed, double[] look) {

    /** Parses "SWING hand ox oy oz dx dy dz speed lx ly lz"; returns null for anything else. */
    public static SwingMessage parse(String line) {
        final String[] parts = line.trim().split("\\s+");
        if (parts.length != 12 || !parts[0].equals("SWING")) return null;
        try {
            final int hand = Integer.parseInt(parts[1]);
            final double[] values = new double[10];
            for (int i = 0; i < values.length; i++) {
                values[i] = Double.parseDouble(parts[2 + i]);
                if (!Double.isFinite(values[i])) return null;
            }
            // The origin is a hand offset from the eye: anything far away is bogus.
            for (int i = 0; i < 3; i++) {
                if (Math.abs(values[i]) > 4) return null;
            }
            final double[] direction = RayMath.normalize(values[3], values[4], values[5]);
            final double[] look = RayMath.normalize(values[7], values[8], values[9]);
            if (direction == null || look == null) return null;
            return new SwingMessage(hand == 0 ? 0 : 1,
                    new double[] {values[0], values[1], values[2]}, direction, values[6], look);
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
