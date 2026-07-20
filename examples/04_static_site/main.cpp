// 04_static_site — static files with compression, caching and an SPA
// fallback (unknown paths serve index.html so client-side routing works).
//
// Files stream from disk with sendfile(2) — no in-memory copies.
//
// Try it:
//   curl -v http://localhost:8080/                        # index.html
//   curl -v -H "Accept-Encoding: gzip" http://localhost:8080/app.css
//   curl -v http://localhost:8080/some/spa/route           # SPA fallback

#include <socketify/socketify.h>

#include <cstdio>

using namespace socketify;

int main() {
    ServerOptions opts;
    opts.compression.min_size = 256; // compress text assets over 256 bytes

    Server server(opts);

    static_files::Options sf;
    sf.root = "public";
    sf.cache_max_age = 3600;   // Cache-Control: public, max-age=3600
    sf.etag = true;            // conditional requests get 304
    server.Use(static_files::serve(sf));

    // SPA fallback: anything the static handler didn't match serves the
    // app shell (client-side router takes over in the browser).
    server.Get("/*rest", [](Request&, Response& res) {
        if (!res.send_file("public/index.html")) {
            res.status(Status::NotFound).send("Not Found\n");
        }
    });

    if (!server.Run("0.0.0.0", 8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("static site on http://localhost:8080 (serving ./public)\n");
    server.Wait();
    return 0;
}
