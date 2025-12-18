# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
import omni.graph.core as og

from lightspeed.trex.logic.ogn._impl.type_resolution import resolve_types, standard_compute, standard_initialize


class Normalize:
    # fmt: off
    VALID_COMBINATIONS = [
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 2), "outputs:output": og.Type(og.BaseDataType.FLOAT, 2)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 3), "outputs:output": og.Type(og.BaseDataType.FLOAT, 3)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 4), "outputs:output": og.Type(og.BaseDataType.FLOAT, 4)},
    ]
    # fmt: on

    compute = standard_compute
    initialize = standard_initialize

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        resolve_types(node, Normalize.VALID_COMBINATIONS)
