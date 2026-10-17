package dev.hytalevr.plugin;

import com.hypixel.hytale.component.Ref;
import com.hypixel.hytale.component.Store;
import com.hypixel.hytale.math.shape.Box;
import com.hypixel.hytale.math.vector.Transform;
import com.hypixel.hytale.protocol.Color;
import com.hypixel.hytale.server.core.asset.type.blocktype.config.BlockType;
import com.hypixel.hytale.server.core.asset.type.blocktype.config.BlockTypeTextures;
import com.hypixel.hytale.server.core.modules.entity.component.BoundingBox;
import com.hypixel.hytale.server.core.modules.entity.component.ModelComponent;
import com.hypixel.hytale.server.core.modules.entity.component.TransformComponent;
import com.hypixel.hytale.server.core.modules.entity.tracker.NetworkId;
import com.hypixel.hytale.server.core.universe.PlayerRef;
import com.hypixel.hytale.server.core.universe.Universe;
import com.hypixel.hytale.server.core.universe.world.World;
import com.hypixel.hytale.server.core.universe.world.chunk.section.BlockSection;
import com.hypixel.hytale.server.core.universe.world.storage.ChunkStore;
import com.hypixel.hytale.server.core.universe.world.storage.EntityStore;
import com.hypixel.hytale.server.core.util.TargetUtil;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import org.joml.Vector3d;

/**
 * Streams the world around the VR player to connected viewers: the player's
 * position and look every tick, nearby block sections when they change, the
 * block types those sections use, and nearby entities as boxes.
 *
 * <p>Lines sent (all text, one per message):
 * <pre>
 * WORLD name
 * PLAYER fx fy fz ex ey ez dx dy dz          feet, eye, look direction
 * BLOCK id|key|drawType|opacity|r,g,b|up|down|north|south|east|west
 * SECTION sx sy sz base64                    see SectionEncoder
 * ENTITIES id,x,y,z,minX,minY,minZ,maxX,maxY,maxZ,yaw,model;...
 * </pre>
 */
final class WorldStreamer {
    static final String VIEWER_ROLE = "hytale-vr-viewer";
    /** Horizontal radius in 32-block sections around the player. */
    static final int RADIUS = 3;
    /** Sections streamed above and below the player's own. */
    static final int VERTICAL = 2;
    private static final int SECTIONS_PER_TICK = 6;
    /** Every this many ticks, nearby sections are re-read in case a change slipped past the counter. */
    private static final int RECHECK_TICKS = 20;
    private static final double ENTITY_RADIUS = 40.0;

    private static final class ViewerState {
        final Map<Long, Short> sentCounters = new HashMap<>();
        final Map<Long, Integer> sentHashes = new HashMap<>();
        final Set<Integer> sentBlockTypes = new HashSet<>();
        String world = "";
    }

    private final Map<BridgeServer.Client, ViewerState> viewers = new ConcurrentHashMap<>();
    private final AtomicBoolean pending = new AtomicBoolean();
    private int tick;

    void addViewer(BridgeServer.Client client) {
        viewers.put(client, new ViewerState());
    }

    /** Called from a timer thread; does the work on the player's world thread. */
    void tick() {
        viewers.keySet().removeIf(client -> !client.isOpen());
        if (viewers.isEmpty() || pending.get()) return;
        final PlayerRef player = HytaleVrPlugin.vrPlayer();
        if (player == null) return;
        final World world = Universe.get().getWorld(player.getWorldUuid());
        if (world == null) return;
        pending.set(true);
        world.execute(() -> {
            try {
                stream(player, world);
            } finally {
                pending.set(false);
            }
        });
    }

    private void stream(PlayerRef playerRef, World world) {
        final Ref<EntityStore> player = playerRef.getReference();
        if (player == null || !player.isValid()) return;
        final Store<EntityStore> store = world.getEntityStore().getStore();
        final TransformComponent transform = store.getComponent(player, TransformComponent.getComponentType());
        final Transform look = TargetUtil.getLook(player, store);
        if (transform == null || look == null) return;
        final Vector3d feet = transform.getPosition();
        final Vector3d eye = look.getPosition();
        final Vector3d direction = look.getDirection();
        tick++;

        final String playerLine = String.format(Locale.ROOT, "PLAYER %.4f %.4f %.4f %.4f %.4f %.4f %.5f %.5f %.5f",
                feet.x, feet.y, feet.z, eye.x, eye.y, eye.z, direction.x, direction.y, direction.z);
        final String entityLine = tick % 3 == 0 ? entitiesLine(player, feet, store) : null;
        final List<long[]> order = sectionsByDistance(feet);
        final ChunkStore chunks = world.getChunkStore();
        final boolean recheck = tick % RECHECK_TICKS == 0;

        for (Map.Entry<BridgeServer.Client, ViewerState> entry : viewers.entrySet()) {
            final BridgeServer.Client client = entry.getKey();
            final ViewerState state = entry.getValue();
            if (!state.world.equals(world.getName())) {
                // New world: the viewer drops everything it had.
                state.world = world.getName();
                state.sentCounters.clear();
                state.sentHashes.clear();
                client.send("WORLD " + world.getName().replace(' ', '_'));
            }
            client.send(playerLine);
            if (entityLine != null) client.send(entityLine);
            streamSections(client, state, chunks, order, recheck);
        }
    }

    private static List<long[]> sectionsByDistance(Vector3d feet) {
        final int cx = (int) Math.floor(feet.x) >> 5;
        final int cy = (int) Math.floor(feet.y) >> 5;
        final int cz = (int) Math.floor(feet.z) >> 5;
        final List<long[]> order = new ArrayList<>();
        for (int dx = -RADIUS; dx <= RADIUS; dx++) {
            for (int dz = -RADIUS; dz <= RADIUS; dz++) {
                for (int dy = -VERTICAL; dy <= VERTICAL; dy++) {
                    final long distance = (long) dx * dx + (long) dz * dz + 4L * dy * dy;
                    order.add(new long[] {cx + dx, cy + dy, cz + dz, distance});
                }
            }
        }
        order.sort((a, b) -> Long.compare(a[3], b[3]));
        return order;
    }

    private void streamSections(BridgeServer.Client client, ViewerState state, ChunkStore chunks,
                                List<long[]> order, boolean recheck) {
        final Store<ChunkStore> chunkStore = chunks.getStore();
        // Forget sections that left the radius so they are sent again on return;
        // the viewer drops them from its own copy too.
        final Set<Long> inRange = new HashSet<>();
        for (long[] coords : order) inRange.add(sectionKey((int) coords[0], (int) coords[1], (int) coords[2]));
        state.sentCounters.keySet().retainAll(inRange);
        state.sentHashes.keySet().retainAll(inRange);
        int budget = SECTIONS_PER_TICK;
        int index = 0;
        for (long[] coords : order) {
            final boolean near = index++ < 27;
            final int sx = (int) coords[0];
            final int sy = (int) coords[1];
            final int sz = (int) coords[2];
            final Ref<ChunkStore> ref = chunks.getChunkSectionReference(sx, sy, sz);
            if (ref == null || !ref.isValid()) continue;
            final BlockSection section = chunkStore.getComponent(ref, BlockSection.getComponentType());
            if (section == null) continue;
            final long key = sectionKey(sx, sy, sz);
            final short counter = section.getLocalChangeCounter();
            final Short sentCounter = state.sentCounters.get(key);
            final boolean changed = sentCounter == null || sentCounter != counter;
            if (!changed && !(recheck && near)) continue;
            if (budget <= 0 && changed) break;

            final int[] ids = new int[SectionEncoder.CELLS];
            if (!section.isSolidAir()) {
                for (int i = 0; i < ids.length; i++) ids[i] = section.get(i);
            }
            final String encoded = SectionEncoder.encode(ids);
            final int hash = encoded.hashCode();
            state.sentCounters.put(key, counter);
            final Integer sentHash = state.sentHashes.get(key);
            if (sentHash != null && sentHash == hash) continue;
            state.sentHashes.put(key, hash);
            for (int id : ids) {
                if (state.sentBlockTypes.add(id)) client.send(blockLine(id));
            }
            client.send("SECTION " + sx + " " + sy + " " + sz + " " + encoded);
            budget--;
        }
    }

    private static long sectionKey(int sx, int sy, int sz) {
        return ((long) (sx & 0x1FFFFF) << 42) | ((long) (sy & 0x1FFFFF) << 21) | (sz & 0x1FFFFF);
    }

    static String blockLine(int id) {
        final BlockType type = BlockType.getAssetMap().getAsset(id);
        if (type == null) return "BLOCK " + id + "|unknown|Empty|Transparent|0,0,0||||||";
        final StringBuilder line = new StringBuilder("BLOCK ").append(id).append('|')
                .append(clean(type.getId())).append('|')
                .append(type.getDrawType() != null ? type.getDrawType().name() : "Empty").append('|')
                .append(type.getOpacity() != null ? type.getOpacity().name() : "Solid").append('|');
        final Color color = type.getTextureComputedColor();
        if (color != null) {
            line.append(color.red & 0xFF).append(',').append(color.green & 0xFF).append(',').append(color.blue & 0xFF);
        } else {
            line.append("128,128,128");
        }
        final BlockTypeTextures[] textures = type.getTextures();
        final BlockTypeTextures first = textures != null && textures.length > 0 ? textures[0] : null;
        final String[] faces = first == null ? new String[6] : new String[] {
                first.getUp(), first.getDown(), first.getNorth(), first.getSouth(), first.getEast(), first.getWest()};
        for (String face : faces) line.append('|').append(clean(face));
        return line.toString();
    }

    private static String clean(String text) {
        return text == null ? "" : text.replace('|', '_').replace('\n', ' ').replace('\r', ' ');
    }

    private static String entitiesLine(Ref<EntityStore> player, Vector3d feet, Store<EntityStore> store) {
        final StringBuilder line = new StringBuilder("ENTITIES ");
        boolean first = true;
        for (Ref<EntityStore> entity : TargetUtil.getAllEntitiesInSphere(feet, ENTITY_RADIUS, store)) {
            if (entity == null || !entity.isValid() || entity.getIndex() == player.getIndex()) continue;
            final TransformComponent transform = store.getComponent(entity, TransformComponent.getComponentType());
            final BoundingBox bounds = store.getComponent(entity, BoundingBox.getComponentType());
            if (transform == null || bounds == null) continue;
            final NetworkId networkId = store.getComponent(entity, NetworkId.getComponentType());
            final ModelComponent model = store.getComponent(entity, ModelComponent.getComponentType());
            final Vector3d position = transform.getPosition();
            final Box box = bounds.getBoundingBox();
            final float yaw = transform.getRotation() != null ? transform.getRotation().yaw() : 0f;
            final String modelId = model != null && model.getModel() != null
                    ? clean(model.getModel().getModelAssetId()).replace(',', '_').replace(';', '_').replace(' ', '_')
                    : "";
            if (!first) line.append(';');
            first = false;
            line.append(networkId != null ? networkId.getId() : -entity.getIndex() - 1).append(',')
                    .append(String.format(Locale.ROOT, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                            position.x, position.y, position.z,
                            box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z, yaw))
                    .append(',').append(modelId);
        }
        return line.toString();
    }
}
