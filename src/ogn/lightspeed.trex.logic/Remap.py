# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.RemapDatabase import RemapDatabase


class Remap:
    @staticmethod
    def compute(_db: RemapDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: clampInput=bool, easingType=uint, inputMax=float, inputMin=float, output=float, outputMax=float, outputMin=float, shouldReverse=bool, value=float
        # Combination 2: clampInput=bool, easingType=uint, inputMax=float, inputMin=float, output=float[2], outputMax=float[2], outputMin=float[2], shouldReverse=bool, value=float
        # Combination 3: clampInput=bool, easingType=uint, inputMax=float, inputMin=float, output=float[3], outputMax=float[3], outputMin=float[3], shouldReverse=bool, value=float
        # Combination 4: clampInput=bool, easingType=uint, inputMax=float, inputMin=float, output=float[4], outputMax=float[4], outputMin=float[4], shouldReverse=bool, value=float

        # Get attributes
        input_outputMin = node.get_attribute("inputs:outputMin")
        input_outputMax = node.get_attribute("inputs:outputMax")
        output_output = node.get_attribute("outputs:output")

        # Get current types of connected attributes
        type_outputMin = input_outputMin.get_resolved_type()
        type_outputMax = input_outputMax.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_outputMin == og.Type(og.BaseDataType.FLOAT) and 
            type_outputMax == og.Type(og.BaseDataType.FLOAT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_outputMin == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_outputMax == og.Type(og.BaseDataType.FLOAT, 2)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_outputMin == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_outputMax == og.Type(og.BaseDataType.FLOAT, 3)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_outputMin == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_outputMax == og.Type(og.BaseDataType.FLOAT, 4)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
