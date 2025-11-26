# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.SmoothDatabase import SmoothDatabase


class Smooth:
    @staticmethod
    def compute(_db: SmoothDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: initialized=bool, input=float, output=float, smoothingFactor=float
        # Combination 2: initialized=bool, input=float[2], output=float[2], smoothingFactor=float
        # Combination 3: initialized=bool, input=float[3], output=float[3], smoothingFactor=float
        # Combination 4: initialized=bool, input=float[4], output=float[4], smoothingFactor=float

        # Get attributes
        input_input = node.get_attribute("inputs:input")
        output_output = node.get_attribute("outputs:output")

        # Get current types of connected attributes
        type_input = input_input.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_input == og.Type(og.BaseDataType.FLOAT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 2)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 3)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 4)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
