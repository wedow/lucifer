"""A small Todo app in Flask, built as a multi-page app, served over keep-alive HTTP/1.1.

The server renders every page with Jinja2: the list (/), one todo (/todos/ID)
and a create form (/new). Forms POST and redirect back (POST/redirect/GET). One
JSON endpoint (/api/todos) keeps JSON encoding in the mix.

The same file runs on every runtime: one process, a thread per connection
(http.server's ThreadingHTTPServer under a minimal WSGI adapter), so the runtime
is the only variable. werkzeug's development server is not used because it
closes every connection after one response. State is in-process behind one lock.

Usage: app.py PORT  (PORT 0 picks a free one; prints "listening PORT" once accepting)
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import sys
import threading
from urllib.parse import unquote_to_bytes

from flask import Flask, abort, jsonify, redirect, render_template, request
from jinja2 import DictLoader

PAGE = 20  # the list shows the newest PAGE todos, as a paginated app would

TEMPLATES = {
    "base.html": """<!doctype html>
<html><head><title>{% block title %}Todos{% endblock %}</title></head>
<body>
<nav><a href="/">All todos</a> | <a href="/new">New todo</a></nav>
{% block body %}{% endblock %}
</body></html>
""",
    "index.html": """{% extends "base.html" %}
{% block body %}<h1>Todos ({{ done }}/{{ total }} done)</h1>
<ul>
{% for t in todos %}  <li class="{{ 'done' if t.done else 'open' }}">
    <a href="/todos/{{ t.id }}">{{ t.title }}</a>
    <form method="post" action="/todos/{{ t.id }}/toggle"><button>{{ 'Reopen' if t.done else 'Done' }}</button></form>
  </li>
{% endfor %}</ul>
{% endblock %}
""",
    "todo.html": """{% extends "base.html" %}
{% block title %}{{ todo.title }}{% endblock %}
{% block body %}<h1>{{ todo.title }}</h1>
<p>#{{ todo.id }} is {{ 'done' if todo.done else 'open' }}.</p>
<form method="post" action="/todos/{{ todo.id }}/toggle"><button>{{ 'Reopen' if todo.done else 'Done' }}</button></form>
{% endblock %}
""",
    "new.html": """{% extends "base.html" %}
{% block title %}New todo{% endblock %}
{% block body %}<h1>New todo</h1>
{% if error %}<p class="error">{{ error }}</p>{% endif %}
<form method="post" action="/todos"><input name="title" required><button>Add</button></form>
{% endblock %}
""",
}


class Store:
    def __init__(self, seed):
        self.lock = threading.Lock()
        self.todos = {}
        self.done = 0
        for i in range(seed):
            self.create(f"seeded todo {i} <&>")

    def create(self, title):
        with self.lock:
            todo = {"id": len(self.todos) + 1, "title": title, "done": False}
            self.todos[todo["id"]] = todo
            return dict(todo)

    def get(self, todo_id):
        with self.lock:
            todo = self.todos.get(todo_id)
            return dict(todo) if todo else None

    def toggle(self, todo_id):
        with self.lock:
            todo = self.todos.get(todo_id)
            if todo is None:
                return False
            todo["done"] = not todo["done"]
            self.done += 1 if todo["done"] else -1
            return True

    def page(self):
        """The newest PAGE todos, newest first, with the total and done counts."""
        with self.lock:
            ids = range(len(self.todos), max(0, len(self.todos) - PAGE), -1)
            return [dict(self.todos[i]) for i in ids], len(self.todos), self.done


def create_app(seed=100):
    app = Flask(__name__)
    app.jinja_loader = DictLoader(TEMPLATES)
    store = Store(seed)

    @app.get("/")
    def index():
        todos, total, done = store.page()
        return render_template("index.html", todos=todos, total=total, done=done)

    @app.get("/todos/<int:todo_id>")
    def show(todo_id):
        return render_template("todo.html", todo=store.get(todo_id) or abort(404))

    @app.get("/new")
    def new():
        return render_template("new.html", error=None)

    @app.post("/todos")
    def create():
        title = request.form.get("title", "").strip()
        if not title:
            return render_template("new.html", error="A todo needs a title."), 400
        return redirect(f"/todos/{store.create(title)['id']}", 303)

    @app.post("/todos/<int:todo_id>/toggle")
    def toggle(todo_id):
        if not store.toggle(todo_id):
            abort(404)
        return redirect(request.referrer or "/", 303)

    @app.get("/api/todos")
    def api_list():
        return jsonify(store.page()[0])

    return app


class WSGIHandler(BaseHTTPRequestHandler):
    """One WSGI call per request on a keep-alive connection; the body is read in full first."""
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True  # headers and body are separate writes

    def handle(self):
        try:
            super().handle()
        except ConnectionError:
            pass  # the client went away mid-connection, as wrk does when it stops

    def run_wsgi(self):
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        path, _, query = self.path.partition("?")
        environ = {
            "REQUEST_METHOD": self.command, "SCRIPT_NAME": "", "QUERY_STRING": query,
            "PATH_INFO": unquote_to_bytes(path).decode("latin-1"),
            "CONTENT_TYPE": self.headers.get("Content-Type", ""), "CONTENT_LENGTH": str(len(body)),
            "SERVER_NAME": self.server.server_address[0], "SERVER_PORT": str(self.server.server_address[1]),
            "SERVER_PROTOCOL": self.request_version, "REMOTE_ADDR": self.client_address[0],
            "wsgi.version": (1, 0), "wsgi.url_scheme": "http", "wsgi.input": io.BytesIO(body),
            "wsgi.errors": sys.stderr, "wsgi.multithread": True, "wsgi.multiprocess": False, "wsgi.run_once": False,
        }
        for name, value in self.headers.items():
            key = "HTTP_" + name.upper().replace("-", "_")
            if key not in ("HTTP_CONTENT_TYPE", "HTTP_CONTENT_LENGTH"):
                environ[key] = value
        response = []

        def start_response(status, headers, exc_info=None):
            response[:] = [status, headers]

        chunks = self.server.app(environ, start_response)
        try:
            payload = b"".join(chunks)
        finally:
            getattr(chunks, "close", lambda: None)()
        status, headers = response
        self.send_response(int(status[:3]), status[4:])
        for name, value in headers:
            if name.lower() != "content-length":
                self.send_header(name, value)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_GET = do_POST = run_wsgi

    def log_message(self, *args):
        pass


class Server(ThreadingHTTPServer):
    request_queue_size = 1024  # socketserver's 5 drops a burst of connects into a 1 s SYN retry


def main():
    server = Server(("127.0.0.1", int(sys.argv[1])), WSGIHandler)
    server.app = create_app()
    print(f"listening {server.server_port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
