import json
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen, Request

from gateway import SERVICE_CATALOG, make_server, status_document


class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.server = make_server("127.0.0.1", 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        host, port = self.server.server_address
        self.base = "http://%s:%d" % (host, port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def get_json(self, path):
        with urlopen(self.base + path, timeout=2) as response:
            return response.status, response.headers, json.loads(response.read().decode())

    def test_health_endpoint(self):
        status, headers, payload = self.get_json("/healthz")
        self.assertEqual(status, 200)
        self.assertEqual(payload["ok"], True)
        self.assertEqual(headers.get("Cache-Control"), "no-store")

    def test_status_is_explicitly_not_protocol_compatible(self):
        status, _, payload = self.get_json("/v1/status")
        self.assertEqual(status, 200)
        self.assertFalse(payload["protocol_compatibility"])
        self.assertEqual(payload["status"], "scaffold")

    def test_service_catalog_marks_protocol_work_pending(self):
        status, _, payload = self.get_json("/v1/services")
        self.assertEqual(status, 200)
        self.assertEqual(len(payload["services"]), len(SERVICE_CATALOG))
        states = {item["id"]: item["state"] for item in payload["services"]}
        self.assertEqual(states["authentication"], "scaffold")
        self.assertEqual(states["security_gateway"], "scaffold")
        self.assertTrue(all(state == "not_implemented" for key, state in states.items() if key not in {"authentication", "security_gateway"}))

    def test_unknown_path_is_404(self):
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.base + "/invented-endpoint", timeout=2)
        self.assertEqual(caught.exception.code, 404)

    def test_post_is_not_implemented(self):
        request = Request(self.base + "/anything", data=b"{}", method="POST")
        with self.assertRaises(HTTPError) as caught:
            urlopen(request, timeout=2)
        self.assertEqual(caught.exception.code, 501)
        payload = json.loads(caught.exception.read().decode())
        self.assertEqual(payload["error"], "not_implemented")

    def test_status_document_is_copy_safe(self):
        snapshot = status_document()
        snapshot["services"][0]["state"] = "changed"
        self.assertEqual(SERVICE_CATALOG[0]["state"], "scaffold")


if __name__ == "__main__":
    unittest.main()
