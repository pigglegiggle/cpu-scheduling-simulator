# Architecture image prompts

Created with the built-in imagegen tool. Diagrams are explanatory views of the C++ implementation, not hardware timing claims.

## architecture.png

```text
Use case: infographic-diagram. Create ONE standalone landscape architecture diagram for an engineering README. Strict black ink on solid white, white box interiors, no colors, no blue, no gray panels, no gradients/shadows/decorations. Clean precise modern technical typography, large readable English labels, restrained line weights and clear arrowheads. No collage and no invented components. Approximate 2:1 aspect ratio. The program is a virtual single-CPU scheduling simulator, not a real hardware controller, GPU or OS. Title: "Scheduling architecture".
Show a clear main horizontal processing chain:
"Workload JSON" → "Python input layer" (subtitle: "Validate + expand periodic jobs") → "pybind11" → "C++ simulation core" → "Result".
Inside a clearly separated enlarged core inset immediately BELOW the C++ core box, show three small components:
"Sorted release + control events" → "Event-driven loop" → "Indexed ready heap".
Also show a separate small box "Indexed deadline heap" connected to the Event-driven loop by a consults arrow. Within the Event-driven loop label or just below it, write "EDF · Fixed priority · FIFO".
One small note underneath the ready heap: "Running job stays in the ready heap".
Inside Result show exactly "Timeline · job outcomes · metrics · event trace".
At bottom, two concise interface summaries on separate lines:
"CLI → Python input layer → shared C++ core"
"Streamlit UI → Python input layer → shared C++ core"
Footer: "Virtual time; zero dispatch cost; no I/O blocking or multicore."
Be careful: no HTTP server, no FastAPI, no database, no persistence, no hardware, no Python scheduling algorithm. Both interfaces use the same C++ core through pybind11. This image shows the architecture only, not detailed heap nodes.
```

## indexed-heap.png

```text
Use case: infographic-diagram. Create ONE standalone landscape architecture diagram for an engineering README. Strict black ink on solid white, white box interiors, no colors, no blue, no gray panels, no gradients/shadows/decorations. Clean precise modern technical typography, large readable English labels, restrained line weights and clear arrowheads. No collage and no invented components. Approximate 2:1 aspect ratio. The program is a virtual single-CPU scheduling simulator, not a real hardware controller, GPU or OS. Title: "Indexed min-heap".
Subtitle: "Array-backed heap + ID-to-position hash map".
Draw ONE binary min-heap tree with exactly six nodes and exactly these edges and labels:
root "ID 40 | rank 5" (small label "index 0").
root LEFT child "ID 20 | rank 12" ("index 1").
root RIGHT child "ID 60 | rank 8" ("index 2").
ID20 LEFT child "ID 10 | rank 32" ("index 3").
ID20 RIGHT child "ID 50 | rank 24" ("index 4").
ID60 LEFT child "ID 30 | rank 16" ("index 5").
No other edges.
To the right, separate table heading "Position map" with exactly these six rows:
"40 → 0"
"20 → 1"
"60 → 2"
"10 → 3"
"50 → 4"
"30 → 5".
Below the tree, a horizontal array with labels:
"[40:5] [20:12] [60:8] [10:32] [50:24] [30:16]"
and caption "Heap order is not fully sorted order".
Bottom note: "Peek O(1) · insert / update / erase O(log n)"
Final small note: "ID lookup uses expected O(1) hashing; swaps update the position map."
All ranks and ID/index mappings MUST match exactly. This is a DSA explanation only. Do not draw update operations, a second tree, command protocols or UI.
```
