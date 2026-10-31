# Architecture diagrams

| Diagram | README image | Editable source |
|---|---|---|
| Inputs, native event loop, heaps and outputs | [Architecture](images/architecture.svg) | [draw.io](images/architecture.drawio) |
| Heap tree, backing array and position map | [Indexed heap](images/indexed-heap.svg) | [draw.io](images/indexed-heap.drawio) |

Open the `.drawio` files in diagrams.net to edit boxes, labels and connectors. Export SVG after editing to update the README images. The diagrams use white backgrounds, black text and simple connectors.

The architecture arrows show data flow and access to internal structures. The simulator advances virtual time; it does not execute host processes. The heap example shows IDs and ranks; actual ordering breaks rank ties by enqueue order, then ID. The ready heap includes the currently running job.
