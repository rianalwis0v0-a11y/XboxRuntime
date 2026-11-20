import struct
import unittest

from dns_server import DNSService, encode_name, read_name


def query(name, qtype=1, ident=0x1234):
    return struct.pack("!HHHHHH", ident, 0x0100, 1, 0, 0, 0) + encode_name(name) + struct.pack("!HH", qtype, 1)


class DNSCodecTests(unittest.TestCase):
    def test_name_round_trip(self):
        wire = encode_name("dashboard.xboxnet.local")
        self.assertEqual(read_name(wire, 0), ("dashboard.xboxnet.local", len(wire)))

    def test_compressed_name(self):
        packet = encode_name("xboxnet.local") + b"\x03www\xc0\x00"
        self.assertEqual(read_name(packet, len(encode_name("xboxnet.local"))), ("www.xboxnet.local", len(packet)))

    def test_rejects_bad_pointer(self):
        with self.assertRaises(ValueError):
            read_name(b"\xc0\x00", 0)


class DNSServiceTests(unittest.TestCase):
    def setUp(self):
        self.service = DNSService({
            "records": {"dashboard.xboxnet.test": {"A": "192.0.2.44", "AAAA": "2001:db8::44"}},
            "upstream": "127.0.0.1",
            "ttl": 30,
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

    def test_ignores_response_packets(self):
        self.assertIsNone(self.service.handle(b"\x00" * 12))

    def test_ignores_multiple_questions(self):
        packet = bytearray(query("dashboard.xboxnet.test"))
        struct.pack_into("!H", packet, 4, 2)
        self.assertIsNone(self.service.handle(bytes(packet)))


if __name__ == "__main__":
    unittest.main()
