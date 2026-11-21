"""Adversarial JSON-line peer used only for adapter lifecycle tests."""
import json
import sys
import threading
import time

lock = threading.Lock()
workers = []


def respond(request):
    time.sleep(request.get("delay", 0))
    response = {"id": request["id"], "ok": True, "result": {"echo": request.get("value"), "op": request["op"]}}
    with lock:
        print(json.dumps(response), flush=True)


for line in sys.stdin:
    request = json.loads(line)
    if request["op"] == "exit":
        sys.exit(7)
    if request["op"] == "malformed":
        print("not JSON", flush=True)
        continue
    if request["op"] == "shutdown":
        respond(request)
        break
    worker = threading.Thread(target=respond, args=(request,))
    worker.start()
    workers.append(worker)
for worker in workers:
    worker.join()
