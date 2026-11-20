#!/usr/bin/env python3
"""Small configurable DNS forwarder for XboxNet.

Runs on an unprivileged port by default (5353). Use --port 53 on a host where
the process is permitted to bind the DNS port.
"""
from __future__ import annotations

import argparse
import ipaddress
import json
import socket
import socketserver
import struct
import threading
from pathlib import Path

HEADER = struct.Struct("!HHHHHH")
QUESTION_TAIL = struct.Struct("!HH")
A = 1
AAAA = 28
IN = 1


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
        label = packet[cursor:cursor + length].decode("ascii")
        labels.append(label)
        wire_length += length + 1
        if wire_length > 255:
            raise DNSFormatError("DNS name too long")
        cursor += length

    return ".".join(labels).lower(), (next_offset if next_offset is not None else cursor + 1)


def encode_name(name: str) -> bytes:
    if not name or name == ".":
        return b"\0"
    out = bytearray()
    for label in name.rstrip(".").split("."):
        raw = label.encode("ascii")
        if not raw or len(raw) > 63:
            raise DNSFormatError("invalid configured hostname")
        out.append(len(raw))
        out.extend(raw)
    out.append(0)
    if len(out) > 255:
        raise DNSFormatError("configured hostname too long")
    return bytes(out)


def load_config(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    mappings = {}
    for hostname, values in data.get("records", {}).items():
        key = hostname.rstrip(".").lower()
        if not key:
            raise ValueError("empty hostname in records")
        normalized = {}
        for kind in ("A", "AAAA"):
            raw = values.get(kind)
            if raw:
                address = ipaddress.ip_address(raw)
                if (kind == "A") != (address.version == 4):
                    raise ValueError(f"{hostname}: {kind} record has wrong address family")
                normalized[kind] = str(address)
        if normalized:
            mappings[key] = normalized
    upstream = data.get("upstream", "1.1.1.1")
    ipaddress.ip_address(upstream)
    ttl = int(data.get("ttl", 60))
    if not 0 <= ttl <= 86400:
        raise ValueError("ttl must be between 0 and 86400")
    return {"records": mappings, "upstream": upstream, "ttl": ttl}


class DNSService:
    def __init__(self, config: dict, timeout: float = 3.0):
        self.config = config
        self.timeout = timeout

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

        if qclass == IN and qtype in (A, AAAA):
            kind = "A" if qtype == A else "AAAA"
            address = self.config["records"].get(name, {}).get(kind)
            if address:
                packed = ipaddress.ip_address(address).packed
                answer = b"\xc0\x0c" + struct.pack("!HHIH", qtype, IN, self.config["ttl"], len(packed)) + packed
                response_flags = 0x8000 | (flags & 0x0100) | 0x0080
                return HEADER.pack(ident, response_flags, 1, 1, 0, 0) + question + answer

        # Unknown names/types are forwarded unchanged, preserving DNS behavior.
        try:
            family = socket.AF_INET6 if ":" in self.config["upstream"] else socket.AF_INET
            with socket.socket(family, socket.SOCK_DGRAM) as sock:
                sock.settimeout(self.timeout)
                sock.sendto(packet, (self.config["upstream"], 53))
                response, _ = sock.recvfrom(65535)
            if len(response) >= HEADER.size and HEADER.unpack_from(response)[0] == ident:
                return response
        except OSError:
            pass

        # No upstream answer: return SERVFAIL rather than inventing an address.
        response_flags = 0x8000 | (flags & 0x0100) | 0x0082
        return HEADER.pack(ident, response_flags, 1, 0, 0, 0) + question


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
    parser = argparse.ArgumentParser(description="XboxNet DNS forwarder")
    parser.add_argument("--config", default="xboxnet-dns.json")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5353,
                        help="DNS port (default: 5353; use 53 for deployment)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    config = load_config(Path(args.config))
    service = DNSService(config)
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
