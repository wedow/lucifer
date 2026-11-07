---
id: luc-uzce
status: open
deps: []
links: []
created: 2026-11-06T04:58:05Z
type: feature
priority: 2
---
# CJK codecs: _multibytecodec and _codecs_cn/_jp/_kr/_tw/_hk/_iso2022

codecs.lookup('gbk'), 'shift_jis', 'big5', 'euc_kr' and the rest of the CJK encodings fail with LookupError: the encodings package imports _codecs_cn etc. (Modules/cjkcodecs), which lucifer does not have. test_capi.test_unicode CAPITest.test_equaltoutf8andsize fails at its last check ('北京市'.encode('gbk')); test_codecencodings_* and test_multibytecodec depend on it too.


## Notes

**2026-11-07T18:46:10Z**

charset_normalizer imports _multibytecodec; without it requests warns (RequestsDependencyWarning) under the corpus sphinx benchmark.
