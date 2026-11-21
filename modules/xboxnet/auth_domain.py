"""Protocol-neutral authentication boundary for XboxNet.

This module intentionally does not parse or mint original-Xbox tickets. The
original protocol's ticket format, cryptography, and domain exchange must be
implemented only after they are established from permitted evidence.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Protocol


@dataclass(frozen=True)
class AuthDecision:
    """Result returned by the domain service without exposing ticket contents."""

    accepted: bool
    principal_id: Optional[str]
    reason: str


class TicketVerifier(Protocol):
    """Adapter contract for a separately researched ticket verifier."""

    def verify(self, ticket: bytes) -> Optional[str]:
        """Return a stable principal ID for a valid ticket, otherwise None."""
        ...


class UnavailableTicketVerifier:
    """Fail-closed default until an authentic protocol adapter exists."""

    def verify(self, ticket: bytes) -> Optional[str]:
        return None


class AuthDomainService:
    """Validate request bounds and delegate protocol verification.

    This is an internal adapter seam, not an Xbox wire-protocol endpoint.
    """

    def __init__(
        self,
        verifier: Optional[TicketVerifier] = None,
        max_ticket_bytes: int = 16 * 1024,
    ) -> None:
        if max_ticket_bytes < 1:
            raise ValueError("max_ticket_bytes must be positive")
        self._verifier = verifier or UnavailableTicketVerifier()
        self._max_ticket_bytes = max_ticket_bytes

    def authenticate(self, ticket: bytes) -> AuthDecision:
        if not isinstance(ticket, bytes):
            return AuthDecision(False, None, "invalid_ticket_type")
        if not ticket:
            return AuthDecision(False, None, "empty_ticket")
        if len(ticket) > self._max_ticket_bytes:
            return AuthDecision(False, None, "ticket_too_large")

        try:
            principal_id = self._verifier.verify(ticket)
        except Exception:
            # Fail closed. Do not return exception text or credential material.
            return AuthDecision(False, None, "verifier_error")

        if not isinstance(principal_id, str) or not principal_id.strip():
            return AuthDecision(False, None, "ticket_rejected")
        return AuthDecision(True, principal_id, "accepted")
