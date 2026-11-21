"""_datetime (Modules/_datetimemodule.c): the pure-Python implementation, which
is compiled by the same JIT as the rest of the library, and the capsule C
extensions import (datetime.datetime_CAPI)."""

from _lucifer import datetime_capi
from _datetime_impl import *
from _datetime_impl import __all__

datetime_CAPI = datetime_capi(date, datetime, time, timedelta, tzinfo, timezone.utc)
__all__ = [*__all__, 'datetime_CAPI']
