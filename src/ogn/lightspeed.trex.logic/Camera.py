# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

import omni.graph.core as og
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.OgnCameraDatabase import OgnCameraDatabase


class Camera:
    @staticmethod
    def compute(_db: OgnCameraDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: aspectRatio=float, farPlane=float, forward=float[3], fovDegrees=float, fovRadians=float, nearPlane=float, position=float[3], right=float[3], up=float[3]

