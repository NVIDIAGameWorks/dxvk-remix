# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.SelectDatabase import SelectDatabase


class Select:
    @staticmethod
    def compute(_db: SelectDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: condition=bool, inputA=bool, inputB=bool, output=bool
        # Combination 2: condition=bool, inputA=float, inputB=float, output=float
        # Combination 3: condition=bool, inputA=float[2], inputB=float[2], output=float[2]
        # Combination 4: condition=bool, inputA=float[3], inputB=float[3], output=float[3]
        # Combination 5: condition=bool, inputA=float[4], inputB=float[4], output=float[4]
        # Combination 6: condition=bool, inputA=uint, inputB=uint, output=uint
        # Combination 7: condition=bool, inputA=token, inputB=token, output=token
        # Combination 8: condition=bool, inputA=target, inputB=target, output=target

        # Get attributes
        input_inputA = node.get_attribute("inputs:inputA")
        input_inputB = node.get_attribute("inputs:inputB")
        output_output = node.get_attribute("outputs:output")

        # Get current types of connected attributes
        type_inputA = input_inputA.get_resolved_type()
        type_inputB = input_inputB.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_inputA == og.Type(og.BaseDataType.BOOL) and 
            type_inputB == og.Type(og.BaseDataType.BOOL)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.BOOL))
        elif (type_inputA == og.Type(og.BaseDataType.FLOAT) and 
            type_inputB == og.Type(og.BaseDataType.FLOAT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_inputA == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_inputB == og.Type(og.BaseDataType.FLOAT, 2)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_inputA == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_inputB == og.Type(og.BaseDataType.FLOAT, 3)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_inputA == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_inputB == og.Type(og.BaseDataType.FLOAT, 4)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
        elif (type_inputA == og.Type(og.BaseDataType.UINT) and 
            type_inputB == og.Type(og.BaseDataType.UINT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.UINT))
        elif (type_inputA == og.Type(og.BaseDataType.TOKEN) and 
            type_inputB == og.Type(og.BaseDataType.TOKEN)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.TOKEN))
        elif (type_inputA == og.Type(og.BaseDataType.RELATIONSHIP) and 
            type_inputB == og.Type(og.BaseDataType.RELATIONSHIP)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.RELATIONSHIP))
