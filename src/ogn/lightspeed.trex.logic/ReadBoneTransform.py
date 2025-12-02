# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.ReadBoneTransformDatabase import ReadBoneTransformDatabase


class ReadBoneTransform:
    @staticmethod
    def compute(_db: ReadBoneTransformDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: boneIndex=float, position=float[3], rotation=float[4], scale=float[3], target=target
