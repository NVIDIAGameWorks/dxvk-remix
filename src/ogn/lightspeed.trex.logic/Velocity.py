# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.VelocityDatabase import VelocityDatabase


class Velocity:
    @staticmethod
    def compute(_db: VelocityDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: input=float, previousValue=float, velocity=float
        # Combination 2: input=float[2], previousValue=float[2], velocity=float[2]
        # Combination 3: input=float[3], previousValue=float[3], velocity=float[3]
        # Combination 4: input=float[4], previousValue=float[4], velocity=float[4]

        # Get attributes
        input_input = node.get_attribute("inputs:input")
        output_velocity = node.get_attribute("outputs:velocity")

        # Get current types of connected attributes
        type_input = input_input.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_input == og.Type(og.BaseDataType.FLOAT)):
            output_velocity.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 2)):
            output_velocity.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 3)):
            output_velocity.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 4)):
            output_velocity.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
