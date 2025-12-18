# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
import omni.graph.core as og

from lightspeed.trex.logic.ogn._impl.type_resolution import resolve_types, standard_compute, standard_initialize


class Clamp:
    # fmt: off
    VALID_COMBINATIONS = [
        {"inputs:value": og.Type(og.BaseDataType.FLOAT), "outputs:result": og.Type(og.BaseDataType.FLOAT)},
        {"inputs:value": og.Type(og.BaseDataType.FLOAT, 2), "outputs:result": og.Type(og.BaseDataType.FLOAT, 2)},
        {"inputs:value": og.Type(og.BaseDataType.FLOAT, 3), "outputs:result": og.Type(og.BaseDataType.FLOAT, 3)},
        {"inputs:value": og.Type(og.BaseDataType.FLOAT, 4), "outputs:result": og.Type(og.BaseDataType.FLOAT, 4)},
    ]
    # fmt: on

    compute = standard_compute
    initialize = standard_initialize

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        resolve_types(node, Clamp.VALID_COMBINATIONS)
