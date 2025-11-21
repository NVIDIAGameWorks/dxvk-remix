# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

import omni.graph.core as og
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.OgnTimeDatabase import OgnTimeDatabase


class Time:
    @staticmethod
    def compute(_db: OgnTimeDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: accumulatedTime=float, currentTime=float, enabled=bool, speedMultiplier=float

