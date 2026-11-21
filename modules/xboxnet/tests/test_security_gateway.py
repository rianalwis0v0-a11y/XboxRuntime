import unittest

from auth_domain import AuthDecision, AuthDomainService
from security_gateway import AuthContext, SecurityGatewayDispatcher, XboxNetSecurityGateway


class EchoHandler:
    def handle(self, context, payload):
        return context.principal_id.encode() + b":" + payload


class BrokenHandler:
    def handle(self, context, payload):
        raise RuntimeError("private internal detail")


class SecurityGatewayTests(unittest.TestCase):
    def setUp(self):
        self.context = AuthContext.from_decision(AuthDecision(True, "player-1", "accepted"))

    def test_context_requires_accepted_authentication(self):
        self.assertIsNone(AuthContext.from_decision(AuthDecision(False, None, "rejected")))
        self.assertEqual(self.context.principal_id, "player-1")

    def test_missing_context_fails_closed(self):
        result = SecurityGatewayDispatcher({"profile": EchoHandler()}).dispatch(
            None, "profile", b"hello"
        )
        self.assertEqual((result.accepted, result.code), (False, "unauthenticated"))

    def test_unknown_service_is_unavailable(self):
        result = SecurityGatewayDispatcher().dispatch(self.context, "unknown", b"")
        self.assertEqual(result.code, "service_unavailable")

    def test_registered_handler_receives_authenticated_context(self):
        result = SecurityGatewayDispatcher({"echo": EchoHandler()}).dispatch(
            self.context, "echo", b"hello"
        )
        self.assertTrue(result.accepted)
        self.assertEqual(result.response, b"player-1:hello")

    def test_payload_limit_is_enforced(self):
        dispatcher = SecurityGatewayDispatcher({"echo": EchoHandler()}, max_payload_bytes=3)
        result = dispatcher.dispatch(self.context, "echo", b"four")
        self.assertEqual(result.code, "payload_too_large")

    def test_handler_errors_are_sanitized(self):
        result = SecurityGatewayDispatcher({"broken": BrokenHandler()}).dispatch(
            self.context, "broken", b""
        )
        self.assertEqual(result.code, "handler_error")
        self.assertIsNone(result.response)

    def test_non_bytes_handler_response_is_rejected(self):
        class BadHandler:
            def handle(self, context, payload):
                return "not bytes"

        result = SecurityGatewayDispatcher({"bad": BadHandler()}).dispatch(
            self.context, "bad", b""
        )
        self.assertEqual(result.code, "invalid_handler_response")

    def test_response_limit_is_enforced(self):
        dispatcher = SecurityGatewayDispatcher(
            {"echo": EchoHandler()}, max_response_bytes=2
        )
        result = dispatcher.dispatch(self.context, "echo", b"hello")
        self.assertEqual(result.code, "response_too_large")

    def test_ticket_flow_fails_closed_with_default_verifier(self):
        gateway = XboxNetSecurityGateway(
            AuthDomainService(),
            SecurityGatewayDispatcher({"echo": EchoHandler()}),
        )
        result = gateway.dispatch_ticket(b"ticket", "echo", b"hello")
        self.assertEqual(result.code, "authentication_failed")

    def test_ticket_flow_authenticates_before_dispatch(self):
        class AcceptVerifier:
            def verify(self, ticket):
                return "player-2" if ticket == b"valid" else None

        gateway = XboxNetSecurityGateway(
            AuthDomainService(AcceptVerifier()),
            SecurityGatewayDispatcher({"echo": EchoHandler()}),
        )
        result = gateway.dispatch_ticket(b"valid", "echo", b"hello")
        self.assertTrue(result.accepted)
        self.assertEqual(result.response, b"player-2:hello")

    def test_limits_must_be_positive(self):
        with self.assertRaises(ValueError):
            SecurityGatewayDispatcher(max_payload_bytes=0)


if __name__ == "__main__":
    unittest.main()
