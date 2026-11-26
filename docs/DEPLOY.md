# Deploying Wayfinder (`journey --serve`)

Wayfinder is designed as a **loopback-only local demo** (`127.0.0.1:8080`). By
default `src/server.cpp` binds `INADDR_LOOPBACK` (`127.0.0.1`), runs
single-threaded (`--threads 1`), and has no authentication or TLS.

If you want to keep it running in the background on a lab machine or expose it
behind a reverse proxy on a trusted internal network, use the setups below.

## 1. Build first

```bash
make clean && make
```

The server resolves `--web` (default `web/`) and `samples/` relative to the
working directory, so set `WorkingDirectory` to the repository root (or pass
`--web /abs/path/to/web`).

## 2. `systemd` user service (loopback only)

Create `~/.config/systemd/user/wayfinder.service`:

```ini
[Unit]
Description=Wayfinder minimum-bus journey planner
After=network.target

[Service]
Type=simple
WorkingDirectory=/path/to/DSCE_AAT
ExecStart=/path/to/DSCE_AAT/build/journey --serve --port 8080 --threads 4 --log
Restart=on-failure
RestartSec=2
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=default.target
```

Enable and manage it without `sudo`:

```bash
systemctl --user daemon-reload
systemctl --user enable --now wayfinder
systemctl --user status wayfinder
journalctl --user -u wayfinder -f
```

Notes:
- `--threads 4` is opt-in (default `1` single-threaded). `AppState` is guarded
  by a `std::shared_mutex` so concurrent `/api/journey` and `/api/impact` reads
  run in parallel while `/api/build` takes an exclusive write lock.
- `--log` prints `[http] METHOD PATH -> STATUS (us)` lines to stderr, which
  `journalctl --user -u wayfinder` captures automatically.
- Keep `journey --serve` bound to `127.0.0.1`; never bind it directly to
  `0.0.0.0`.

## 3. Reverse proxy (Caddy or Nginx)

Terminate TLS and optional access control at the proxy while keeping Wayfinder
on `127.0.0.1:8080`.

### Caddy

```caddyfile
wayfinder.example.internal {
  reverse_proxy 127.0.0.1:8080
}
```

### Nginx

```nginx
server {
  listen 443 ssl;
  server_name wayfinder.example.internal;

  # ssl_certificate ...
  # ssl_certificate_key ...

  location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    client_max_body_size 1m;
  }
}
```

## 4. Operational limits & caveats

- **Shared in-memory state:** `POST /api/build` replaces the active network for
  all visitors to that server instance. Keep public deployments read-only (for
  example, block `POST /api/build` at the proxy if exposing a fixed sample) or
  run behind authentication.
- **Request size:** `src/server.cpp` caps incoming HTTP bodies and route counts
  at the contract limits (`<= 400` routes, `<= 40,000` stop appearances).
- **No external runtime dependencies:** only the `build/journey` binary, `web/`,
  and `samples/` are required at runtime.
