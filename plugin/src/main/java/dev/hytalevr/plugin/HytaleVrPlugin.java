package dev.hytalevr.plugin;

import com.hypixel.hytale.server.core.modules.singleplayer.SingleplayerModule;
import com.hypixel.hytale.server.core.plugin.JavaPlugin;
import com.hypixel.hytale.server.core.plugin.JavaPluginInit;
import com.hypixel.hytale.server.core.universe.PlayerRef;
import com.hypixel.hytale.server.core.universe.Universe;
import com.hypixel.hytale.server.core.universe.world.World;
import java.util.Collection;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;

/**
 * Server half of hytale-vr. The dashboard on the same PC connects over
 * 127.0.0.1 and reports VR swings; this plugin resolves them along the hand's
 * own ray with Hytale's block and damage rules.
 */
public final class HytaleVrPlugin extends JavaPlugin {
    private BridgeServer bridge;
    private final WorldStreamer streamer = new WorldStreamer();
    private ScheduledExecutorService ticker;

    public HytaleVrPlugin(JavaPluginInit init) {
        super(init);
    }

    @Override
    protected void setup() {
        bridge = new BridgeServer(BridgeServer.PORT, this::onConnect, this::onLine, this::info);
        bridge.start();
        ticker = Executors.newSingleThreadScheduledExecutor(runnable -> {
            final Thread thread = new Thread(runnable, "HytaleVR streamer");
            thread.setDaemon(true);
            return thread;
        });
        ticker.scheduleAtFixedRate(() -> {
            try {
                streamer.tick();
            } catch (RuntimeException e) {
                getLogger().at(Level.WARNING).withCause(e).log("VR world stream failed");
            }
        }, 50, 50, TimeUnit.MILLISECONDS);
    }

    @Override
    protected void shutdown() {
        if (ticker != null) ticker.shutdownNow();
        if (bridge != null) bridge.close();
    }

    private void info(String message) {
        getLogger().at(Level.INFO).log(message);
    }

    private void onConnect(BridgeServer.Client client) {
        client.send("HELLO hytale-vr-plugin 1");
    }

    private void onLine(BridgeServer.Client client, String line) {
        if (line.startsWith("HELLO ")) {
            final String[] parts = line.split(" ");
            if (parts.length < 2) return;
            client.role = parts[1];
            info("VR " + (WorldStreamer.VIEWER_ROLE.equals(client.role) ? "viewer" : "dashboard") + " connected");
            if (WorldStreamer.VIEWER_ROLE.equals(client.role)) streamer.addViewer(client);
            return;
        }
        if (line.startsWith("SWING ")) {
            final SwingMessage swing = SwingMessage.parse(line);
            if (swing == null) return;
            final PlayerRef player = vrPlayer();
            final World world = player != null ? Universe.get().getWorld(player.getWorldUuid()) : null;
            if (world == null) {
                client.send("HIT noplayer");
                return;
            }
            world.execute(() -> {
                String result;
                try {
                    result = SwingHandler.handle(player, world, swing);
                } catch (RuntimeException e) {
                    getLogger().at(Level.WARNING).withCause(e).log("VR swing failed");
                    result = "error";
                }
                client.send("HIT " + result);
            });
        }
    }

    /**
     * The player wearing the headset: the singleplayer world's owner, or the
     * only player online when the plugin runs on a local dedicated server.
     */
    static PlayerRef vrPlayer() {
        final Universe universe = Universe.get();
        if (universe == null) return null;
        try {
            final UUID owner = SingleplayerModule.getUuid();
            if (owner != null) {
                final PlayerRef player = universe.getPlayer(owner);
                if (player != null) return player;
            }
        } catch (RuntimeException ignored) {
            // Not singleplayer.
        }
        final Collection<PlayerRef> players = universe.getPlayers();
        return players.size() == 1 ? players.iterator().next() : null;
    }
}
