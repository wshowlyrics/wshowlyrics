#!/usr/bin/env python3
"""A stand-in for lrclib.net that fails on purpose.

Started by run.sh next to it; see there. Standard library only.

    mock.py PORT REQUEST_LOG PID_FILE

What it answers depends on the track_name of the request:

    retry-ok    /api/get: 503, 503, then 200 with synced lyrics
    ratelimit   /api/get: 429 with "Retry-After: 2", then 200
    toolong     /api/get: 429 with "Retry-After: 60", every time
    down        /api/get: 503, every time
    cancel      /api/get: 503, every time (the test changes track meanwhile)
    notfound    /api/get: 404; /api/search: an empty list
    rejected    /api/get: 400; /api/search: an empty list
    huge        /api/get: 200 with a 3 MB body and no Content-Length
    hugelen     /api/get: 200 announcing a 3 MB body (Content-Length)

Every request is appended to REQUEST_LOG as
    <ms since the first request for that track_name> <track_name> <path> -> <status> | <User-Agent>
"""
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

port, request_log, pid_file = int(sys.argv[1]), sys.argv[2], sys.argv[3]
counts = {}      # track_name -> number of /api/get requests seen
first_seen = {}  # track_name -> time of its first request

LYRICS = "[00:01.00] mock line one\n[00:05.00] mock line two\n[00:09.00] mock line three"


def exact_match(name, duration):
    # Compact, as lrclib.net sends it: no space after the colons
    return json.dumps({
        "id": 1, "trackName": name, "artistName": "Mock", "albumName": "Mock",
        "duration": duration, "instrumental": False,
        "plainLyrics": "mock line one\nmock line two\nmock line three",
        "syncedLyrics": LYRICS,
    }, separators=(",", ":"))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def answer(self, status, body="", headers=None):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(data)
        return status
    def flood(self, announce):
        """200 with a 3 MB body; without a Content-Length unless announce."""
        size = 3 * 1024 * 1024
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        if announce:
            self.send_header("Content-Length", str(size))
        self.end_headers()
        try:
            chunk = b'{"x":"' + b"a" * 65000 + b'"}'
            sent = 0
            while sent < size:
                self.wfile.write(chunk)
                sent += len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass  # the client hung up, as it should
        return 200


    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        name = query.get("track_name", [""])[0]
        if url.path == "/ping":
            self.answer(200, "pong")
            return

        now = time.monotonic()
        first_seen.setdefault(name, now)
        status = self.respond(url.path, name, query)
        with open(request_log, "a") as log:
            log.write("%5d ms  %-9s %-11s -> %d | %s\n" % (
                (now - first_seen[name]) * 1000, name, url.path, status,
                self.headers.get("User-Agent", "-")))

    def respond(self, path, name, query):
        error = json.dumps({"statusCode": 503, "name": "ServiceUnavailable"})
        if path == "/api/search":
            return self.answer(200, "[]")
        if path != "/api/get":
            return self.answer(404, "{}")

        counts[name] = counts.get(name, 0) + 1
        seen = counts[name]
        duration = int(query.get("duration", ["30"])[0])
        if name == "retry-ok":
            return self.answer(503, error) if seen <= 2 else self.answer(200, exact_match(name, duration))
        if name == "ratelimit":
            if seen == 1:
                return self.answer(429, "rate limited", {"Retry-After": "2"})
            return self.answer(200, exact_match(name, duration))
        if name == "toolong":
            return self.answer(429, "rate limited", {"Retry-After": "60"})
        if name in ("huge", "hugelen"):
            return self.flood(announce=(name == "hugelen"))
        if name == "rejected":
            return self.answer(400, json.dumps({"statusCode": 400, "name": "BadRequest"}))
        if name in ("down", "cancel"):
            return self.answer(503, error)
        return self.answer(404, json.dumps({"statusCode": 404, "name": "TrackNotFound"}))


with open(pid_file, "w") as f:
    f.write(str(os.getpid()))
ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
