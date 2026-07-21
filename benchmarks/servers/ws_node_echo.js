// Node.js `ws` echo server — popular raw WebSocket stack for comparison.
const http = require("http");
const { WebSocketServer } = require("ws");

const port = Number(process.argv[2] || 19181);
const server = http.createServer((_req, res) => {
  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end("ok\n");
});

const wss = new WebSocketServer({ server, path: "/pong" });
wss.on("connection", (ws) => {
  ws.on("message", (data, isBinary) => {
    ws.send(data, { binary: isBinary });
  });
});

server.listen(port, "127.0.0.1", () => {
  process.stderr.write(`node ws echo on ${port} (/pong)\n`);
});
