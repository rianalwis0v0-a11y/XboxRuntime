"""XboxNet account/profile storage and authenticated service handler.

This is an internal service implementation, not an original-Xbox HTTP or
binary protocol implementation. Authentication is supplied by AuthContext.
"""
from __future__ import annotations

import json
import re
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from security_gateway import AuthContext


_GAMERTAG = re.compile(r"^[A-Za-z0-9 _-]{1,15}$")
_MAX_TEXT = 128


class ProfileError(Exception):
    """Base class for expected profile-service errors."""


class ProfileNotFound(ProfileError):
    pass


class ProfileAlreadyExists(ProfileError):
    pass


class InvalidProfile(ProfileError):
    pass


@dataclass(frozen=True)
class PlayerProfile:
    xuid: int
    gamertag: str
    motto: str
    location: str
    created_at: int


class AccountProfileService:
    """SQLite-backed profile store keyed by authenticated principal ID."""

    def __init__(self, database: str | Path) -> None:
        self._database = str(database)
        if self._database != ":memory:":
            Path(self._database).parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as connection:
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS player_profiles (
                    xuid INTEGER PRIMARY KEY AUTOINCREMENT,
                    principal_id TEXT NOT NULL UNIQUE,
                    gamertag TEXT NOT NULL COLLATE NOCASE UNIQUE,
                    motto TEXT NOT NULL DEFAULT '',
                    location TEXT NOT NULL DEFAULT '',
                    created_at INTEGER NOT NULL
                )
                """
            )

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self._database, timeout=5)
        connection.row_factory = sqlite3.Row
        return connection

    @staticmethod
    def _validate_principal(principal_id: str) -> str:
        if not isinstance(principal_id, str) or not principal_id.strip():
            raise InvalidProfile("invalid_principal")
        if len(principal_id) > 256:
            raise InvalidProfile("principal_too_long")
        return principal_id

    @staticmethod
    def _validate_gamertag(gamertag: str) -> str:
        if not isinstance(gamertag, str) or not _GAMERTAG.fullmatch(gamertag):
            raise InvalidProfile("invalid_gamertag")
        return gamertag

    @staticmethod
    def _validate_text(value: str, field: str) -> str:
        if not isinstance(value, str) or len(value) > _MAX_TEXT:
            raise InvalidProfile("invalid_" + field)
        if any(ord(char) < 32 for char in value):
            raise InvalidProfile("invalid_" + field)
        return value

    @staticmethod
    def _profile(row: sqlite3.Row) -> PlayerProfile:
        return PlayerProfile(
            xuid=int(row["xuid"]),
            gamertag=row["gamertag"],
            motto=row["motto"],
            location=row["location"],
            created_at=int(row["created_at"]),
        )

    def create_profile(
        self,
        principal_id: str,
        gamertag: str,
        motto: str = "",
        location: str = "",
    ) -> PlayerProfile:
        principal_id = self._validate_principal(principal_id)
        gamertag = self._validate_gamertag(gamertag)
        motto = self._validate_text(motto, "motto")
        location = self._validate_text(location, "location")
        try:
            with self._connect() as connection:
                cursor = connection.execute(
                    """INSERT INTO player_profiles
                       (principal_id, gamertag, motto, location, created_at)
                       VALUES (?, ?, ?, ?, ?)""",
                    (principal_id, gamertag, motto, location, int(time.time())),
                )
                row = connection.execute(
                    "SELECT * FROM player_profiles WHERE xuid = ?",
                    (cursor.lastrowid,),
                ).fetchone()
                return self._profile(row)
        except sqlite3.IntegrityError as exc:
            raise ProfileAlreadyExists("principal_or_gamertag_exists") from exc

    def get_profile(self, principal_id: str) -> PlayerProfile:
        principal_id = self._validate_principal(principal_id)
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM player_profiles WHERE principal_id = ?",
                (principal_id,),
            ).fetchone()
        if row is None:
            raise ProfileNotFound("profile_not_found")
        return self._profile(row)

    def update_profile(self, principal_id: str, **changes: str) -> PlayerProfile:
        principal_id = self._validate_principal(principal_id)
        allowed = {"gamertag", "motto", "location"}
        if not changes or set(changes) - allowed:
            raise InvalidProfile("invalid_profile_fields")
        validated = {}
        for key, value in changes.items():
            validated[key] = (
                self._validate_gamertag(value)
                if key == "gamertag"
                else self._validate_text(value, key)
            )
        assignments = ", ".join(key + " = ?" for key in validated)
        try:
            with self._connect() as connection:
                cursor = connection.execute(
                    "UPDATE player_profiles SET " + assignments + " WHERE principal_id = ?",
                    (*validated.values(), principal_id),
                )
                if cursor.rowcount == 0:
                    raise ProfileNotFound("profile_not_found")
        except sqlite3.IntegrityError as exc:
            raise ProfileAlreadyExists("gamertag_exists") from exc
        return self.get_profile(principal_id)


class ProfileServiceHandler:
    """JSON adapter for the internal security-gateway dispatcher."""

    def __init__(self, profiles: AccountProfileService) -> None:
        self._profiles = profiles

    def handle(self, context: AuthContext, payload: bytes) -> bytes:
        if len(payload) > 16 * 1024:
            return self._error("request_too_large")
        try:
            request = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return self._error("invalid_json")
        if not isinstance(request, dict):
            return self._error("invalid_request")

        operation = request.get("operation")
        try:
            if operation == "create":
                profile = self._profiles.create_profile(
                    context.principal_id,
                    request.get("gamertag"),
                    request.get("motto", ""),
                    request.get("location", ""),
                )
            elif operation == "get":
                profile = self._profiles.get_profile(context.principal_id)
            elif operation == "update":
                changes = request.get("changes")
                if not isinstance(changes, dict):
                    return self._error("invalid_profile_fields")
                profile = self._profiles.update_profile(context.principal_id, **changes)
            else:
                return self._error("unknown_operation")
        except ProfileError as exc:
            return self._error(str(exc))
        except (TypeError, ValueError):
            return self._error("invalid_profile_fields")
        return json.dumps(
            {
                "ok": True,
                "profile": {
                    "xuid": profile.xuid,
                    "gamertag": profile.gamertag,
                    "motto": profile.motto,
                    "location": profile.location,
                    "created_at": profile.created_at,
                },
            },
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")

    @staticmethod
    def _error(code: str) -> bytes:
        return json.dumps(
            {"ok": False, "error": code},
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
