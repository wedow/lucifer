---
id: luc-5ot9
status: closed
deps: []
links: [luc-zj23]
created: 2026-11-14T23:58:41Z
type: feature
priority: 2
---
# _socket: datagram sendto/recvfrom/recvfrom_into and send/recv flags

lib/modules/_socket.py raises ENOSYS for sendto, recvfrom and recvfrom_into and for any send/recv flags, so asyncio datagram tests in test_asyncio.test_events (test_datagram_close_flushes_queued_data, test_datagram_write_error_reentrant_sendto, test_datagram_send_to_non_listening_address, in two loop classes) sit out 10-31 s timeouts each. Implement them as Modules/socketmodule.c: AF_INET/AF_INET6/AF_UNIX addresses, flags, timeouts, nonblocking calls, its argument errors.

## Acceptance Criteria

tests/kernel/socket_datagram.py matches the oracle on every lane; the six asyncio datagram tests pass without timing out.


## Notes

**2026-11-15T02:59:33Z**

Landed: sendto/recvfrom/recvfrom_into, send/recv flags, getnameinfo, AF_UNIX autobind. test_socket 749 TIMEOUT -> 236 PASS/500 SKIP/13 other (91 s); test_asyncio.test_events module TIMEOUT -> 206 PASS (56 s plain, 89 s jit=always); all asyncio datagram tests pass in the poll and select loops. sendmsg/recvmsg remain (luc-zj23).
