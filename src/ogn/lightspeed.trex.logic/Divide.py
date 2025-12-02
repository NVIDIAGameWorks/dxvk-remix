# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.DivideDatabase import DivideDatabase


class Divide:
    @staticmethod
    def compute(_db: DivideDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: a=float, b=float, quotient=float
        # Combination 2: a=float[2], b=float, quotient=float[2]
        # Combination 3: a=float[2], b=float[2], quotient=float[2]
        # Combination 4: a=float[3], b=float, quotient=float[3]
        # Combination 5: a=float[3], b=float[3], quotient=float[3]
        # Combination 6: a=float[4], b=float, quotient=float[4]
        # Combination 7: a=float[4], b=float[4], quotient=float[4]

        # Get attributes
        input_a = node.get_attribute("inputs:a")
        input_b = node.get_attribute("inputs:b")
        output_quotient = node.get_attribute("outputs:quotient")

        # Get current types of connected attributes
        type_a = input_a.get_resolved_type()
        type_b = input_b.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_a == og.Type(og.BaseDataType.FLOAT) and 
            type_b == og.Type(og.BaseDataType.FLOAT)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_b == og.Type(og.BaseDataType.FLOAT)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_b == og.Type(og.BaseDataType.FLOAT, 2)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_b == og.Type(og.BaseDataType.FLOAT)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_b == og.Type(og.BaseDataType.FLOAT, 3)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_b == og.Type(og.BaseDataType.FLOAT)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
        elif (type_a == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_b == og.Type(og.BaseDataType.FLOAT, 4)):
            output_quotient.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
