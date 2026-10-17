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
 * Line-based TCP link to the hytale-vr dashboard and VR viewer, bound to
 * 127.0.0.1 so only programs on this PC can reach it. Each connection says
 * what it is in its HELLO line.
 */
final class BridgeServer implements AutoCloseable {
    static final int PORT = 47823;
    private static final int MAX_LINE = 4096;
    private static final int MAX_CLIENTS = 8;

    /** One live connection. Sends are queued and written by the client's own thread. */
    static final class Client {
        /** Lines queued beyond this mean the reader has stalled; the link is dropped. */
        private static final int MAX_QUEUED = 16384;
        private static final byte[] CLOSE = new byte[0];

        private final Socket socket;
        private final OutputStream out;
        private final java.util.concurrent.BlockingQueue<byte[]> queue =
                new java.util.concurrent.LinkedBlockingQueue<>(MAX_QUEUED);
        /** Set from the HELLO line: "hytale-vr" (dashboard) or "hytale-vr-viewer". */
        volatile String role = "";

        Client(Socket socket) throws IOException {
            this.socket = socket;
            this.out = new java.io.BufferedOutputStream(socket.getOutputStream(), 65536);
            final Thread writer = new Thread(this::writeLoop, "HytaleVR bridge writer");
            writer.setDaemon(true);
            writer.start();
        }

        /** Queues one line without blocking; returns false once the link is gone. */
        boolean send(String line) {
            if (socket.isClosed()) return false;
            if (!queue.offer((line + "\n").getBytes(StandardCharsets.UTF_8))) {
                closeQuietly();
                return false;
            }
            return true;
        }

        private void writeLoop() {
            try {
                while (true) {
                    byte[] bytes = queue.take();
                    if (bytes == CLOSE) return;
                    out.write(bytes);
                    // Batch whatever else is already waiting before flushing.
                    while ((bytes = queue.poll()) != null) {
                        if (bytes == CLOSE) return;
                        out.write(bytes);
                    }
                    out.flush();
                }
            } catch (IOException | InterruptedException e) {
                closeQuietly();
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
            queue.clear();
            queue.offer(CLOSE);
        }
    }

    private final BiConsumer<Client, String> onLine;
    private final Consumer<Client> onConnect;
    private final Consumer<String> log;
    private final int port;
    private volatile ServerSocket listener;
    private final java.util.concurrent.CopyOnWriteArrayList<Client> clients =
            new java.util.concurrent.CopyOnWriteArrayList<>();
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

    /** Open connections that introduced themselves with the given role. */
    java.util.List<Client> clients(String role) {
        final java.util.List<Client> result = new java.util.ArrayList<>();
        for (Client client : clients) {
            if (client.isOpen() && role.equals(client.role)) result.add(client);
        }
        return result;
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
                    if (clients.size() >= MAX_CLIENTS) {
                        socket.close();
                        continue;
                    }
                    final Client client = new Client(socket);
                    clients.add(client);
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
            clients.remove(client);
        }
    }

    @Override
    public void close() {
        running = false;
        for (Client client : clients) client.closeQuietly();
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
