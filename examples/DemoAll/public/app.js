async function fetchJSON(url, opts = {}) {
  const res = await fetch(url, {
    headers: {
      "Accept": "application/json",
      "Content-Type": "application/json",
    },
    ...opts,
  });
  const text = await res.text();
  let data = text;
  try { data = JSON.parse(text); } catch {}
  return { status: res.status, ok: res.ok, headers: Object.fromEntries(res.headers.entries()), data };
}

document.addEventListener("DOMContentLoaded", () => {
  const helloBtn = document.getElementById("btn-hello");
  const helloOut = document.getElementById("out-hello");
  helloBtn.addEventListener("click", async () => {
    const r = await fetchJSON("/api/hello");
    helloOut.textContent = JSON.stringify(r, null, 2);
  });

  const echoBtn = document.getElementById("btn-echo");
  const echoInput = document.getElementById("echo-input");
  const echoOut = document.getElementById("out-echo");
  echoBtn.addEventListener("click", async () => {
    const r = await fetchJSON("/api/echo", { method: "POST", body: echoInput.value });
    echoOut.textContent = JSON.stringify(r, null, 2);
  });

  const ppBtn = document.getElementById("btn-pp");
  const ppOut = document.getElementById("out-pp");
  ppBtn.addEventListener("click", async () => {
    // GET on a POST-only route → should be 405 and include "Allow: POST"
    const res = await fetch("/api/pp", { method: "GET" });
    const text = await res.text();
    ppOut.textContent = `status: ${res.status}\nallow: ${res.headers.get("Allow")}\n---\n${text}`;
  });
});
