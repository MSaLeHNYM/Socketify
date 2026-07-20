const express = require("express");
const app = express();
const port = Number(process.argv[2] || 19082);

app.get("/ping", (_req, res) => {
  res.json({ ok: true });
});

app.listen(port, "127.0.0.1", () => {
  console.error(`express ping on ${port}`);
});
