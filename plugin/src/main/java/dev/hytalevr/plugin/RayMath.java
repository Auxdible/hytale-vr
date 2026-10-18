package dev.hytalevr.plugin;

/** Small, engine-independent geometry used to aim swings. */
public final class RayMath {
    private RayMath() {}

    /**
     * Distance along a ray to where it enters an axis-aligned box, or NaN when it
     * misses or the box lies entirely behind the origin. A ray starting inside
     * the box reports 0.
     */
    public static double rayBoxEntry(double ox, double oy, double oz,
                                     double dx, double dy, double dz,
                                     double minX, double minY, double minZ,
                                     double maxX, double maxY, double maxZ) {
        double near = Double.NEGATIVE_INFINITY;
        double far = Double.POSITIVE_INFINITY;
        final double[] origin = {ox, oy, oz};
        final double[] direction = {dx, dy, dz};
        final double[] min = {minX, minY, minZ};
        final double[] max = {maxX, maxY, maxZ};
        for (int axis = 0; axis < 3; axis++) {
            if (Math.abs(direction[axis]) < 1e-9) {
                if (origin[axis] < min[axis] || origin[axis] > max[axis]) return Double.NaN;
                continue;
            }
            double t1 = (min[axis] - origin[axis]) / direction[axis];
            double t2 = (max[axis] - origin[axis]) / direction[axis];
            if (t1 > t2) {
                final double swap = t1;
                t1 = t2;
                t2 = swap;
            }
            near = Math.max(near, t1);
            far = Math.min(far, t2);
            if (near > far) return Double.NaN;
        }
        if (far < 0) return Double.NaN;
        return Math.max(near, 0);
    }

    /**
     * Distance along a ray to where it leaves an axis-aligned box, or NaN when
     * it misses the box.
     */
    public static double rayBoxExit(double ox, double oy, double oz,
                                    double dx, double dy, double dz,
                                    double minX, double minY, double minZ,
                                    double maxX, double maxY, double maxZ) {
        // Leaving the box forward is entering it backward from far along the ray.
        final double reach = 1e4;
        final double back = rayBoxEntry(ox + dx * reach, oy + dy * reach, oz + dz * reach, -dx, -dy, -dz,
                minX, minY, minZ, maxX, maxY, maxZ);
        return Double.isNaN(back) ? Double.NaN : reach - back;
    }

    /** Returns the vector scaled to unit length, or null when it has no direction. */
    public static double[] normalize(double x, double y, double z) {
        final double length = Math.sqrt(x * x + y * y + z * z);
        if (!(length > 1e-6) || !Double.isFinite(length)) return null;
        return new double[] {x / length, y / length, z / length};
    }
}
