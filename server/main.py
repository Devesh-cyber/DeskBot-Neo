"""
Desk Bot backend (Phase 2)
Run:  uvicorn main:app --host 0.0.0.0 --port 8000
Phone: open http://<laptop-ip>:8000   (find the IP with: hostname -I)
ESP32: GET http://<laptop-ip>:8000/api/state
"""
import json
import time
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

DATA_FILE = Path(__file__).with_name("data.json")
app = FastAPI(title="Desk Bot")

# ---------- storage (tiny JSON file so todos survive restarts) ----------
state = {"todos": [], "next_id": 1}
timer = {"end_at": None, "total": 0, "stopped": True}  # runtime only


def load() -> None:
    if DATA_FILE.exists():
        state.update(json.loads(DATA_FILE.read_text()))


def save() -> None:
    DATA_FILE.write_text(json.dumps(state, indent=2))


load()


# ---------- models ----------
class TodoIn(BaseModel):
    text: str


class TimerIn(BaseModel):
    minutes: float


# ---------- todos ----------
@app.get("/api/todos")
def list_todos():
    return state["todos"]


@app.post("/api/todos")
def add_todo(body: TodoIn):
    text = body.text.strip()
    if not text:
        raise HTTPException(400, "empty todo")
    todo = {"id": state["next_id"], "text": text, "done": False}
    state["next_id"] += 1
    state["todos"].append(todo)
    save()
    return todo


@app.post("/api/todos/{todo_id}/toggle")
def toggle_todo(todo_id: int):
    for t in state["todos"]:
        if t["id"] == todo_id:
            t["done"] = not t["done"]
            save()
            return t
    raise HTTPException(404, "not found")


@app.delete("/api/todos/{todo_id}")
def delete_todo(todo_id: int):
    before = len(state["todos"])
    state["todos"] = [t for t in state["todos"] if t["id"] != todo_id]
    if len(state["todos"]) == before:
        raise HTTPException(404, "not found")
    save()
    return {"ok": True}


# ---------- timer ----------
def timer_view() -> dict:
    if timer["end_at"] is None:
        return {"status": "idle", "remaining": 0, "total": 0}
    remaining = int(round(timer["end_at"] - time.time()))
    if timer["stopped"]:
        return {"status": "idle", "remaining": 0, "total": 0}
    if remaining <= 0:
        return {"status": "finished", "remaining": 0, "total": timer["total"]}
    return {"status": "running", "remaining": remaining, "total": timer["total"]}


@app.post("/api/timer/start")
def start_timer(body: TimerIn):
    if body.minutes <= 0 or body.minutes > 600:
        raise HTTPException(400, "minutes must be between 0 and 600")
    timer["total"] = int(body.minutes * 60)
    timer["end_at"] = time.time() + timer["total"]
    timer["stopped"] = False
    return timer_view()


@app.post("/api/timer/stop")
def stop_timer():
    timer["stopped"] = True
    timer["end_at"] = None
    return timer_view()


# ---------- compact state for the ESP32 ----------
def ascii_only(s: str, limit: int = 40) -> str:
    # the default OLED font only draws plain ASCII
    return s.encode("ascii", "ignore").decode()[:limit]


@app.get("/api/state")
def esp_state():
    pending = [t for t in state["todos"] if not t["done"]]
    return {
        "task": ascii_only(pending[0]["text"]) if pending else "",
        "pending": len(pending),
        "timer": timer_view(),
    }


# ---------- phone page ----------
PAGE = """<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Desk Bot</title>
<style>
  body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:520px;margin:auto}
  h1{font-size:20px;margin:0 0 12px}
  .card{background:#1c1c1c;border-radius:12px;padding:14px;margin-bottom:14px}
  input{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;border:1px solid #333;background:#111;color:#eee;font-size:16px}
  button{padding:10px 14px;border:0;border-radius:8px;background:#2f6fed;color:#fff;font-size:15px;margin:4px 4px 0 0}
  button.grey{background:#444} button.red{background:#c0392b}
  .time{font-size:42px;text-align:center;margin:6px 0;font-variant-numeric:tabular-nums}
  .status{text-align:center;color:#999;margin-bottom:8px}
  .todo{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid #2a2a2a}
  .todo:last-child{border:0} .todo span{flex:1} .done span{text-decoration:line-through;color:#777}
  .x{background:none;color:#888;padding:4px 8px;margin:0}
</style></head><body>
<h1>Desk Bot</h1>

<div class="card">
  <div class="status" id="tstatus">idle</div>
  <div class="time" id="tleft">--:--</div>
  <div style="text-align:center">
    <button onclick="startTimer(25)">Focus 25</button>
    <button onclick="startTimer(5)">Break 5</button>
    <button onclick="startTimer(15)">Break 15</button>
    <button class="red" onclick="stopTimer()">Stop</button>
  </div>
  <div style="display:flex;gap:8px;margin-top:10px">
    <input id="mins" type="number" inputmode="decimal" placeholder="custom minutes">
    <button onclick="startTimer(parseFloat(document.getElementById('mins').value))">Start</button>
  </div>
</div>

<div class="card">
  <div style="display:flex;gap:8px">
    <input id="newtodo" placeholder="Add a todo..." onkeydown="if(event.key==='Enter')addTodo()">
    <button onclick="addTodo()">Add</button>
  </div>
  <div id="list"></div>
</div>

<script>
const $ = id => document.getElementById(id);
async function api(path, method='GET', body){
  const r = await fetch(path,{method,headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
  if(!r.ok){ alert(await r.text()); return null; }
  return r.json();
}
async function loadTodos(){
  const todos = await api('/api/todos'); if(!todos) return;
  $('list').innerHTML = todos.map(t =>
    `<div class="todo ${t.done?'done':''}">
       <input type="checkbox" style="width:auto" ${t.done?'checked':''} onchange="toggle(${t.id})">
       <span>${t.text.replace(/</g,'&lt;')}</span>
       <button class="x" onclick="del(${t.id})">x</button></div>`).join('');
}
async function addTodo(){ const v=$('newtodo').value.trim(); if(!v) return;
  await api('/api/todos','POST',{text:v}); $('newtodo').value=''; loadTodos(); }
async function toggle(id){ await api('/api/todos/'+id+'/toggle','POST'); loadTodos(); }
async function del(id){ await api('/api/todos/'+id,'DELETE'); loadTodos(); }
async function startTimer(m){ if(!(m>0)) return; await api('/api/timer/start','POST',{minutes:m}); pollTimer(); }
async function stopTimer(){ await api('/api/timer/stop','POST'); pollTimer(); }
async function pollTimer(){
  const s = await api('/api/state'); if(!s) return;
  const t = s.timer, m = Math.floor(t.remaining/60), sec = t.remaining%60;
  $('tstatus').textContent = t.status==='finished' ? 'finished!' : t.status;
  $('tleft').textContent = t.status==='idle' ? '--:--' : String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');
}
loadTodos(); pollTimer(); setInterval(pollTimer, 1000);
</script></body></html>"""


@app.get("/", response_class=HTMLResponse)
def home():
    return PAGE