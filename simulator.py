"""
simulator.py  —  Python Controller
════════════════════════════════════
Middleware between the C engine and the HTML dashboard.

Responsibilities
────────────────
  • Input validation with detailed error messages
  • Calls compiled C binary via subprocess
  • Enriches results: efficiency rank, heatmap data,
    access frequency analysis, normalised scores
  • Serves the HTML dashboard on localhost
  • REST API  :  POST /api/simulate   GET /api/export/csv
  • Supports batch simulation (compare workloads)

Subject : CSE-316 Operating Systems | CA2 Project
"""

from __future__ import annotations
import subprocess, json, os, sys, csv, io, math
import statistics, random, argparse, threading, webbrowser
import http.server, socketserver, urllib.parse
from pathlib import Path
from typing  import Any

# ════════════════════════════════════════════════
#  PATHS
# ════════════════════════════════════════════════

BASE      = Path(__file__).resolve().parent
ROOT      = BASE.parent                            # project root
BIN       = ROOT / "core" / "disk_scheduler"
DASHBOARD = ROOT / "dashboard"
WEB       = DASHBOARD / "index.html"
OUT       = BASE / "output"
OUT.mkdir(exist_ok=True)


# ════════════════════════════════════════════════
#  CUSTOM EXCEPTIONS
# ════════════════════════════════════════════════

class ValidationError(ValueError): pass
class EngineError(RuntimeError):   pass


# ════════════════════════════════════════════════
#  INPUT VALIDATION
# ════════════════════════════════════════════════

def validate(disk_size: int, head: int,
             requests: list[int], direction: str) -> None:
    if not isinstance(disk_size, int) or not (10 <= disk_size <= 100_000):
        raise ValidationError(
            f"disk_size must be an integer in [10, 100000]. Got: {disk_size!r}")

    if not isinstance(head, int) or not (0 <= head < disk_size):
        raise ValidationError(
            f"head_position {head} out of range [0, {disk_size-1}]")

    if not requests:
        raise ValidationError("Request queue is empty.")

    if len(requests) > 512:
        raise ValidationError(
            f"Maximum 512 requests allowed. Got {len(requests)}.")

    bad = [r for r in requests if not (0 <= r < disk_size)]
    if bad:
        raise ValidationError(
            f"Requests out of range [0,{disk_size-1}]: {bad[:5]}")

    if direction not in ("left", "right"):
        raise ValidationError(
            f"direction must be 'left' or 'right'. Got: {direction!r}")


# ════════════════════════════════════════════════
#  C ENGINE INTERFACE
# ════════════════════════════════════════════════

def call_engine(disk_size: int, head: int, requests: list[int],
                direction: str, algo: str = "all") -> list[dict]:
    """
    Invokes the C binary in JSON mode.
    Returns a list of result dicts (one per algorithm).
    """
    if not BIN.exists():
        raise EngineError(
            f"C binary not found at {BIN}\n"
            "Run:  make  (from the project root)\n"
            "  or:  gcc -O2 -Wall -std=c99 -I core/include "
            "core/src/main.c core/src/algorithms.c core/src/output.c "
            "-lm -o core/disk_scheduler")

    cmd = [
        str(BIN), "--json",
        "--head",     str(head),
        "--size",     str(disk_size),
        "--requests", ",".join(map(str, requests)),
        "--dir",      direction,
        "--algo",     algo,
    ]

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    if proc.returncode != 0:
        raise EngineError(f"C engine exited {proc.returncode}:\n{proc.stderr}")

    raw = proc.stdout.strip()
    parsed = json.loads(raw)
    return parsed if isinstance(parsed, list) else [parsed]


# ════════════════════════════════════════════════
#  POST-PROCESSING
# ════════════════════════════════════════════════

def enrich(results: list[dict], requests: list[int],
           disk_size: int) -> list[dict]:
    """
    Adds metrics the C engine doesn't compute:
      - efficiency_pct   (0–100%, 100 = best algorithm)
      - normalised_score (0–1)
      - rank
      - access_heatmap   (frequency of each cylinder band)
      - percentile_seek  (50th / 95th percentile step distance)
    """
    if not results:
        return results
    dists = [r["total_seek_distance"] for r in results]
    mn, mx = min(dists), max(dists)

    # Access frequency heatmap — divide disk into 20 bands
    BANDS = 20
    band_w = max(1, disk_size // BANDS)
    freq = [0] * BANDS
    for cyl in requests:
        freq[min(cyl // band_w, BANDS - 1)] += 1

    for r in results:
        d = r["total_seek_distance"]

        # Efficiency
        r["efficiency_pct"] = round(
            100.0 * (mx - d) / (mx - mn), 1) if mx != mn else 100.0

        # Normalised score (lower = better, 0 = best)
        r["normalised_score"] = round((d - mn) / (mx - mn), 4) if mx != mn else 0.0

        # Percentile seek distances
        step_dists = [s["dist"] for s in r.get("steps", [])]
        if step_dists:
            sd_sorted = sorted(step_dists)
            n = len(sd_sorted)
            r["p50_seek"] = sd_sorted[int(n * 0.50)]
            r["p95_seek"] = sd_sorted[min(int(n * 0.95), n-1)]
        else:
            r["p50_seek"] = r["p95_seek"] = 0

        # Access heatmap (shared across all algorithms)
        r["access_heatmap"] = freq
        r["band_width"]     = band_w

        # Flag
        r["is_best"] = (d == mn)

    # Sort by total seek distance, assign rank
    results.sort(key=lambda x: x["total_seek_distance"])
    for i, r in enumerate(results):
        r["rank"] = i + 1

    return results


def generate_random(disk_size: int = 200,
                    count: int = 8) -> tuple[list[int], int]:
    reqs = random.sample(range(disk_size), min(count, disk_size))
    head = random.randint(0, disk_size - 1)
    return reqs, head


# ════════════════════════════════════════════════
#  CSV EXPORT
# ════════════════════════════════════════════════

def to_csv(results: list[dict], config: dict) -> str:
    """Returns CSV content as a string."""
    buf = io.StringIO()
    w   = csv.writer(buf)

    w.writerow(["=== Disk Scheduling Simulator — Results ==="])
    w.writerow(["Disk Size",    config["disk_size"]])
    w.writerow(["Head Start",   config["head"]])
    w.writerow(["Direction",    config["direction"]])
    w.writerow(["Requests",     ", ".join(map(str, config["requests"]))])
    w.writerow([])

    w.writerow([
        "Rank", "Algorithm", "Total Seek", "Avg Seek",
        "Std Dev", "P50 Seek", "P95 Seek",
        "Throughput", "Efficiency%", "Starvation Events", "Best?"
    ])
    for r in results:
        w.writerow([
            r["rank"],
            r["algorithm"],
            r["total_seek_distance"],
            f"{r['avg_seek_distance']:.2f}",
            f"{r.get('std_deviation',0):.2f}",
            r.get("p50_seek", "—"),
            r.get("p95_seek", "—"),
            f"{r['throughput']:.8f}",
            f"{r['efficiency_pct']:.1f}%",
            r.get("starvation_count", 0),
            "✓ BEST" if r["is_best"] else "",
        ])

    w.writerow([])
    w.writerow(["=== Seek Sequences ==="])
    for r in results:
        w.writerow([r["algorithm"]] + r["seek_sequence"])

    return buf.getvalue()


# ════════════════════════════════════════════════
#  HTTP SERVER  (Dashboard + REST API)
# ════════════════════════════════════════════════

class Handler(http.server.BaseHTTPRequestHandler):

    _last_results : list[dict] = []
    _last_config  : dict       = {}

    def log_message(self, *_): pass   # silence access log

    # ── Routing ──────────────────────────────────
    # MIME type map for static assets
    _MIME = {
        ".html": "text/html; charset=utf-8",
        ".css":  "text/css; charset=utf-8",
        ".js":   "application/javascript; charset=utf-8",
    }

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path

        if path in ("/", "/index.html"):
            self._serve_file(WEB, "text/html; charset=utf-8")
        elif path == "/api/export/csv":
            self._export_csv()
        elif path == "/api/export/json":
            self._export_json()
        else:
            # Serve static dashboard assets (styles.css, script.js, etc.)
            fname = path.lstrip("/")
            fpath = DASHBOARD / fname
            ext   = Path(fname).suffix
            mime  = self._MIME.get(ext)
            if mime and fpath.exists() and fpath.resolve().parent == DASHBOARD.resolve():
                self._serve_file(fpath, mime)
            else:
                self._json({"error": "not found"}, 404)

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/simulate":
            self._simulate()
        else:
            self._json({"error": "not found"}, 404)

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors_headers()
        self.end_headers()

    # ── Simulate endpoint ─────────────────────────
    def _simulate(self):
        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length)
        try:
            p         = json.loads(body)
            disk_size = int(p["disk_size"])
            head      = int(p["head"])
            requests  = [int(x) for x in p["requests"]]
            direction = p.get("direction", "right")
            algos     = p.get("algorithms", ["fcfs","sstf","scan","cscan","look"])

            validate(disk_size, head, requests, direction)

            results = call_engine(disk_size, head, requests, direction)

            # Filter to requested algos — normalise both sides for comparison
            def _norm(name):
                return name.lower().replace('-', '').replace('_', '')
            algos_norm = [_norm(a) for a in algos]
            results = [r for r in results
                       if _norm(r["algorithm"]) in algos_norm]

            if not results:
                raise ValidationError("No algorithms selected. Please enable at least one algorithm.")

            results = enrich(results, requests, disk_size)

            config = {"disk_size": disk_size, "head": head,
                      "requests": requests, "direction": direction}
            Handler._last_results = results
            Handler._last_config  = config

            self._json({"status": "ok", "results": results, "config": config})

        except ValidationError as e:
            self._json({"status": "error", "type": "validation", "message": str(e)}, 400)
        except EngineError as e:
            self._json({"status": "error", "type": "engine",     "message": str(e)}, 500)
        except Exception as e:
            self._json({"status": "error", "type": "unknown",    "message": str(e)}, 500)

    # ── Export endpoints ─────────────────────────
    def _export_csv(self):
        if not Handler._last_results:
            self._json({"error": "no results yet"}, 400); return
        data = to_csv(Handler._last_results, Handler._last_config).encode()
        self.send_response(200)
        self._cors_headers()
        self.send_header("Content-Type",        "text/csv")
        self.send_header("Content-Disposition", 'attachment; filename="disk_results.csv"')
        self.send_header("Content-Length",      len(data))
        self.end_headers()
        self.wfile.write(data)

    def _export_json(self):
        if not Handler._last_results:
            self._json({"error": "no results yet"}, 400); return
        data = json.dumps({
            "config": Handler._last_config,
            "results": Handler._last_results
        }, indent=2).encode()
        self.send_response(200)
        self._cors_headers()
        self.send_header("Content-Type",        "application/json")
        self.send_header("Content-Disposition", 'attachment; filename="disk_results.json"')
        self.send_header("Content-Length",      len(data))
        self.end_headers()
        self.wfile.write(data)

    # ── Helpers ───────────────────────────────────
    def _serve_file(self, path: Path, ctype: str):
        if not path.exists():
            self._json({"error": f"file not found: {path}"}, 404); return
        data = path.read_bytes()
        self.send_response(200)
        self._cors_headers()
        self.send_header("Content-Type",   ctype)
        self.send_header("Content-Length", len(data))
        self.end_headers()
        self.wfile.write(data)

    def _json(self, data, code=200):
        body = json.dumps(data).encode()
        self.send_response(code)
        self._cors_headers()
        self.send_header("Content-Type",   "application/json")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def _cors_headers(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")


# ════════════════════════════════════════════════
#  LAUNCH
# ════════════════════════════════════════════════

def launch(port: int = 8080):
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", port), Handler) as srv:
        url = f"http://localhost:{port}"
        print(f"\n  ┌────────────────────────────────────────────┐")
        print(f"  │  Disk Scheduling Simulator  v2.0           │")
        print(f"  │  Dashboard → {url}            │")
        print(f"  │  Press Ctrl+C to stop                      │")
        print(f"  └────────────────────────────────────────────┘\n")
        threading.Timer(1.2, webbrowser.open, args=[url]).start()
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            print("\n  Stopped.")


# ════════════════════════════════════════════════
#  CLI  (python simulator.py run ...)
# ════════════════════════════════════════════════

def cli_run(args):
    reqs = [int(x.strip()) for x in args.requests.split(",")]
    validate(args.size, args.head, reqs, args.direction)
    results = call_engine(args.size, args.head, reqs, args.direction)
    results = enrich(results, reqs, args.size)
    config  = {"disk_size": args.size, "head": args.head,
               "requests": reqs, "direction": args.direction}

    print(f"\n  {'Rank':<5} {'Algorithm':<10} {'Seek':>8} {'Avg':>8}  {'Eff%':>6}  {'Best'}")
    print(f"  {'─'*55}")
    for r in results:
        print(f"  {r['rank']:<5} {r['algorithm']:<10} "
              f"{r['total_seek_distance']:>8} "
              f"{r['avg_seek_distance']:>8.2f}  "
              f"{r['efficiency_pct']:>5.1f}%  "
              f"{'★' if r['is_best'] else ''}")

    csv_path = OUT / "results.csv"
    csv_path.write_text(to_csv(results, config))
    print(f"\n  CSV saved → {csv_path}\n")


# ════════════════════════════════════════════════
#  ENTRY
# ════════════════════════════════════════════════

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Disk Scheduling Simulator")
    sp = ap.add_subparsers(dest="cmd")

    dp = sp.add_parser("dashboard")
    dp.add_argument("--port", type=int, default=8080)

    rp = sp.add_parser("run")
    rp.add_argument("--size",      type=int, default=200)
    rp.add_argument("--head",      type=int, default=53)
    rp.add_argument("--requests",  default="98,183,37,122,14,124,65,67")
    rp.add_argument("--direction", default="right", choices=["left","right"])

    args = ap.parse_args()
    if   args.cmd == "run":       cli_run(args)
    elif args.cmd == "dashboard": launch(args.port)
    else:                         launch()
