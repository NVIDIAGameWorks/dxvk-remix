# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

import omni.graph.core as og
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.OgnRtxOptionLayerActionDatabase import OgnRtxOptionLayerActionDatabase


class RtxOptionLayerAction:
    @staticmethod
    def compute(_db: OgnRtxOptionLayerActionDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: blendStrength=float, blendThreshold=float, configPath=token, enabled=bool, holdsReference=bool, priority=uint

