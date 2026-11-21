import json
import os
import tempfile
import unittest

from account_profiles import (
    AccountProfileService,
    InvalidProfile,
    ProfileAlreadyExists,
    ProfileNotFound,
    ProfileServiceHandler,
)
from auth_domain import AuthDecision, AuthDomainService
from security_gateway import (
    AuthContext,
    SecurityGatewayDispatcher,
    XboxNetSecurityGateway,
)


class AccountProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.database = os.path.join(self.temp.name, "profiles.sqlite3")
        self.service = AccountProfileService(self.database)

    def tearDown(self):
        self.temp.cleanup()

    def test_create_and_read_profile(self):
        created = self.service.create_profile("principal-a", "Rian", "Hello Xbox")
        loaded = self.service.get_profile("principal-a")
        self.assertEqual(loaded, created)
        self.assertGreater(created.xuid, 0)
        self.assertEqual(created.motto, "Hello Xbox")

    def test_principal_cannot_create_two_profiles(self):
        self.service.create_profile("principal-a", "Rian")
        with self.assertRaises(ProfileAlreadyExists):
            self.service.create_profile("principal-a", "Rian2")

    def test_gamertag_is_unique_case_insensitively(self):
        self.service.create_profile("principal-a", "PlayerOne")
        with self.assertRaises(ProfileAlreadyExists):
            self.service.create_profile("principal-b", "playerone")

    def test_profile_isolation_by_principal(self):
        self.service.create_profile("principal-a", "PlayerA")
        with self.assertRaises(ProfileNotFound):
            self.service.get_profile("principal-b")

    def test_update_profile(self):
        self.service.create_profile("principal-a", "PlayerA")
        updated = self.service.update_profile(
            "principal-a", gamertag="PlayerB", motto="Ready to play"
        )
        self.assertEqual(updated.gamertag, "PlayerB")
        self.assertEqual(updated.motto, "Ready to play")

    def test_invalid_gamertag_is_rejected(self):
        with self.assertRaises(InvalidProfile):
            self.service.create_profile("principal-a", "this gamertag is much too long")

    def test_unknown_update_fields_are_rejected(self):
        self.service.create_profile("principal-a", "PlayerA")
        with self.assertRaises(InvalidProfile):
            self.service.update_profile("principal-a", principal_id="other")

    def test_profile_handler_create_get_update(self):
        context = AuthContext.from_decision(AuthDecision(True, "principal-a", "accepted"))
        handler = ProfileServiceHandler(self.service)

        created = json.loads(handler.handle(
            context, b'{"operation":"create","gamertag":"Rian","motto":"Hello"}'
        ))
        self.assertTrue(created["ok"])
        self.assertEqual(created["profile"]["gamertag"], "Rian")

        updated = json.loads(handler.handle(
            context, b'{"operation":"update","changes":{"motto":"Updated"}}'
        ))
        self.assertEqual(updated["profile"]["motto"], "Updated")

        loaded = json.loads(handler.handle(context, b'{"operation":"get"}'))
        self.assertEqual(loaded["profile"]["motto"], "Updated")

    def test_handler_rejects_bad_json_and_unknown_operation(self):
        context = AuthContext("principal-a")
        handler = ProfileServiceHandler(self.service)
        self.assertEqual(
            json.loads(handler.handle(context, b"{"))["error"], "invalid_json"
        )
        self.assertEqual(
            json.loads(handler.handle(context, b'{"operation":"delete"}'))["error"],
            "unknown_operation",
        )

    def test_gateway_only_dispatches_profile_after_authentication(self):
        context = AuthContext("principal-a")
        handler = ProfileServiceHandler(self.service)
        gateway = SecurityGatewayDispatcher({"profiles": handler})
        result = gateway.dispatch(
            context, "profiles", b'{"operation":"create","gamertag":"Rian"}'
        )
        self.assertTrue(result.accepted)
        self.assertTrue(json.loads(result.response)["ok"])

    def test_ticket_gateway_stays_closed_without_real_verifier(self):
        gateway = XboxNetSecurityGateway(
            AuthDomainService(),
            SecurityGatewayDispatcher({"profiles": ProfileServiceHandler(self.service)}),
        )
        result = gateway.dispatch_ticket(
            b"placeholder-ticket", "profiles",
            b'{"operation":"create","gamertag":"Rian"}',
        )
        self.assertEqual(result.code, "authentication_failed")


if __name__ == "__main__":
    unittest.main()
