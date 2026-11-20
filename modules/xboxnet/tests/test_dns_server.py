import json
import struct
import unittest
from unittest.mock import patch

from dns_server import DNSService, encode_name, parse_zone_feed, read_name


def query(name, qtype=1, ident=0x1234):
    return struct.pack("!HHHHHH", ident, 0x0100, 1, 0, 0, 0) + encode_name(name) + struct.pack("!HH", qtype, 1)


class DNSCodecTests(unittest.TestCase):
    def test_name_round_trip(self):
        wire = encode_name("dashboard.xboxnet.local")
        self.assertEqual(read_name(wire, 0), ("dashboard.xboxnet.local", len(wire)))

    def test_compressed_name(self):
        base = encode_name("xboxnet.local")
        packet = base + b"\x03www\xc0\x00"
        self.assertEqual(read_name(packet, len(base)), ("www.xboxnet.local", len(packet)))

    def test_rejects_bad_pointer(self):
        with self.assertRaises(ValueError):
            read_name(b"\xc0\x00", 0)


class ZoneFeedTests(unittest.TestCase):
    def test_insignia_style_static_and_hostname_records(self):
        payload = json.dumps([
            {"type": "a", "name": "static.xboxnet.test", "value": "192.0.2.8"},
            {"type": "p", "name": "dynamic.xboxnet.test", "value": "backend.example.test"},
            {"type": "ignored", "name": "skip.xboxnet.test", "value": "x"},
        ])
        records = parse_zone_feed(payload, resolver=lambda host: "198.51.100.7")
        self.assertEqual(records["static.xboxnet.test"]["A"], "192.0.2.8")
        self.assertEqual(records["dynamic.xboxnet.test"]["A"], "198.51.100.7")
        self.assertNotIn("skip.xboxnet.test", records)

    def test_rejects_wrong_address(self):
        with self.assertRaises(ValueError):
            parse_zone_feed('[{"type":"a","name":"bad.test","value":"not-an-ip"}]')


class DNSServiceTests(unittest.TestCase):
    def setUp(self):
        self.service = DNSService({
            "records": {"dashboard.xboxnet.test": {"A": "192.0.2.44", "AAAA": "2001:db8::44"}},
            "zone_url": None,
            "ttl": 30,
            "refresh_seconds": 300,
        })

    def test_a_override(self):
        response = self.service.handle(query("dashboard.xboxnet.test"))
        ident, flags, qd, an, _, _ = struct.unpack_from("!HHHHHH", response)
        self.assertEqual((ident, qd, an), (0x1234, 1, 1))
        self.assertTrue(flags & 0x8000)
        self.assertEqual(response[-4:], b"\xc0\x00\x02\x2c")

    def test_aaaa_override(self):
        response = self.service.handle(query("dashboard.xboxnet.test", 28))
        self.assertEqual(response[-16:], bytes.fromhex("20010db8000000000000000000000044"))

    def test_unknown_name_is_nodata_not_upstream_forwarded(self):
        response = self.service.handle(query("unknown.example.test"))
        _, flags, _, answers, _, _ = struct.unpack_from("!HHHHHH", response)
        self.assertEqual(answers, 0)
        self.assertEqual(flags & 0x000F, 0)

    def test_any_returns_available_records(self):
        response = self.service.handle(query("dashboard.xboxnet.test", 255))
        self.assertEqual(struct.unpack_from("!H", response, 6)[0], 2)

    def test_ignores_response_packets(self):
        self.assertIsNone(self.service.handle(b"\x00" * 12))

    def test_ignores_multiple_questions(self):
        packet = bytearray(query("dashboard.xboxnet.test"))
        struct.pack_into("!H", packet, 4, 2)
        self.assertIsNone(self.service.handle(bytes(packet)))


if __name__ == "__main__":
    unittest.main()
