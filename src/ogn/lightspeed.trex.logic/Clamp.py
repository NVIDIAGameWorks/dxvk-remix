# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.ClampDatabase import ClampDatabase


class Clamp:
    @staticmethod
    def compute(_db: ClampDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: maxValue=float, minValue=float, result=float, value=float
        # Combination 2: maxValue=float, minValue=float, result=float[2], value=float[2]
        # Combination 3: maxValue=float, minValue=float, result=float[3], value=float[3]
        # Combination 4: maxValue=float, minValue=float, result=float[4], value=float[4]

        # Get attributes
        input_value = node.get_attribute("inputs:value")
        output_result = node.get_attribute("outputs:result")

        # Get current types of connected attributes
        type_value = input_value.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_value == og.Type(og.BaseDataType.FLOAT)):
            output_result.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 2)):
            output_result.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 3)):
            output_result.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 4)):
            output_result.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
