package dev.hytalevr.plugin;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.function.BiConsumer;
import java.util.function.Consumer;

/**
 * Line-based TCP link to the hytale-vr dashboard, bound to 127.0.0.1 so only
 * programs on this PC can reach it. One dashboard is served at a time; a new
 * connection replaces the old one.
 */
final class BridgeServer implements AutoCloseable {
    static final int PORT = 47823;
    private static final int MAX_LINE = 4096;

    /** The live connection to one dashboard. */
    static final class Client {
        private final Socket socket;
        private final OutputStream out;

        Client(Socket socket) throws IOException {
            this.socket = socket;
            this.out = socket.getOutputStream();
        }

        /** Sends one line; returns false once the dashboard is gone. */
        synchronized boolean send(String line) {
            try {
                out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
                out.flush();
                return true;
            } catch (IOException e) {
                closeQuietly();
                return false;
            }
        }

        /** Sends raw bytes (already framed by the caller). */
        synchronized boolean sendRaw(byte[] bytes) {
            try {
                out.write(bytes);
                out.flush();
                return true;
            } catch (IOException e) {
                closeQuietly();
                return false;
            }
        }

        boolean isOpen() {
            return !socket.isClosed();
        }

        void closeQuietly() {
            try {
                socket.close();
            } catch (IOException ignored) {
                // Already closed.
            }
        }
    }

    private final BiConsumer<Client, String> onLine;
    private final Consumer<Client> onConnect;
    private final Consumer<String> log;
    private final int port;
    private volatile ServerSocket listener;
    private volatile Client current;
    private volatile boolean running = true;

    BridgeServer(int port, Consumer<Client> onConnect, BiConsumer<Client, String> onLine, Consumer<String> log) {
        this.port = port;
        this.onConnect = onConnect;
        this.onLine = onLine;
        this.log = log;
    }

    void start() {
        final Thread thread = new Thread(this::acceptLoop, "HytaleVR bridge");
        thread.setDaemon(true);
        thread.start();
    }

    Client current() {
        final Client client = current;
        return client != null && client.isOpen() ? client : null;
    }

    private void acceptLoop() {
        while (running) {
            try (ServerSocket server = new ServerSocket()) {
                server.setReuseAddress(true);
                server.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), port));
                listener = server;
                log.accept("Listening for the VR dashboard on 127.0.0.1:" + port);
                while (running) {
                    final Socket socket = server.accept();
                    socket.setTcpNoDelay(true);
                    final Client previous = current;
                    if (previous != null) previous.closeQuietly();
                    final Client client = new Client(socket);
                    current = client;
                    final Thread reader = new Thread(() -> readLoop(client, socket), "HytaleVR bridge client");
                    reader.setDaemon(true);
                    reader.start();
                }
            } catch (IOException e) {
                if (!running) return;
                // Another world on this PC may hold the port; try again shortly.
                log.accept("VR bridge unavailable (" + e.getMessage() + "), retrying");
                try {
                    Thread.sleep(5000);
                } catch (InterruptedException interrupted) {
                    return;
                }
            }
        }
    }

    private void readLoop(Client client, Socket socket) {
        try (BufferedReader in = new BufferedReader(
                new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8))) {
            onConnect.accept(client);
            String line;
            while (running && (line = in.readLine()) != null) {
                if (line.length() > MAX_LINE) break;
                onLine.accept(client, line);
            }
        } catch (IOException ignored) {
            // Dashboard went away.
        } finally {
            client.closeQuietly();
        }
    }

    @Override
    public void close() {
        running = false;
        final Client client = current;
        if (client != null) client.closeQuietly();
        final ServerSocket server = listener;
        if (server != null) {
            try {
                server.close();
            } catch (IOException ignored) {
                // Already closed.
            }
        }
    }
}
