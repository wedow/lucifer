# Exercise every byte position in an ASCII word and across word boundaries.
import codecs
import io
import pickle


for length in range(25):
    prefix = ('a\x00b' * 8)[:length]
    for suffix in ('', '\xe9', '\u20ac', '\U0001f600', '\xe9\u20ac\U0001f600\x00end'):
        text = prefix + suffix
        data = text.encode('utf-8')
        assert data.decode('utf-8') == str(data, 'utf-8') == text
        assert codecs.utf_8_decode(data, 'strict', False) == (text, len(data))
        assert io.TextIOWrapper(io.BytesIO(data), encoding='utf-8').read() == text
        assert pickle.loads(pickle.dumps(text, protocol=4)) == text

prefix = b'a\x00' * 8 + b'b'
assert codecs.utf_8_decode(prefix + b'\xe2\x82') == (prefix.decode(), len(prefix))
assert (prefix + b'\xffend').decode('utf-8', 'replace') == prefix.decode() + '\ufffdend'
surrogate = prefix.decode() + '\ud800'
assert surrogate.encode('utf-8', 'surrogatepass').decode('utf-8', 'surrogatepass') == surrogate
assert pickle.loads(pickle.dumps(surrogate, protocol=4)) == surrogate

# SHORT_BINUNICODE uses the native decoder. Check its full error span and reason.
for malformed in (b'\x80', b'\xe2\x82', b'\xe2\x82!', b'\xed\xa0!'):
    data = prefix + malformed
    blob = b'\x80\x04\x8c' + bytes([len(data)]) + data + b'.'
    for decode in (lambda: data.decode('utf-8', 'surrogatepass'), lambda: pickle.loads(blob)):
        try:
            decode()
        except UnicodeDecodeError as error:
            assert error.object == data
            print(error.encoding, error.start, error.end, error.reason, str(error))
        else:
            raise AssertionError('malformed UTF-8 decoded')
