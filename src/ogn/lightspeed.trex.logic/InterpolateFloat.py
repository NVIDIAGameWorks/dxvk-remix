# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.InterpolateFloatDatabase import InterpolateFloatDatabase


class InterpolateFloat:
    @staticmethod
    def compute(_db: InterpolateFloatDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: clampInput=bool, easingType=uint, inputMax=float, inputMin=float, output=float, outputMax=float, outputMin=float, shouldReverse=bool, value=float
