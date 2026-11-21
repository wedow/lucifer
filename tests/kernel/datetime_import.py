import sys

# A fresh import must work with either backend blocked.
sys.modules['_pydatetime'] = None
import datetime
import _datetime

print(datetime.date is _datetime.date, datetime.UTC is datetime.timezone.utc)
print(type(datetime.datetime_CAPI).__name__, sys.modules['_pydatetime'] is None)
print(datetime.date.fromisoformat('2024-02-29') + datetime.timedelta(days=1))

del sys.modules['datetime']
del sys.modules['_pydatetime']
sys.modules['_datetime'] = None
import datetime as pure_datetime
import _pydatetime

print(pure_datetime.date is _pydatetime.date, pure_datetime.date is datetime.date)
print(hasattr(pure_datetime, 'datetime_CAPI'))
