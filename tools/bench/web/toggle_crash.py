"""Reproduces a JIT crash the web benchmark found: threads POST the toggle form
through Flask's test client, whose view returns redirect(request.referrer or "/").
Under the JIT, flask.helpers.redirect reads its `location` argument as NULL
after `current_app.__bool__` runs, and the process dies with SIGSEGV.

  PYTHONPATH=SITE bin/lucifer -X lucifer_jit=on tools/bench/web/toggle_crash.py [REQUESTS [THREADS]]

SITE is a site-packages with Flask for the 3.14t ABI. Prints "ok" when every
request answers 303. Run it through the host lock.
"""
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
from app import create_app  # noqa: E402


def main():
    requests = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    threads = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    app = create_app()

    def work():
        client = app.test_client()
        for i in range(requests):
            status = client.post(f"/todos/{i % 100 + 1}/toggle", headers={"Referer": "/"}).status_code
            assert status == 303, status

    workers = [threading.Thread(target=work) for _ in range(threads)]
    for t in workers:
        t.start()
    for t in workers:
        t.join()
    print("ok")


if __name__ == "__main__":
    main()
