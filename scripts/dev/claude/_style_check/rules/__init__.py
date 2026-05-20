# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring

from . import cols_80
from . import pragma_once
from . import sizeof_parens
from . import static_fn_def_type_own_line
from . import comment_block_multiline_format

ALL_RULES = [
   cols_80.RULE,
   pragma_once.RULE,
   sizeof_parens.RULE,
   static_fn_def_type_own_line.RULE,
   comment_block_multiline_format.RULE,
]

RULES_BY_ID = { r.id: r for r in ALL_RULES }
