"""Small movement-policy analog plus independent BFS over the native graph export.

No park assets or Python dependencies are required for the synthetic tests.
This models the early wide-neighbour/no-backtrack shortcut and exact routing.
It deliberately does not model the legacy heuristic, transport, or guest needs.
Native probes in TexasPathfindingInvestigation.cpp corroborate the local decisions.
"""
import argparse
import csv
import json
import unittest
from collections import defaultdict, deque
from dataclasses import dataclass, field

DELTA = ((-1, 0), (0, 1), (1, 0), (0, -1))
CENTRE = (55, 141, 14)
SOUTH = (55, 142, 14)
EAST = (56, 141, 14)
GOAL = (45, 114, 14)


@dataclass
class Node:
    wide: bool = False
    queue: int = -1
    links: dict = field(default_factory=dict)
    permitted: int = 15
    distance: int = -1
    route: int = -1


def load_graph(filename):
    graph = {}
    with open(filename, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            r = {key: int(value) for key, value in row.items()}
            loc = r["x"], r["y"], r["z"]
            if loc in graph or not r["exact"] or r["permitted"] < 0:
                raise ValueError("This investigation requires an exact, unambiguous graph")
            links = {
                d: (loc[0] + dx, loc[1] + dy, r[f"conn{d}"])
                for d, (dx, dy) in enumerate(DELTA)
                if r[f"conn{d}"] >= 0 and r["edges"] & (1 << d)
            }
            graph[loc] = Node(bool(r["wide"]), r["queue"], links, r["permitted"], r["distance"], r["route"])
    return graph


def reverse_bfs(graph, goal, target_ride=2):
    """Independent solver, using native exported connectivity and banner masks."""
    incoming = defaultdict(list)
    for loc, node in graph.items():
        if node.queue not in (-1, target_ride):
            continue
        for d, other in node.links.items():
            if node.permitted & (1 << d) and other in graph and graph[other].queue in (-1, target_ride):
                incoming[other].append(loc)
    distance = {goal: 0}
    pending = deque([goal])
    while pending:
        dest = pending.popleft()
        for source in incoming[dest]:
            if source not in distance:
                distance[source] = distance[dest] + 1
                pending.append(source)
    return distance


def exact_direction(graph, distances, loc):
    node = graph[loc]
    if distances.get(loc, 0) == 0:
        return None
    choices = [d for d, other in node.links.items()
               if node.permitted & (1 << d) and distances.get(other) == distances[loc] - 1]
    return min(choices) if choices else None


def movement_direction(graph, distances, loc, incoming, exact_first=False):
    node = graph[loc]
    route = exact_direction(graph, distances, loc)
    if exact_first and route is not None:
        return route
    edges = {d for d in node.links if node.permitted & (1 << d)}
    narrow = {d for d in edges if not graph[node.links[d]].wide}
    if narrow:
        edges = narrow
    reverse = (incoming + 2) % 4
    if edges - {reverse}:
        edges.discard(reverse)
    if len(edges) == 1:
        return next(iter(edges))
    # ChooseDirection re-reads permitted edges; it does not receive the early mask.
    return route


def walk(graph, goal, exact_first=False, limit=1000):
    distances = reverse_bfs(graph, goal)
    loc, incoming = CENTRE, 0  # Arrived from the east.
    trace = [loc]
    for _ in range(limit):
        if loc == goal:
            break
        direction = movement_direction(graph, distances, loc, incoming, exact_first)
        if direction is None:
            break
        loc, incoming = graph[loc].links[direction], direction
        trace.append(loc)
    return trace


def synthetic_graph():
    # A two-wide avenue and cross path, preserving the trap's relevant masks.
    tiles = {(x, y, 14) for x in (55, 56) for y in range(138, 143)}
    tiles |= {(x, 142, 14) for x in range(53, 59)}
    graph = {p: Node(wide=p[0] == 55 and p[1] < 142) for p in tiles}
    for p, node in graph.items():
        node.links = {d: (p[0]+dx, p[1]+dy, p[2]) for d, (dx, dy) in enumerate(DELTA)
                      if (p[0]+dx, p[1]+dy, p[2]) in graph}
    return graph


class MovementPolicyTests(unittest.TestCase):
    def test_exact_solver_finds_progress(self):
        graph = synthetic_graph()
        distances = reverse_bfs(graph, (55, 138, 14))
        self.assertEqual(exact_direction(graph, distances, CENTRE), 3)

    def test_current_movement_reproduces_three_tile_loop(self):
        trace = walk(synthetic_graph(), (55, 138, 14))
        self.assertEqual(len(trace), 1001)
        self.assertEqual(set(trace), {CENTRE, SOUTH, EAST})
        self.assertEqual(trace[:5], [CENTRE, SOUTH, CENTRE, EAST, CENTRE])

    def test_proposed_precedence_reaches_goal(self):
        goal = (55, 138, 14)
        trace = walk(synthetic_graph(), goal, exact_first=True)
        self.assertEqual(trace[-1], goal)
        self.assertEqual(len(trace)-1, 3)

    def test_banners_remain_hard_constraints(self):
        graph = synthetic_graph()
        graph[CENTRE].permitted &= ~(1 << 3)
        distances = reverse_bfs(graph, (55, 138, 14))
        self.assertNotEqual(movement_direction(graph, distances, CENTRE, 0, True), 3)

    def test_foreign_queue_remains_forbidden(self):
        graph = synthetic_graph()
        graph[(55, 140, 14)].queue = 16
        distances = reverse_bfs(graph, (55, 138, 14))
        self.assertNotIn((55, 140, 14), distances)
        self.assertNotEqual(movement_direction(graph, distances, CENTRE, 0, True), 3)


def investigate(paths, guests=None):
    graph = load_graph(paths)
    distances = reverse_bfs(graph, GOAL)
    mismatches = [p for p, n in graph.items() if n.distance != distances.get(p, -1)]
    violations = []
    for p, node in graph.items():
        if node.route >= 0 and (node.route not in node.links or
                               distances.get(node.links[node.route]) != distances.get(p, 0)-1):
            violations.append(p)
    old = walk(graph, GOAL)
    proposed = walk(graph, GOAL, exact_first=True)
    assert not mismatches, mismatches[:10]
    assert not violations, violations[:10]
    assert set(old) == {CENTRE, SOUTH, EAST}, set(old)
    assert proposed[-1] == GOAL
    assert len(proposed)-1 == distances[CENTRE]
    result = dict(nodes=len(graph), reachable=len(distances), distance_mismatches=len(mismatches),
                  non_descending_native_steps=len(violations), current_steps=len(old)-1,
                  current_unique_tiles=sorted(set(old)), proposed_steps=len(proposed)-1,
                  proposed_reached_goal=proposed[-1] == GOAL, proposed_trace=proposed)
    if guests:
        histories = defaultdict(list)
        with open(guests, newline="", encoding="utf-8") as source:
            for row in csv.DictReader(source):
                histories[row["id"]].append(row)
        trapped = []
        for ident, rows in histories.items():
            positions = {(int(r["x"]), int(r["y"]), int(r["z"])) for r in rows}
            if len(rows) >= 100 and positions <= {CENTRE, SOUTH, EAST}:
                trapped.append(dict(id=ident, name=rows[0]["name"], samples=len(rows),
                                    first_tick=int(rows[0]["tick"]), last_tick=int(rows[-1]["tick"])))
        result["guests_only_observed_in_trap_at_least_100_samples"] = trapped
        runs = []
        for ident, rows in histories.items():
            longest, consecutive, previous_tick = 0, 0, -16
            for row in rows:
                tick = int(row["tick"])
                pos = tuple(int(row[k]) for k in ("x", "y", "z"))
                if tick != previous_tick + 16:
                    consecutive = 0
                consecutive = consecutive + 1 if pos in {CENTRE, SOUTH, EAST} else 0
                previous_tick = tick
                longest = max(longest, consecutive)
            if longest >= 100:
                runs.append(dict(id=ident, name=rows[0]["name"], longest_consecutive_samples=longest))
        result["guests_with_at_least_100_consecutive_trap_samples"] = runs
        result["cecil"] = [r for rows in histories.values() for r in rows if r["name"] == "Cecil N."][:6]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--paths")
    parser.add_argument("--guests")
    args = parser.parse_args()
    if args.paths:
        print(json.dumps(investigate(args.paths, args.guests), indent=2))
    else:
        unittest.main(argv=[__file__])
