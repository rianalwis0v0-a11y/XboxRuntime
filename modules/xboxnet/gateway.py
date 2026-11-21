"""XboxNet's deliberately small control/status gateway.

This is infrastructure scaffolding, not an implementation of the original
Xbox Live wire protocol. It exposes read-only health and capability metadata.
"""
from __future__ import annotations

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Iterable, Tuple


SERVICE_CATALOG = (
    {
        "id": "authentication",
        "name": "Authentication and domain service",
        "state": "scaffold",
        "protocol": "research_required",
        "note": "Internal fail-closed adapter boundary only; no Xbox ticket protocol is implemented.",
    },
    {
        "id": "security_gateway",
        "name": "Security gateway",
        "state": "not_implemented",
        "protocol": "research_required",
    },
    {
        "id": "profiles",
        "name": "Account and profile APIs",
        "state": "not_implemented",
        "protocol": "research_required",
    },
    {
        "id": "friends_presence",
        "name": "Friends and presence",
        "state": "not_implemented",
        "protocol": "research_required",
    },
    {
        "id": "matchmaking",
        "name": "Title-specific matchmaking and sessions",
        "state": "not_implemented",
        "protocol": "title_specific_research_required",
    },
    {
        "id": "peer_connectivity",
        "name": "Peer connectivity",
        "state": "not_implemented",
        "protocol": "research_required",
    },
)


def status_document() -> Dict[str, Any]:
    """Return a JSON-safe snapshot of implemented and pending components."""
    return {
        "name": "XboxNet",
        "status": "scaffold",
        "protocol_compatibility": False,
        "services": [dict(item) for item in SERVICE_CATALOG],
        "implemented_in_this_gateway": [
            "read_only_health",
            "read_only_service_catalog",
        ],
    }


class XboxNetRequestHandler(BaseHTTPRequestHandler):
    server_version = "XboxNetGateway/0.1"
    sys_version = ""

    def _send_json(self, status: int, payload: Dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path == "/healthz":
            self._send_json(200, {"ok": True, "service": "xboxnet-gateway"})
            return
        if self.path == "/v1/status":
            self._send_json(200, status_document())
            return
        if self.path == "/v1/services":
            self._send_json(200, {"services": [dict(item) for item in SERVICE_CATALOG]})
            return
        self._send_json(404, {"error": "not_found", "path": self.path})

    def do_HEAD(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/healthz":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        # No state-changing or authentication routes exist yet. Do not silently
        # accept guessed Xbox Live requests or invent protocol behavior.
        self._send_json(
            501,
            {
                "error": "not_implemented",
                "message": "XboxNet protocol handlers are not implemented.",
            },
        )

    def log_message(self, fmt: str, *args: Iterable[Any]) -> None:
        # Keep the default server quiet; applications may wrap this handler
        # with their own structured logging when deploying it.
        return


class XboxNetHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def make_server(bind: str = "127.0.0.1", port: int = 8081) -> XboxNetHTTPServer:
    """Create (but do not start) the read-only HTTP status server."""
    if not 0 <= int(port) <= 65535:
        raise ValueError("port must be between 0 and 65535")
    return XboxNetHTTPServer((bind, int(port)), XboxNetRequestHandler)


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="XboxNet read-only status gateway")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    server = make_server(args.bind, args.port)
    print("XboxNet status gateway listening on http://%s:%d" % server.server_address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
