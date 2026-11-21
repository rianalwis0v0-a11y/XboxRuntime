import unittest

from auth_domain import AuthDomainService


class AcceptingVerifier:
    def verify(self, ticket):
        if ticket == b"test-ticket":
            return "test-principal"
        return None


class ExplodingVerifier:
    def verify(self, ticket):
        raise RuntimeError("sensitive internal detail")


class AuthDomainTests(unittest.TestCase):
    def test_default_verifier_fails_closed(self):
        result = AuthDomainService().authenticate(b"opaque-ticket")
        self.assertFalse(result.accepted)
        self.assertIsNone(result.principal_id)
        self.assertEqual(result.reason, "ticket_rejected")

    def test_valid_result_is_delegated_to_adapter(self):
        result = AuthDomainService(AcceptingVerifier()).authenticate(b"test-ticket")
        self.assertTrue(result.accepted)
        self.assertEqual(result.principal_id, "test-principal")
        self.assertEqual(result.reason, "accepted")

    def test_invalid_type_is_rejected(self):
        result = AuthDomainService().authenticate("not-bytes")
        self.assertEqual(result.reason, "invalid_ticket_type")

    def test_empty_ticket_is_rejected(self):
        result = AuthDomainService().authenticate(b"")
        self.assertEqual(result.reason, "empty_ticket")

    def test_oversized_ticket_is_rejected_before_verification(self):
        result = AuthDomainService(max_ticket_bytes=4).authenticate(b"12345")
        self.assertEqual(result.reason, "ticket_too_large")

    def test_verifier_exceptions_fail_closed_without_leaking_details(self):
        result = AuthDomainService(ExplodingVerifier()).authenticate(b"opaque")
        self.assertFalse(result.accepted)
        self.assertIsNone(result.principal_id)
        self.assertEqual(result.reason, "verifier_error")

    def test_max_ticket_size_must_be_positive(self):
        with self.assertRaises(ValueError):
            AuthDomainService(max_ticket_bytes=0)


if __name__ == "__main__":
    unittest.main()
