package dev.hytalevr.plugin;

import com.hypixel.hytale.component.Ref;
import com.hypixel.hytale.component.Store;
import com.hypixel.hytale.math.shape.Box;
import com.hypixel.hytale.math.vector.Transform;
import com.hypixel.hytale.server.core.asset.type.blocktype.config.BlockType;
import com.hypixel.hytale.server.core.asset.type.item.config.Item;
import com.hypixel.hytale.server.core.asset.type.item.config.ItemTool;
import com.hypixel.hytale.server.core.inventory.InventoryComponent;
import com.hypixel.hytale.server.core.inventory.ItemStack;
import com.hypixel.hytale.server.core.modules.entity.component.BoundingBox;
import com.hypixel.hytale.server.core.modules.entity.component.TransformComponent;
import com.hypixel.hytale.server.core.modules.entity.damage.Damage;
import com.hypixel.hytale.server.core.modules.entity.damage.DamageCause;
import com.hypixel.hytale.server.core.modules.entity.damage.DamageSystems;
import com.hypixel.hytale.server.core.modules.interaction.BlockHarvestUtils;
import com.hypixel.hytale.server.core.universe.PlayerRef;
import com.hypixel.hytale.server.core.universe.world.World;
import com.hypixel.hytale.server.core.universe.world.storage.ChunkStore;
import com.hypixel.hytale.server.core.universe.world.storage.EntityStore;
import com.hypixel.hytale.server.core.util.TargetUtil;
import java.util.List;
import org.joml.Vector3d;
import org.joml.Vector3i;

/**
 * Resolves a VR swing on the world thread: finds the first mob or block along
 * the hand's ray and hits it the way a click would, with the held item.
 */
final class SwingHandler {
    /** How far a swing reaches from the hand, in blocks. */
    static final double REACH = 4.5;
    /** A mob this close to the hand is hit whichever way the hand points: the blade sweeps through it. */
    static final double MELEE = 1.6;

    private SwingHandler() {}

    /** Must run on the player's world thread. Returns "block", "entity", "none" or an error word. */
    static String handle(PlayerRef playerRef, World world, SwingMessage swing) {
        final Ref<EntityStore> player = playerRef.getReference();
        if (player == null || !player.isValid()) return "noplayer";
        final Store<EntityStore> store = world.getEntityStore().getStore();

        final Transform look = TargetUtil.getLook(player, store);
        if (look == null) return "nolook";
        final Vector3d eye = look.getPosition();
        final Vector3d serverLookVector = look.getDirection();
        final double[] serverLook = RayMath.normalize(serverLookVector.x, serverLookVector.y, serverLookVector.z);
        if (serverLook == null) return "nolook";
        final AxisMapping mapping = AxisMapping.find(swing.look(), serverLook);
        if (mapping == null) return "axes";

        final double[] offset = mapping.apply(swing.origin());
        final double[] direction = mapping.apply(swing.direction());
        final double ox = eye.x + offset[0];
        final double oy = eye.y + offset[1];
        final double oz = eye.z + offset[2];

        final ItemStack held = heldItem(player, store);
        // Weapons go through Hytale's own attack (its damage, knockback and
        // combos), which a plugin can't replay: the client clicks instead.
        final Item heldItem = held != null ? held.getItem() : null;
        if (heldItem != null && heldItem.getWeapon() != null) return "click";

        // Mobs and other players: the nearest bounding box the ray enters.
        Ref<EntityStore> entityHit = null;
        double entityDistance = Double.POSITIVE_INFINITY;
        final Vector3d middle = new Vector3d(
                ox + direction[0] * REACH * 0.5, oy + direction[1] * REACH * 0.5, oz + direction[2] * REACH * 0.5);
        final List<Ref<EntityStore>> nearby = TargetUtil.getAllEntitiesInSphere(middle, REACH * 0.5 + 2.0, store);
        for (Ref<EntityStore> candidate : nearby) {
            if (candidate == null || !candidate.isValid() || candidate.getIndex() == player.getIndex()) continue;
            final TransformComponent transform = store.getComponent(candidate, TransformComponent.getComponentType());
            final BoundingBox bounds = store.getComponent(candidate, BoundingBox.getComponentType());
            if (transform == null || bounds == null) continue;
            final Vector3d position = transform.getPosition();
            final Box box = bounds.getBoundingBox();
            final double minX = position.x + box.min.x, minY = position.y + box.min.y, minZ = position.z + box.min.z;
            final double maxX = position.x + box.max.x, maxY = position.y + box.max.y, maxZ = position.z + box.max.z;
            double t = RayMath.rayBoxEntry(ox, oy, oz, direction[0], direction[1], direction[2],
                    minX, minY, minZ, maxX, maxY, maxZ);
            final double dx = Math.max(Math.max(minX - ox, 0), ox - maxX);
            final double dy = Math.max(Math.max(minY - oy, 0), oy - maxY);
            final double dz = Math.max(Math.max(minZ - oz, 0), oz - maxZ);
            final double near = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (near <= MELEE && (Double.isNaN(t) || near < t)) t = near;
            if (!Double.isNaN(t) && t <= REACH && t < entityDistance) {
                entityDistance = t;
                entityHit = candidate;
            }
        }

        // Blocks: Hytale's own ray march, stopping at the first non-empty block.
        final ChunkStore chunks = world.getChunkStore();
        final Vector3i blockHit = TargetUtil.getTargetBlock(chunks,
                (blockId, fluidId) -> blockId != BlockType.EMPTY_ID,
                ox, oy, oz, direction[0], direction[1], direction[2], REACH);
        double blockDistance = Double.POSITIVE_INFINITY;
        if (blockHit != null) {
            final double t = RayMath.rayBoxEntry(ox, oy, oz, direction[0], direction[1], direction[2],
                    blockHit.x, blockHit.y, blockHit.z, blockHit.x + 1, blockHit.y + 1, blockHit.z + 1);
            blockDistance = Double.isNaN(t) ? REACH : t;
        }

        if (entityHit != null && entityDistance <= blockDistance) {
            hitEntity(player, entityHit, held, store);
            return "entity";
        }
        if (entityHit != null && entityDistance <= MELEE) {
            hitEntity(player, entityHit, held, store);
            return "entity";
        }
        if (blockHit != null) {
            return hitBlock(player, blockHit, held, chunks, store) ? "block" : "blocked";
        }
        return "none";
    }

    /** How far Hytale's own interactions reach along the look, in blocks. */
    static final double AIM_REACH = 5.0;

    /**
     * What a click would reach along the player's own look: the distance to
     * the first mob or block, or "none". Must run on the player's world thread.
     */
    static String aim(PlayerRef playerRef, World world) {
        final Ref<EntityStore> player = playerRef.getReference();
        if (player == null || !player.isValid()) return "none";
        final Store<EntityStore> store = world.getEntityStore().getStore();
        final Transform look = TargetUtil.getLook(player, store);
        if (look == null) return "none";
        final Vector3d eye = look.getPosition();
        final Vector3d lookVector = look.getDirection();
        final double[] direction = RayMath.normalize(lookVector.x, lookVector.y, lookVector.z);
        if (direction == null) return "none";
        double best = Double.POSITIVE_INFINITY;

        final Vector3d middle = new Vector3d(eye.x + direction[0] * AIM_REACH * 0.5,
                eye.y + direction[1] * AIM_REACH * 0.5, eye.z + direction[2] * AIM_REACH * 0.5);
        for (Ref<EntityStore> candidate : TargetUtil.getAllEntitiesInSphere(middle, AIM_REACH * 0.5 + 2.0, store)) {
            if (candidate == null || !candidate.isValid() || candidate.getIndex() == player.getIndex()) continue;
            final TransformComponent transform = store.getComponent(candidate, TransformComponent.getComponentType());
            final BoundingBox bounds = store.getComponent(candidate, BoundingBox.getComponentType());
            if (transform == null || bounds == null) continue;
            final Vector3d position = transform.getPosition();
            final Box box = bounds.getBoundingBox();
            final double t = RayMath.rayBoxEntry(eye.x, eye.y, eye.z, direction[0], direction[1], direction[2],
                    position.x + box.min.x, position.y + box.min.y, position.z + box.min.z,
                    position.x + box.max.x, position.y + box.max.y, position.z + box.max.z);
            if (!Double.isNaN(t) && t <= AIM_REACH && t < best) best = t;
        }

        final Vector3i block = TargetUtil.getTargetBlock(world.getChunkStore(),
                (blockId, fluidId) -> blockId != BlockType.EMPTY_ID,
                eye.x, eye.y, eye.z, direction[0], direction[1], direction[2], AIM_REACH);
        if (block != null) {
            final double t = RayMath.rayBoxEntry(eye.x, eye.y, eye.z, direction[0], direction[1], direction[2],
                    block.x, block.y, block.z, block.x + 1, block.y + 1, block.z + 1);
            if (!Double.isNaN(t) && t < best) best = t;
        }
        return Double.isFinite(best) ? String.format(java.util.Locale.ROOT, "%.3f", best) : "none";
    }

    private static ItemStack heldItem(Ref<EntityStore> player, Store<EntityStore> store) {
        final InventoryComponent.Hotbar hotbar = store.getComponent(player, InventoryComponent.Hotbar.getComponentType());
        if (hotbar == null) return null;
        final ItemStack stack = hotbar.getActiveItem();
        return ItemStack.isEmpty(stack) ? null : stack;
    }

    private static boolean hitBlock(Ref<EntityStore> player, Vector3i target, ItemStack held,
                                    ChunkStore chunks, Store<EntityStore> store) {
        final Ref<ChunkStore> section = chunks.getChunkSectionReferenceAtBlock(target.x, target.y, target.z);
        if (section == null || !section.isValid()) return false;
        final Item item = held != null ? held.getItem() : null;
        final ItemTool tool = item != null ? item.getTool() : null;
        // Same arguments Hytale's own BreakBlock interaction uses for one hit.
        return BlockHarvestUtils.performBlockDamage(player, player, target, held, tool, null, false,
                1.0f, 0, false, section, store, chunks.getStore());
    }

    private static void hitEntity(Ref<EntityStore> player, Ref<EntityStore> target, ItemStack held,
                                  Store<EntityStore> store) {
        final int cause = DamageCause.getAssetMap().getIndex("Physical");
        if (cause < 0) return;
        final Damage damage = new Damage(new Damage.EntitySource(player), cause, swingDamage(held));
        DamageSystems.executeDamage(target, store, damage);
    }

    /**
     * Hytale computes weapon damage inside its attack interactions, which a
     * plugin cannot replay for an arbitrary target. Until it can, a swing deals
     * a flat amount by what is held.
     */
    private static float swingDamage(ItemStack held) {
        final Item item = held != null ? held.getItem() : null;
        if (item == null) return 1.0f;
        if (item.getWeapon() != null) return 5.0f;
        if (item.getTool() != null) return 3.0f;
        return 1.0f;
    }
}
