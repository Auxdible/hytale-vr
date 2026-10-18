package dev.hytalevr.plugin;

/**
 * Asks what a hand's ray reaches: its origin relative to the player's eye and
 * its direction, plus Hytale's own look direction in the same axes so the axes
 * can be checked against the server's.
 */
public record AimMessage(double[] origin, double[] direction, double[] look) {

    /** Parses "AIM ox oy oz dx dy dz lx ly lz"; returns null for anything else. */
    public static AimMessage parse(String line) {
        final String[] parts = line.trim().split("\\s+");
        if (parts.length != 10 || !parts[0].equals("AIM")) return null;
        try {
            final double[] values = new double[9];
            for (int i = 0; i < values.length; i++) {
                values[i] = Double.parseDouble(parts[1 + i]);
                if (!Double.isFinite(values[i])) return null;
            }
            for (int i = 0; i < 3; i++) {
                if (Math.abs(values[i]) > 4) return null;
            }
            final double[] direction = RayMath.normalize(values[3], values[4], values[5]);
            final double[] look = RayMath.normalize(values[6], values[7], values[8]);
            if (direction == null || look == null) return null;
            return new AimMessage(new double[] {values[0], values[1], values[2]}, direction, look);
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
