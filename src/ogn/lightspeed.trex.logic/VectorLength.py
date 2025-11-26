# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.VectorLengthDatabase import VectorLengthDatabase


class VectorLength:
    @staticmethod
    def compute(_db: VectorLengthDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: input=float[2], length=float
        # Combination 2: input=float[3], length=float
        # Combination 3: input=float[4], length=float
