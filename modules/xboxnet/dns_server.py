#!/usr/bin/env python3
"""XboxNet DNS zone server, following the public insigniaDNS design.

It loads local A/AAAA mappings and an optional JSON zone feed. The feed format
accepts Insignia-style records: {"type":"a","name":"host","value":"192.0.2.1"}
or {"type":"p","name":"host","value":"target.example"}.
Unknown names receive an empty NOERROR response, not a fabricated address.
"""
from __future__ import annotations

import argparse
import ipaddress
import json
import socket
import socketserver
import struct
import threading
import time
import urllib.request
from pathlib import Path

HEADER = struct.Struct("!HHHHHH")
QUESTION_TAIL = struct.Struct("!HH")
A, AAAA, ANY, IN = 1, 28, 255, 1
DEFAULT_ZONE_URL = "https://insignia.live/dns_zones.json"


class DNSFormatError(ValueError):
    pass


def read_name(packet: bytes, offset: int) -> tuple[str, int]:
    labels: list[str] = []
    cursor = offset
    next_offset = None
    seen: set[int] = set()
    wire_length = 1
    while True:
        if cursor >= len(packet):
            raise DNSFormatError("name extends beyond packet")
        length = packet[cursor]
        if length & 0xC0 == 0xC0:
            if cursor + 1 >= len(packet):
                raise DNSFormatError("truncated compression pointer")
            pointer = ((length & 0x3F) << 8) | packet[cursor + 1]
            if pointer >= len(packet) or pointer in seen:
                raise DNSFormatError("invalid compression pointer")
            seen.add(pointer)
            if next_offset is None:
                next_offset = cursor + 2
            cursor = pointer
            continue
        if length & 0xC0:
            raise DNSFormatError("unsupported label encoding")
        cursor += 1
        if length == 0:
            break
        if length > 63 or cursor + length > len(packet):
            raise DNSFormatError("invalid label length")
        labels.append(packet[cursor:cursor + length].decode("ascii"))
        wire_length += length + 1
        if wire_length > 255:
            raise DNSFormatError("DNS name too long")
        cursor += length
    return ".".join(labels).lower(), next_offset if next_offset is not None else cursor


def encode_name(name: str) -> bytes:
    if not name or name == ".":
        return b"\0"
    out = bytearray()
    for label in name.rstrip(".").split("."):
        raw = label.encode("ascii")
        if not raw or len(raw) > 63:
            raise DNSFormatError("invalid hostname label")
        out.append(len(raw))
        out.extend(raw)
    out.append(0)
    if len(out) > 255:
        raise DNSFormatError("DNS name too long")
    return bytes(out)


def _add_record(records: dict, hostname: str, kind: str, address: str) -> None:
    name = hostname.rstrip(".").lower()
    if not name:
        raise ValueError("empty hostname in DNS records")
    ip = ipaddress.ip_address(address)
    if (kind == "A" and ip.version != 4) or (kind == "AAAA" and ip.version != 6):
        raise ValueError(f"{name}: {kind} record has wrong address family")
    records.setdefault(name, {})[kind] = str(ip)


def parse_zone_feed(payload: bytes | str, resolver=socket.gethostbyname) -> dict:
    data = json.loads(payload)
    if not isinstance(data, list):
        raise ValueError("DNS zone feed must be a JSON list")
    records: dict = {}
    for zone in data:
        if not isinstance(zone, dict):
            continue
        kind = str(zone.get("type", "")).lower()
        name = zone.get("name")
        value = zone.get("value")
        if not name or not value:
            continue
        if kind == "a":
            _add_record(records, name, "A", value)
        elif kind == "p":
            _add_record(records, name, "A", resolver(value))
    return records


def load_config(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    records: dict = {}
    for hostname, values in data.get("records", {}).items():
        for kind in ("A", "AAAA"):
            raw = values.get(kind)
            if raw:
                _add_record(records, hostname, kind, raw)
    ttl = int(data.get("ttl", 300))
    if not 0 <= ttl <= 86400:
        raise ValueError("ttl must be between 0 and 86400")
    return {
        "records": records,
        "zone_url": data.get("zone_url"),
        "ttl": ttl,
        "refresh_seconds": max(30, int(data.get("refresh_seconds", 300))),
    }


class DNSService:
    def __init__(self, config: dict):
        self.ttl = config["ttl"]
        self.zone_url = config.get("zone_url")
        self.refresh_seconds = config.get("refresh_seconds", 300)
        self._local_records = dict(config["records"])
        self._records = dict(self._local_records)
        self._lock = threading.RLock()
        if self.zone_url:
            self.refresh_zones()

    def refresh_zones(self) -> bool:
        if not self.zone_url:
            return False
        try:
            request = urllib.request.Request(
                self.zone_url, headers={"User-Agent": "XboxNetDNS/0.1"}
            )
            with urllib.request.urlopen(request, timeout=5) as response:
                payload = response.read(1024 * 1024 + 1)
            if len(payload) > 1024 * 1024:
                raise ValueError("DNS zone feed exceeds 1 MiB")
            feed_records = parse_zone_feed(payload)
            with self._lock:
                # Rebuild from the latest feed on every refresh. Merge only
                # explicit local overrides; merging the previous snapshot would
                # retain records that the upstream operator has removed.
                merged = dict(feed_records)
                merged.update(self._local_records)
                self._records = merged
            return True
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            # Keep the last known-good table if the remote feed is unavailable.
            return False

    def start_refresh(self) -> threading.Thread | None:
        if not self.zone_url:
            return None

        def worker():
            while True:
                time.sleep(self.refresh_seconds)
                self.refresh_zones()

        thread = threading.Thread(target=worker, name="xboxnet-dns-refresh", daemon=True)
        thread.start()
        return thread

    def handle(self, packet: bytes) -> bytes | None:
        if len(packet) < HEADER.size:
            return None
        ident, flags, qd, _, _, _ = HEADER.unpack_from(packet)
        if flags & 0x8000 or qd != 1:
            return None
        try:
            name, end = read_name(packet, HEADER.size)
            if end + QUESTION_TAIL.size > len(packet):
                return None
            qtype, qclass = QUESTION_TAIL.unpack_from(packet, end)
            question_end = end + QUESTION_TAIL.size
            question = packet[HEADER.size:question_end]
        except (DNSFormatError, UnicodeError, struct.error):
            return None

        answers = []
        if qclass == IN:
            with self._lock:
                values = self._records.get(name, {})
            kinds = ("A", "AAAA") if qtype == ANY else (("A",) if qtype == A else (("AAAA",) if qtype == AAAA else ()))
            for kind in kinds:
                address = values.get(kind)
                if address:
                    rtype = A if kind == "A" else AAAA
                    packed = ipaddress.ip_address(address).packed
                    answers.append(b"\xc0\x0c" + struct.pack("!HHIH", rtype, IN, self.ttl, len(packed)) + packed)

        # This is a mapping DNS server, not a general-purpose open resolver.
        # Unmapped names return NOERROR/NODATA, matching insigniaDNS behavior.
        response_flags = 0x8000 | 0x0400 | (flags & 0x0100)
        return HEADER.pack(ident, response_flags, 1, len(answers), 0, 0) + question + b"".join(answers)


class UDPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        packet, sock = self.request
        response = self.server.service.handle(packet)
        if response:
            sock.sendto(response, self.client_address)


class TCPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        prefix = self._read_exact(2)
        if prefix is None:
            return
        size = struct.unpack("!H", prefix)[0]
        if size < HEADER.size:
            return
        packet = self._read_exact(size)
        if packet is None:
            return
        response = self.server.service.handle(packet)
        if response and len(response) <= 65535:
            self.request.sendall(struct.pack("!H", len(response)) + response)

    def _read_exact(self, size):
        chunks = bytearray()
        while len(chunks) < size:
            part = self.request.recv(size - len(chunks))
            if not part:
                return None
            chunks.extend(part)
        return bytes(chunks)


class UDPServer(socketserver.ThreadingUDPServer):
    allow_reuse_address = True
    daemon_threads = True


class TCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> None:
    parser = argparse.ArgumentParser(description="XboxNet DNS zone server")
    parser.add_argument("--config", default="xboxnet-dns.json")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5353,
                        help="DNS port (default: 5353; use 53 for deployment)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    config = load_config(Path(args.config))
    service = DNSService(config)
    service.start_refresh()
    udp = UDPServer((args.bind, args.port), UDPHandler)
    tcp = TCPServer((args.bind, args.port), TCPHandler)
    udp.service = service
    tcp.service = service
    threading.Thread(target=tcp.serve_forever, daemon=True).start()
    print(f"XboxNet DNS listening on {args.bind}:{args.port} (UDP/TCP)", flush=True)
    try:
        udp.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        udp.shutdown()
        tcp.shutdown()
        udp.server_close()
        tcp.server_close()


if __name__ == "__main__":
    main()
