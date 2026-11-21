"""Internal, protocol-neutral XboxNet security-gateway dispatch boundary.

This module is deliberately not an Xbox security-gateway wire implementation.
It accepts already-authenticated context from the auth-domain adapter and
routes bounded payloads to explicitly registered service handlers.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, Optional, Protocol

from auth_domain import AuthDecision, AuthDomainService


@dataclass(frozen=True)
class AuthContext:
    """Authenticated identity passed from the authentication boundary."""

    principal_id: str

    @classmethod
    def from_decision(cls, decision: AuthDecision) -> Optional["AuthContext"]:
        if not decision.accepted or not decision.principal_id:
            return None
        return cls(decision.principal_id)


class ServiceHandler(Protocol):
    def handle(self, context: AuthContext, payload: bytes) -> bytes:
        """Handle one service payload and return response bytes."""
        ...


@dataclass(frozen=True)
class DispatchResult:
    accepted: bool
    code: str
    response: Optional[bytes] = None


class SecurityGatewayDispatcher:
    """Fail-closed internal service dispatcher; no network framing is assumed."""

    def __init__(
        self,
        handlers: Optional[Mapping[str, ServiceHandler]] = None,
        max_payload_bytes: int = 64 * 1024,
        max_response_bytes: int = 64 * 1024,
    ) -> None:
        if max_payload_bytes < 1 or max_response_bytes < 1:
            raise ValueError("payload limits must be positive")
        self._handlers = dict(handlers or {})
        self._max_payload_bytes = max_payload_bytes
        self._max_response_bytes = max_response_bytes

    def dispatch(
        self,
        context: Optional[AuthContext],
        service_id: str,
        payload: bytes,
    ) -> DispatchResult:
        if context is None or not isinstance(context, AuthContext):
            return DispatchResult(False, "unauthenticated")
        if not isinstance(context.principal_id, str) or not context.principal_id.strip():
            return DispatchResult(False, "invalid_principal")
        if not isinstance(service_id, str) or not service_id.strip():
            return DispatchResult(False, "invalid_service")
        if not isinstance(payload, bytes):
            return DispatchResult(False, "invalid_payload_type")
        if len(payload) > self._max_payload_bytes:
            return DispatchResult(False, "payload_too_large")

        handler = self._handlers.get(service_id)
        if handler is None:
            return DispatchResult(False, "service_unavailable")

        try:
            response = handler.handle(context, payload)
        except Exception:
            return DispatchResult(False, "handler_error")
        if not isinstance(response, bytes):
            return DispatchResult(False, "invalid_handler_response")
        if len(response) > self._max_response_bytes:
            return DispatchResult(False, "response_too_large")
        return DispatchResult(True, "dispatched", response)



class XboxNetSecurityGateway:
    """Compose ticket authentication and service dispatch behind one boundary."""

    def __init__(
        self,
        auth_domain: AuthDomainService,
        dispatcher: SecurityGatewayDispatcher,
    ) -> None:
        self._auth_domain = auth_domain
        self._dispatcher = dispatcher

    def dispatch_ticket(
        self,
        ticket: bytes,
        service_id: str,
        payload: bytes,
    ) -> DispatchResult:
        decision = self._auth_domain.authenticate(ticket)
        context = AuthContext.from_decision(decision)
        if context is None:
            return DispatchResult(False, "authentication_failed")
        return self._dispatcher.dispatch(context, service_id, payload)
