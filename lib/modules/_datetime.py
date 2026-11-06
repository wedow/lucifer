"""_datetime (Modules/_datetimemodule.c): the pure-Python implementation, which
is compiled by the same JIT as the rest of the library, and the capsule C
extensions import (datetime.datetime_CAPI).

The C API's types must be static, so the classes are rebased onto static
bases; the type checks (PyDate_Check and its kin) hold by inheritance."""

from _lucifer import datetime_capi, datetime_type_bases
from _pydatetime import *
from _pydatetime import __all__
from _pydatetime import date, datetime, time, timedelta, tzinfo, timezone

_date_base, _datetime_base, _time_base, _delta_base, _tzinfo_base = datetime_type_bases()
date.__bases__ = (_date_base,)
datetime.__bases__ = (date, _datetime_base)
time.__bases__ = (_time_base,)
timedelta.__bases__ = (_delta_base,)
tzinfo.__bases__ = (_tzinfo_base,)

datetime_CAPI = datetime_capi(date, datetime, time, timedelta, tzinfo, timezone.utc)
__all__ = [*__all__, 'datetime_CAPI']
