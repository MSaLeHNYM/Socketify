import React, { useEffect, useState } from "react";
import { Link, Navigate, Route, Routes, useNavigate, useParams } from "react-router-dom";
import { useAuth } from "./auth.jsx";
import { api } from "./api.js";

function Shell({ children }) {
  const { user, logout } = useAuth();
  const nav = useNavigate();
  return (
    <div className="shell">
      <header className="top">
        <Link to="/" className="brand">
          <span className="brand-mark" />
          Nexus
        </Link>
        <nav>
          {user ? (
            <>
              <Link to="/">Board</Link>
              <Link to="/profile">Profile</Link>
              <button
                className="ghost"
                onClick={async () => {
                  await logout();
                  nav("/login");
                }}
              >
                Log out
              </button>
              <span className="pill">{user.name}</span>
            </>
          ) : (
            <>
              <Link to="/login">Log in</Link>
              <Link className="btn" to="/register">
                Sign up
              </Link>
            </>
          )}
        </nav>
      </header>
      <main>{children}</main>
      <footer className="foot">
        Powered by <strong>Socketify</strong> · live SSE · SQLite · sessions
      </footer>
    </div>
  );
}

function Private({ children }) {
  const { user, loading } = useAuth();
  if (loading) return <div className="center muted">Loading…</div>;
  if (!user) return <Navigate to="/login" replace />;
  return children;
}

function AuthForm({ mode }) {
  const { login, register } = useAuth();
  const nav = useNavigate();
  const [email, setEmail] = useState("");
  const [name, setName] = useState("");
  const [password, setPassword] = useState("");
  const [err, setErr] = useState("");
  const [busy, setBusy] = useState(false);

  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    setErr("");
    try {
      if (mode === "login") await login(email, password);
      else await register(email, name, password);
      nav("/");
    } catch (ex) {
      setErr(ex.message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Shell>
      <section className="hero-auth">
        <div className="hero-copy">
          <p className="eyebrow">Socketify × React</p>
          <h1>Ship work together — live.</h1>
          <p>
            Nexus Board is a full-stack sample: SQLite persistence, session auth, rate limits,
            multipart uploads, and Server-Sent Events — all on Socketify.
          </p>
        </div>
        <form className="card-form" onSubmit={submit}>
          <h2>{mode === "login" ? "Welcome back" : "Create account"}</h2>
          {mode === "register" && (
            <label>
              Name
              <input value={name} onChange={(e) => setName(e.target.value)} required />
            </label>
          )}
          <label>
            Email
            <input type="email" value={email} onChange={(e) => setEmail(e.target.value)} required />
          </label>
          <label>
            Password
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              required
              minLength={6}
            />
          </label>
          {err && <p className="error">{err}</p>}
          <button className="btn wide" disabled={busy}>
            {busy ? "…" : mode === "login" ? "Log in" : "Sign up"}
          </button>
        </form>
      </section>
    </Shell>
  );
}

function Dashboard() {
  const [projects, setProjects] = useState([]);
  const [stats, setStats] = useState(null);
  const [name, setName] = useState("");
  const [desc, setDesc] = useState("");
  const [live, setLive] = useState("");
  const nav = useNavigate();

  const load = async () => {
    const [p, s] = await Promise.all([api.projects(), api.stats()]);
    setProjects(p.projects);
    setStats(s);
  };

  useEffect(() => {
    load().catch(console.error);
    const es = new EventSource("/api/events", { withCredentials: true });
    const bump = (ev) => {
      setLive(`${ev.type} @ ${new Date().toLocaleTimeString()}`);
      load().catch(() => {});
    };
    es.addEventListener("project", bump);
    es.addEventListener("task", bump);
    es.addEventListener("hello", () => setLive("SSE connected"));
    return () => es.close();
  }, []);

  const create = async (e) => {
    e.preventDefault();
    await api.createProject({ name, description: desc });
    setName("");
    setDesc("");
    await load();
  };

  return (
    <Shell>
      <section className="dash-head">
        <div>
          <p className="eyebrow">Workspace</p>
          <h1>Projects</h1>
          <p className="muted">Live feed: {live || "connecting…"}</p>
        </div>
        {stats && (
          <div className="stat-grid">
            <div><strong>{stats.projects}</strong><span>projects</span></div>
            <div><strong>{stats.tasks}</strong><span>tasks</span></div>
            <div><strong>{stats.todo || 0}</strong><span>todo</span></div>
            <div><strong>{stats.done || 0}</strong><span>done</span></div>
          </div>
        )}
      </section>

      <form className="inline-form" onSubmit={create}>
        <input placeholder="New project name" value={name} onChange={(e) => setName(e.target.value)} required />
        <input placeholder="Description (optional)" value={desc} onChange={(e) => setDesc(e.target.value)} />
        <button className="btn">Create</button>
        <a className="ghost" href="/api/export/projects.json">
          Export JSON
        </a>
      </form>

      <div className="project-grid">
        {projects.map((p) => (
          <button key={p.id} className="project-card" onClick={() => nav(`/projects/${p.id}`)}>
            <h3>{p.name}</h3>
            <p>{p.description || "No description"}</p>
            <span className="muted">#{p.id}</span>
          </button>
        ))}
        {!projects.length && <p className="muted">No projects yet — create one above.</p>}
      </div>
    </Shell>
  );
}

function ProjectBoard() {
  const { id } = useParams();
  const [project, setProject] = useState(null);
  const [tasks, setTasks] = useState([]);
  const [q, setQ] = useState("");
  const [title, setTitle] = useState("");
  const [selected, setSelected] = useState(null);
  const [comments, setComments] = useState([]);
  const [comment, setComment] = useState("");
  const nav = useNavigate();

  const load = async () => {
    const p = await api.project(id);
    setProject(p.project);
    const query = q ? `?q=${encodeURIComponent(q)}` : "";
    const t = await api.tasks(id, query);
    setTasks(t.tasks);
  };

  useEffect(() => {
    load().catch(console.error);
    const es = new EventSource("/api/events", { withCredentials: true });
    const onTask = () => load().catch(() => {});
    es.addEventListener("task", onTask);
    es.addEventListener("comment", async (ev) => {
      const data = JSON.parse(ev.data);
      if (selected && data.comment?.task_id === selected.id) {
        const c = await api.comments(selected.id);
        setComments(c.comments);
      }
      onTask();
    });
    return () => es.close();
  }, [id, q, selected?.id]);

  const columns = ["todo", "doing", "done"];

  const openTask = async (t) => {
    setSelected(t);
    const c = await api.comments(t.id);
    setComments(c.comments);
  };

  return (
    <Shell>
      <section className="dash-head">
        <div>
          <button className="ghost" onClick={() => nav("/")}>← Back</button>
          <h1>{project?.name || "…"}</h1>
          <p className="muted">{project?.description}</p>
        </div>
        <button
          className="ghost danger"
          onClick={async () => {
            if (confirm("Delete this project?")) {
              await api.deleteProject(id);
              nav("/");
            }
          }}
        >
          Delete project
        </button>
      </section>

      <form
        className="inline-form"
        onSubmit={async (e) => {
          e.preventDefault();
          await api.createTask(id, { title, status: "todo", priority: 1 });
          setTitle("");
          await load();
        }}
      >
        <input placeholder="Add a task…" value={title} onChange={(e) => setTitle(e.target.value)} required />
        <input placeholder="Search" value={q} onChange={(e) => setQ(e.target.value)} />
        <button className="btn">Add</button>
      </form>

      <div className="board">
        {columns.map((col) => (
          <div key={col} className="column">
            <h3>{col}</h3>
            {tasks
              .filter((t) => t.status === col)
              .map((t) => (
                <article key={t.id} className="task" onClick={() => openTask(t)}>
                  <strong>{t.title}</strong>
                  <p>{t.body || "—"}</p>
                  <div className="task-actions">
                    {columns
                      .filter((c) => c !== t.status)
                      .map((c) => (
                        <button
                          key={c}
                          className="mini"
                          onClick={async (e) => {
                            e.stopPropagation();
                            await api.patchTask(t.id, { status: c });
                            await load();
                          }}
                        >
                          → {c}
                        </button>
                      ))}
                  </div>
                </article>
              ))}
          </div>
        ))}
      </div>

      {selected && (
        <aside className="drawer">
          <button className="ghost" onClick={() => setSelected(null)}>
            Close
          </button>
          <h2>{selected.title}</h2>
          <p>{selected.body || "No details"}</p>
          <form
            onSubmit={async (e) => {
              e.preventDefault();
              await api.addComment(selected.id, { body: comment });
              setComment("");
              const c = await api.comments(selected.id);
              setComments(c.comments);
            }}
          >
            <textarea
              rows={3}
              placeholder="Write a comment…"
              value={comment}
              onChange={(e) => setComment(e.target.value)}
              required
            />
            <button className="btn">Comment</button>
          </form>
          <ul className="comments">
            {comments.map((c) => (
              <li key={c.id}>
                <strong>{c.user_name}</strong>
                <span className="muted"> {c.created_at}</span>
                <p>{c.body}</p>
              </li>
            ))}
          </ul>
          <button
            className="ghost danger"
            onClick={async () => {
              await api.deleteTask(selected.id);
              setSelected(null);
              await load();
            }}
          >
            Delete task
          </button>
        </aside>
      )}
    </Shell>
  );
}

function Profile() {
  const { user, refresh } = useAuth();
  const [name, setName] = useState(user?.name || "");
  const [msg, setMsg] = useState("");

  return (
    <Shell>
      <section className="profile">
        <h1>Profile</h1>
        {user?.avatar_path && <img className="avatar" src={user.avatar_path} alt="" />}
        <form
          onSubmit={async (e) => {
            e.preventDefault();
            await api.updateProfile({ name });
            await refresh();
            setMsg("Saved");
          }}
        >
          <label>
            Display name
            <input value={name} onChange={(e) => setName(e.target.value)} />
          </label>
          <label>
            Avatar
            <input
              type="file"
              accept="image/*"
              onChange={async (e) => {
                const f = e.target.files?.[0];
                if (!f) return;
                await api.uploadAvatar(f);
                await refresh();
                setMsg("Avatar updated");
              }}
            />
          </label>
          <button className="btn">Save</button>
          {msg && <p className="ok">{msg}</p>}
        </form>
        <p className="muted">{user?.email}</p>
      </section>
    </Shell>
  );
}

export default function App() {
  return (
    <Routes>
      <Route path="/login" element={<AuthForm mode="login" />} />
      <Route path="/register" element={<AuthForm mode="register" />} />
      <Route
        path="/"
        element={
          <Private>
            <Dashboard />
          </Private>
        }
      />
      <Route
        path="/projects/:id"
        element={
          <Private>
            <ProjectBoard />
          </Private>
        }
      />
      <Route
        path="/profile"
        element={
          <Private>
            <Profile />
          </Private>
        }
      />
    </Routes>
  );
}
