import React, { createContext, useContext, useEffect, useState } from "react";
import { api } from "./api.js";

const AuthCtx = createContext(null);

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  const refresh = async () => {
    try {
      const data = await api.me();
      setUser(data.user);
    } catch (err) {
      // Only clear the session on a real auth failure. Network blips, 429s,
      // and 5xx must not boot the user to the login screen.
      if (err?.status === 401) setUser(null);
      else if (loading) setUser(null);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    refresh();
  }, []);

  const value = {
    user,
    loading,
    setUser,
    refresh,
    async login(email, password) {
      const data = await api.login({ email, password });
      setUser(data.user);
      return data.user;
    },
    async register(email, name, password) {
      const data = await api.register({ email, name, password });
      setUser(data.user);
      return data.user;
    },
    async logout() {
      try {
        await api.logout();
      } catch {
        /* ignore */
      }
      setUser(null);
    },
  };

  return <AuthCtx.Provider value={value}>{children}</AuthCtx.Provider>;
}

export function useAuth() {
  return useContext(AuthCtx);
}
