# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.EqualToDatabase import EqualToDatabase


class EqualTo:
    @staticmethod
    def compute(_db: EqualToDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: a=float, b=float, result=bool, tolerance=float
        # Combination 2: a=float[2], b=float[2], result=bool, tolerance=float
        # Combination 3: a=float[3], b=float[3], result=bool, tolerance=float
        # Combination 4: a=float[4], b=float[4], result=bool, tolerance=float
