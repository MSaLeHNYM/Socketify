const jsonHeaders = { "Content-Type": "application/json" };

async function req(path, opts = {}) {
  const res = await fetch(path, {
    credentials: "include",
    ...opts,
    headers: { ...(opts.body instanceof FormData ? {} : jsonHeaders), ...opts.headers },
  });
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = { raw: text };
  }
  if (!res.ok) {
    const err = new Error(data?.error || res.statusText || "request failed");
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

export const api = {
  health: () => req("/api/health"),
  me: () => req("/api/auth/me"),
  register: (body) => req("/api/auth/register", { method: "POST", body: JSON.stringify(body) }),
  login: (body) => req("/api/auth/login", { method: "POST", body: JSON.stringify(body) }),
  logout: () => req("/api/auth/logout", { method: "POST", body: "{}" }),
  stats: () => req("/api/stats"),
  projects: () => req("/api/projects"),
  createProject: (body) => req("/api/projects", { method: "POST", body: JSON.stringify(body) }),
  project: (id) => req(`/api/projects/${id}`),
  deleteProject: (id) => req(`/api/projects/${id}`, { method: "DELETE" }),
  tasks: (id, q = "") => req(`/api/projects/${id}/tasks${q}`),
  createTask: (id, body) =>
    req(`/api/projects/${id}/tasks`, { method: "POST", body: JSON.stringify(body) }),
  patchTask: (id, body) =>
    req(`/api/tasks/${id}`, { method: "PATCH", body: JSON.stringify(body) }),
  deleteTask: (id) => req(`/api/tasks/${id}`, { method: "DELETE" }),
  comments: (id) => req(`/api/tasks/${id}/comments`),
  addComment: (id, body) =>
    req(`/api/tasks/${id}/comments`, { method: "POST", body: JSON.stringify(body) }),
  updateProfile: (body) =>
    req("/api/profile", { method: "PATCH", body: JSON.stringify(body) }),
  uploadAvatar: (file) => {
    const fd = new FormData();
    fd.append("avatar", file);
    return req("/api/profile/avatar", { method: "POST", body: fd, headers: {} });
  },
};
