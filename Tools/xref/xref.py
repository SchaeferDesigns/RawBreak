"""pooltool cross-check of BilliardsCore (prior-art 9.12: XREF-01, XREF-02). Owner: WP-10.

Runs the same B1-like shots in pooltool 0.6.0 (the oracle, in-process) and in BilliardsCore (Tools/rbsim, one process per shot,
JSON output) and compares event sequences, event times and final positions.

  XREF-01  pooltool: frictionless-elastic ball-ball, "unrealistic" (lossless mirror) cushions, canonical pockets and transitions.
           rbsim:    ballball.e = 1 without friction, cushion model Mirror with e = 1, pooltool contact distance (R),
                     CaptureCircle pockets. Pass: same event-type sequence in >= 99 % of shots, final positions within 1 mm,
                     event times within 1e-6 s.
  XREF-02  pooltool defaults: Alciatore ball-ball friction (a, b, c = 0.009951, 0.108, 1.088; e_b 0.95), Stronge compliant
           cushions (omega_ratio 1.8, e_c 0.85, f_c 0.2), canonical pockets. rbsim: FrictionalImpulse with Alciatore friction,
           StrongeCompliant (e 0.85, mu_w 0.2), contact distance R, CaptureCircle pockets. Pass: final positions within 5 mm in
           >= 95 % of shots.

Both run on TABLE_7FT_78 (pooltool's default table, SEVEN_FOOT_SHOWOOD) with pooltool's ball parameters (m 0.170097, R 0.028575,
mu_s 0.2, mu_r 0.01, u_sp = (10 * 2 / 5 / 9) R -> alpha_sp = 5 u_sp g / (2 R) = 10.9 rad/s^2, g 9.81).

Shots (INTERPRETATION of B1 for an oracle without a cue model or airborne balls): 2-9 object balls placed at random (seeded), the
cue ball starts SLIDING with 0.5-4 m/s toward a random object ball with a random cut (+-0.35 rad) and random spin (top/back
-1.25 ... 1.25 x natural roll, side +-1 v / R), i.e. the post-strike state of a level stroke; both simulators get the identical
initial state, so the comparison covers motion, detection and the collision models, not the cue models.

Frames: pooltool's origin is the table corner with x across the width and y along the length; the core's origin is the centre
with X along the length. The map is the rotation (X, Y) = (y - l / 2, w / 2 - x), applied to positions, velocities and spins.

Two comparisons (WP-10):
  * whole shots, as XREF-01 / XREF-02 specify. Shots are chaotic: every contact amplifies a position difference ~10^2-fold, so a
    difference of 1e-8 m at one contact is a millimetre three contacts later;
  * one step: for every pooltool event k the core restarts from pooltool's state of all balls right after event k-1 and must
    find pooltool's event k first (same type and balls, time within 1e-6 s); for contacts, the post-contact velocity and spin
    of the balls involved are compared with pooltool's (this checks motion, detection and resolution without chaos).

Expected model differences (compared only where the models agree):
  * pooltool's contact spacer: before resolving a contact it moves the balls apart to 2 R + MIN_DIST (cushions R + MIN_DIST),
    MIN_DIST = 1 um (``make_kiss``). The core resolves at the exact touching state. XREF-01 sets MIN_DIST to 1e-8 m (0 makes
    pooltool find the resolved contact again at once; 1e-9 m makes ``make_kiss`` fail to find a real root); XREF-02 keeps 1 um.
  * facings: the core's are undercut (beta_v 12 deg) with the on-shelf contact offset s_f and a tilted normal; pooltool's are
    vertical lines at R, and TABLE_7FT_78's side facings use sin instead of tan of the pocket angle (equipment 12.3). Contacts
    with a facing (pooltool: a linear cushion segment that is not parallel to a rail; core: BallJaw elements 1, 2) are a model
    difference: shots with one are left out of the whole-shot comparison, steps with one out of the one-step comparison.
  * clusters: a ball-ball contact with a third ball or a cushion within delta_cl goes to the core's compliant island (1 us
    steps, multi-contact); pooltool resolves pairs one after the other (and moves 10 % of the radial momentum between touching,
    co-moving balls). Such contacts differ in outcome and by up to one island step in time.
  * vertical impulses (XREF-02): pooltool drops v_z after every ball-ball and cushion resolution ("FIXME-3D"); the core applies
    the slate reaction to the downward part (impulsive cloth friction, collisions 2.4 steps 6-7 and 4.7), which changes the
    horizontal velocity and spin of a ball whose friction impulse points down (a spinning cue ball at a sticking ball-ball
    contact, every elevated nose contact).
  * pooltool's ball-ball friction impulse is mu e v_n per unit mass (FrictionalInelastic); the core's is mu (1 + e) v_n / 2
    (TP A.14). Both stick when the slip would reverse, so the difference shows only in sliding contacts.

Usage:  Tools/xref/.venv/Scripts/python.exe Tools/xref/xref.py --case 01 --shots 1000 --rbsim build/Tools/rbsim/Release/rbsim.exe
        [--no-steps] [--details N] [--show SHOT] [--spacer M] [--report FILE]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import subprocess
import sys
import time

import numpy as np
import pooltool as pt
import pooltool.constants as const
from pooltool.events.datatypes import EventType
from pooltool.objects import BallState
from pooltool.physics.engine import PhysicsEngine
from pooltool.physics.resolve.ball_ball.friction import AlciatoreBallBallFriction
from pooltool.physics.resolve.ball_ball.frictional_inelastic import FrictionalInelastic
from pooltool.physics.resolve.ball_ball.frictionless_elastic import FrictionlessElastic
from pooltool.physics.resolve.ball_cushion.stronge_compliant import StrongeCompliantCircular, StrongeCompliantLinear
from pooltool.physics.resolve.ball_cushion.unrealistic import UnrealisticCircular, UnrealisticLinear
from pooltool.physics.resolve.ball_pocket import CanonicalBallPocket
from pooltool.physics.resolve.resolver import VERSION, Resolver
from pooltool.physics.resolve.stick_ball.instantaneous_point import InstantaneousPoint
from pooltool.physics.resolve.transition import CanonicalTransition

R = 0.028575
MASS = 0.170097
G = 9.81
U_SP_PROPORTIONALITY = 10 * 2 / 5 / 9
ALPHA_SP = 5 * U_SP_PROPORTIONALITY * G / 2  # 5 u_sp g / (2 R) with u_sp = proportionality * R

TIME_TOL = 1e-6  # s, XREF-01 event times
SIMULTANEOUS = 1e-9  # s, events closer than this are one instant (their order is not compared)
PT_MAX_EVENTS = 20000  # the core's MaxEvents


# --------------------------------------------------------------------------------------------------------------------------
# Configurations
# --------------------------------------------------------------------------------------------------------------------------


def set_pooltool_spacer(spacer: float) -> None:
    """Sets pooltool's contact spacer for the whole process (the oracle's configuration; no pooltool code is copied).

    Before resolving a contact, pooltool 0.6.0 moves the balls apart to 2 R + MIN_DIST (ball-ball) or R + MIN_DIST (cushions)
    along their velocities (``make_kiss``, MIN_DIST = 1 um, read at run time). BilliardsCore resolves contacts at the exact
    touching state. The 1 um jump per contact is amplified by every later contact (x ~400 over two collisions of shot 4 of the
    default seed), so with it the XREF-01 tolerances (event times 1e-6 s, positions 1 mm) compare the spacer, not the physics.
    Without any spacer pooltool finds the resolved contact again at once (an event explosion that ran out of memory); 1 nm keeps
    its detection working while the jumps stay far below the XREF-01 tolerances."""
    const.MIN_DIST = spacer


def pooltool_engine(case: str) -> PhysicsEngine:
    """The resolver built explicitly (Resolver.default() would read / write ~/.config/pooltool)."""
    if case == "01":
        resolver = Resolver(
            ball_ball=FrictionlessElastic(),
            ball_linear_cushion=UnrealisticLinear(),
            ball_circular_cushion=UnrealisticCircular(),
            ball_pocket=CanonicalBallPocket(),
            stick_ball=InstantaneousPoint(english_throttle=1.0, squirt_throttle=1.0),
            transition=CanonicalTransition(),
            version=VERSION,
        )
    else:
        resolver = Resolver(
            ball_ball=FrictionalInelastic(friction=AlciatoreBallBallFriction(a=0.009951, b=0.108, c=1.088)),
            ball_linear_cushion=StrongeCompliantLinear(omega_ratio=1.8),
            ball_circular_cushion=StrongeCompliantCircular(omega_ratio=1.8),
            ball_pocket=CanonicalBallPocket(),
            stick_ball=InstantaneousPoint(english_throttle=1.0, squirt_throttle=1.0),
            transition=CanonicalTransition(),
            version=VERSION,
        )
    return PhysicsEngine(resolver=resolver)


def rbsim_params(case: str) -> list[str]:
    common = [
        "gravity=9.81",
        "cloth.mu_s=0.2",
        "cloth.mu_r=0.01",
        f"cloth.alpha_sp={ALPHA_SP!r}",
        "pockets.model=1",  # PocketModel::CaptureCircle
        "cushion.pooltool_compat=1",  # contact distance R (equipment 12.2)
        "cushion.e_slope=0",
    ]
    if case == "01":
        specific = [
            "ballball.e=1",
            "ballball.friction=2",  # BallBallFrictionModel::None
            "cushion.model=2",  # CushionModel::Mirror
            "cushion.e_max=1",
            "cushion.e_min=1",
        ]
    else:
        specific = [
            "ballball.e=0.95",
            "ballball.friction=0",  # Alciatore
            "ballball.mu_a=0.009951",
            "ballball.mu_b=0.108",
            "ballball.mu_c=1.088",
            "cushion.model=3",  # CushionModel::StrongeCompliant
            "cushion.stronge_omega_ratio=1.8",
            "cushion.e_max=0.85",
            "cushion.e_min=0.85",
            "cushion.mu_w=0.2",
        ]
    args: list[str] = []
    for p in common + specific:
        args += ["--param", p]
    return args


# --------------------------------------------------------------------------------------------------------------------------
# Shots
# --------------------------------------------------------------------------------------------------------------------------


def make_shot(rng: random.Random, length: float, width: float, pocket_points: list[tuple[float, float]]) -> list[dict]:
    """Balls in the CORE frame: [{'id', 'r': (X, Y), 'v': (vx, vy), 'w': (wx, wy, wz)}]; ball 0 moves."""
    half_l, half_w = length / 2, width / 2
    margin = R + 0.03
    n_balls = 3 + rng.randrange(8)
    balls: list[dict] = []
    for _ in range(2000):
        if len(balls) >= n_balls:
            break
        p = (rng.uniform(-half_l + margin, half_l - margin), rng.uniform(-half_w + margin, half_w - margin))
        if any(math.dist(p, b["r"]) <= 2 * R + 0.005 for b in balls):
            continue
        if any(math.dist(p, q) <= 0.12 for q in pocket_points):
            continue
        balls.append({"id": len(balls), "r": p, "v": (0.0, 0.0), "w": (0.0, 0.0, 0.0)})
    target = balls[1 + rng.randrange(len(balls) - 1)]
    aim = math.atan2(target["r"][1] - balls[0]["r"][1], target["r"][0] - balls[0]["r"][0]) + rng.uniform(-0.35, 0.35)
    speed = rng.uniform(0.5, 4.0)
    d = (math.cos(aim), math.sin(aim))
    v = (speed * d[0], speed * d[1])
    top = rng.uniform(-1.25, 1.25)  # x natural roll
    side = rng.uniform(-1.0, 1.0)
    # natural roll about the horizontal axis perpendicular to d: w = (z x v) / R
    w = (-v[1] / R * top, v[0] / R * top, side * speed / R)
    balls[0]["v"] = v
    balls[0]["w"] = w
    return balls


def to_pooltool(p: tuple[float, float], length: float, width: float) -> tuple[float, float]:
    return (width / 2 - p[1], p[0] + length / 2)


def vec_to_pooltool(v: tuple[float, float]) -> tuple[float, float]:
    return (-v[1], v[0])


def from_pooltool(p, length: float, width: float) -> tuple[float, float]:
    return (p[1] - length / 2, width / 2 - p[0])


# --------------------------------------------------------------------------------------------------------------------------
# Runs
# --------------------------------------------------------------------------------------------------------------------------

PT_TYPES = {
    EventType.BALL_BALL: "BB",
    EventType.BALL_LINEAR_CUSHION: "C",
    EventType.BALL_CIRCULAR_CUSHION: "C",
    EventType.BALL_POCKET: "P",
    EventType.SLIDING_ROLLING: "Sliding>Rolling",
    EventType.ROLLING_SPINNING: "Rolling>Spinning",
    EventType.ROLLING_STATIONARY: "Rolling>Stationary",
    EventType.SPINNING_STATIONARY: "Spinning>Stationary",
}

RB_TYPES = {"BallBall": "BB", "BallCushion": "C", "BallJaw": "C", "BallPocketed": "P"}


def pt_kind(e) -> str | None:
    """Event kind of a pooltool event; a linear cushion segment that is not parallel to a rail is a pocket FACING ("F")."""
    kind = PT_TYPES.get(e.event_type)
    if e.event_type == EventType.BALL_LINEAR_CUSHION:
        for a in e.agents:
            if a.agent_type.value == "linear_cushion_segment" and a.initial is not None:
                d = a.initial.p2 - a.initial.p1
                if abs(d[0]) > 1e-9 and abs(d[1]) > 1e-9:
                    kind = "F"
    return kind


def rb_kind(e: dict) -> str | None:
    """Event kind of an rbsim event: BallJaw elements 1 and 2 (facing face / edge) are facings ("F"), element 0 a jaw arc."""
    if e["type"] == "MotionTransition":
        return f"{e['from']}>{e['to']}"
    if e["type"] == "BallJaw" and (e.get("sub", 0) >> 4) != 0:
        return "F"
    return RB_TYPES.get(e["type"])


def run_pooltool(balls: list[dict], engine: PhysicsEngine, table) -> tuple[list[tuple], dict]:
    length, width = float(table.l), float(table.w)  # numpy scalars would print as np.float64(...) on the rbsim command line
    pt_balls = {}
    for b in balls:
        ball = pt.Ball.create(str(b["id"]), xy=to_pooltool(b["r"], length, width), m=MASS, R=R, u_s=0.2, u_r=0.01,
                              u_sp_proportionality=U_SP_PROPORTIONALITY, e_b=0.95, e_c=0.85, f_c=0.2, g=G)
        vx, vy = vec_to_pooltool(b["v"])
        wx, wy = vec_to_pooltool((b["w"][0], b["w"][1]))
        rvw = np.array([[*to_pooltool(b["r"], length, width), R], [vx, vy, 0.0], [wx, wy, b["w"][2]]], dtype=np.float64)
        moving = b["v"] != (0.0, 0.0)
        ball.state = BallState(rvw=rvw, s=const.sliding if moving else const.stationary, t=0.0)
        pt_balls[str(b["id"])] = ball
    system = pt.System(cue=pt.Cue(cue_ball_id="0"), table=table, balls=pt_balls)
    pt.simulate(system, engine=engine, inplace=True, max_events=PT_MAX_EVENTS)  # a runaway cannot hang the harness
    events = []
    for e in system.events:
        kind = pt_kind(e)
        if kind is None:
            continue
        ids = [int(a.id) for a in e.agents if a.agent_type.value == "ball"]
        events.append((e.time, kind, tuple(sorted(ids))))
    finals = {}
    for key, ball in system.balls.items():
        if ball.state.s == const.pocketed:
            finals[int(key)] = ("pocket", from_pooltool(ball.state.rvw[0][:2], length, width))
        else:
            finals[int(key)] = ("table", from_pooltool(ball.state.rvw[0][:2], length, width))
    return events, finals, system


def vec_from_pooltool(v) -> tuple[float, float]:
    return (float(v[1]), -float(v[0]))


def run_rbsim(balls: list[dict], exe: str, case: str) -> tuple[list[tuple], dict, str]:
    args = [exe, "--table", "7ft-78", "--no-trajectories", "--no-states", "--dt", "0", "--compact"] + rbsim_params(case)
    for b in balls:
        v = b["v"]
        w = b["w"]
        args += ["--state", f"{b['id']}:{b['r'][0]!r},{b['r'][1]!r},{R!r},{v[0]!r},{v[1]!r},0,{w[0]!r},{w[1]!r},{w[2]!r}"]
    out = subprocess.run(args, capture_output=True, text=True)
    if out.returncode not in (0, 3):
        raise RuntimeError(f"rbsim failed ({out.returncode}): {out.stderr.strip()}")
    data = json.loads(out.stdout)
    events = []
    for e in data["events"]:
        kind = rb_kind(e)
        if kind is None:
            continue
        ids = [e["a"]] + ([e["b"]] if kind == "BB" else [])
        events.append((e["t"], kind, tuple(sorted(ids))))
    finals = {}
    for b in data["balls"]:
        r = b["finalState"]["r"]
        finals[b["id"]] = ("pocket" if b["final"] == "Pocketed" else "table", (r[0], r[1]), b["pocket"])
    return events, finals, data["status"]


# --------------------------------------------------------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------------------------------------------------------


def normalise(events: list[tuple]) -> list[tuple]:
    """Drops zero-duration motion phases (a transition at the instant the phase began: pooltool marks every ball leaving a
    collision 'sliding' and emits sliding->rolling at once when it already rolls, the core classifies the state directly), then
    orders each instant (events closer than SIMULTANEOUS) by key, so only the order of distinct instants counts."""
    events = sorted(events, key=lambda e: e[0])
    last_change: dict[int, float] = {}
    kept = []
    for t, kind, ids in events:
        if ">" in kind:
            b = ids[0]
            if b in last_change and t - last_change[b] < SIMULTANEOUS:
                last_change[b] = t
                continue
        for b in ids:
            last_change[b] = t
        kept.append((t, kind, ids))
    out: list[tuple] = []
    group: list[tuple] = []
    for e in kept:
        if group and e[0] - group[0][0] >= SIMULTANEOUS:
            out += sorted(group, key=lambda x: (x[1], x[2]))
            group = []
        group.append(e)
    out += sorted(group, key=lambda x: (x[1], x[2]))
    return out


def step_check(system, exe: str, case: str, length: float, width: float) -> list[dict]:
    """One-step comparison (no chaos): for every pooltool event k, BilliardsCore restarts from pooltool's state of ALL balls right
    after event k-1 (the ball histories) and its first event must be pooltool's event k: same type and balls, time within
    TIME_TOL. Balls pooltool has pocketed are left out. Skipped: pooltool's zero-duration motion phases (a ball it marks
    'sliding' after a contact although it rolls; the core classifies such a state directly) and the terminal dummy event."""
    steps = []
    events = system.events
    ids = sorted(system.balls.keys(), key=int)
    for k in range(1, len(events)):
        e = events[k]
        kind = pt_kind(e)
        if kind is None:
            continue
        t0 = float(events[k - 1].time)
        dt = float(e.time) - t0
        agents = tuple(sorted(int(a.id) for a in e.agents if a.agent_type.value == "ball"))
        if ">" in kind and dt < SIMULTANEOUS:
            continue  # a zero-duration phase of pooltool's (see above)
        # pooltool events at the same instant as event k: any of them may come first in the core
        expected = {(kind, agents): k}
        for j in range(k + 1, len(events)):
            if float(events[j].time) - float(e.time) >= SIMULTANEOUS:
                break
            other = pt_kind(events[j])
            if other is not None:
                expected.setdefault((other, tuple(sorted(int(a.id) for a in events[j].agents if a.agent_type.value == "ball"))), j)
        args = [exe, "--table", "7ft-78", "--no-trajectories", "--dt", "0", "--compact"] + rbsim_params(case)
        # rbsim always puts a cue ball (id 0) on the table: once pooltool has pocketed ball 0, the lowest remaining ball plays
        # core id 0 (the physics does not depend on ids) and ids are mapped back.
        live = [key for key in ids if system.balls[key].history[k - 1].s != const.pocketed]
        core_of = {int(key): int(key) for key in live}
        if live and "0" not in live:
            core_of[int(live[0])] = 0
        pt_of = {c: p for p, c in core_of.items()}
        for key in live:
            s = system.balls[key].history[k - 1]
            p = from_pooltool(s.rvw[0][:2], length, width)
            v = vec_from_pooltool(s.rvw[1][:2])
            w = vec_from_pooltool(s.rvw[2][:2])
            args += ["--state", f"{core_of[int(key)]}:{float(p[0])!r},{float(p[1])!r},{R!r},{v[0]!r},{v[1]!r},0,{w[0]!r},{w[1]!r},{float(s.rvw[2][2])!r}"]
        out = subprocess.run(args, capture_output=True, text=True)
        got = None
        post_error = math.nan
        resolution = None
        if out.returncode not in (0, 3):
            got = (math.nan, f"rbsim exit {out.returncode}: {out.stderr.strip()[:120]}", ())
        else:
            for g in json.loads(out.stdout)["events"]:
                gk = rb_kind(g)
                if gk is None or (">" in gk and g["t"] < SIMULTANEOUS):
                    continue
                got = (g["t"], gk, tuple(sorted(pt_of.get(b, b) for b in [g["a"]] + ([g["b"]] if gk == "BB" else []))))
                match = expected.get((got[1], got[2]))
                if match is not None and gk in ("BB", "C", "F") and "post" in g:
                    # The resolution: the post-contact planar velocity and spin (R w, all three axes) of the balls involved
                    # against pooltool's state right after its event, in m/s.
                    post_error = 0.0
                    detail = []
                    for slot, core_ball in enumerate([g["a"]] + ([g["b"]] if gk == "BB" else [])):
                        ball = pt_of.get(core_ball, core_ball)
                        ours = g["post"][slot]
                        theirs = system.balls[str(ball)].history[match]
                        tv = vec_from_pooltool(theirs.rvw[1][:2])
                        tw = vec_from_pooltool(theirs.rvw[2][:2]) + (float(theirs.rvw[2][2]),)
                        dv = math.hypot(ours["v"][0] - tv[0], ours["v"][1] - tv[1])
                        dw = R * math.dist(ours["w"], tw)
                        post_error = max(post_error, dv, dw)
                        detail.append({"ball": ball, "pre_v": g["pre"][slot]["v"], "pre_w": g["pre"][slot]["w"], "core_v": ours["v"],
                                       "core_w": ours["w"], "pooltool_v": tv, "pooltool_w": tw})
                    resolution = {"event": {k2: g[k2] for k2 in ("type", "feature", "sub", "normal", "vn")}, "balls": detail}
                break
        same = got is not None and (got[1], got[2]) in expected
        facing = kind == "F" or (got is not None and got[1] == "F")
        steps.append({"k": k, "expected": (dt, kind, agents), "got": got, "same": same, "facing": facing,
                      "dt_error": abs(got[0] - dt) if same else math.inf, "post_error": post_error, "resolution": resolution})
    return steps


def compare(pt_events, pt_finals, rb_events, rb_finals, pocket_map) -> dict:
    a = normalise(pt_events)
    b = normalise(rb_events)
    same_seq = [(k, i) for _, k, i in a] == [(k, i) for _, k, i in b]
    max_dt = max((abs(x[0] - y[0]) for x, y in zip(a, b)), default=0.0) if same_seq else math.inf
    first_diff = None
    if not same_seq:
        for n, (x, y) in enumerate(zip(a + [None] * len(b), b + [None] * len(a))):
            if x is None or y is None or (x[1], x[2]) != (y[1], y[2]):
                first_diff = (n, x, y)
                break
    max_pos = 0.0
    pocket_mismatch = 0
    for ball, (where, p) in pt_finals.items():
        rb_where, q, rb_pocket = rb_finals[ball]
        if where != rb_where:
            pocket_mismatch += 1
            max_pos = math.inf
        elif where == "pocket":
            if pocket_map(p) != rb_pocket:
                pocket_mismatch += 1
                max_pos = math.inf
        else:
            max_pos = max(max_pos, math.dist(p, q))
    facing = any(e[1] == "F" for e in a + b)
    return {"same_sequence": same_seq, "max_dt": max_dt, "max_pos": max_pos, "pocket_mismatch": pocket_mismatch,
            "events": len(a), "first_diff": first_diff, "facing": facing}



def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--case", choices=["01", "02"], required=True)
    parser.add_argument("--shots", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=20260927)
    parser.add_argument("--rbsim", required=True, help="path of a Release rbsim executable")
    parser.add_argument("--report", help="write the per-shot results as JSON")
    parser.add_argument("--outliers", type=int, default=6, help="print this many outliers")
    parser.add_argument("--show", type=int, default=-1, help="print both normalised event lists of this shot and stop")
    parser.add_argument("--spacer", type=float, default=-1.0,
                        help="pooltool's contact spacer MIN_DIST [m]; default: 1e-8 for XREF-01 (exact event model), pooltool's 1e-6 for XREF-02")
    parser.add_argument("--no-steps", action="store_true", help="skip the one-step comparison (one rbsim run per pooltool event)")
    parser.add_argument("--details", type=int, default=0, help="print the states of this many worst-resolved contacts per kind")
    args = parser.parse_args()
    set_pooltool_spacer(args.spacer if args.spacer >= 0.0 else (1e-8 if args.case == "01" else 1e-6))
    exe = os.path.abspath(args.rbsim)

    table = pt.Table.default()
    length, width = float(table.l), float(table.w)  # numpy scalars would print as np.float64(...) on the rbsim command line
    # Pocket ids of the core (PocketId order: P0 (-L/2, -W/2), P1 (0, -W/2), P2 (L/2, -W/2), P3 (L/2, W/2), P4 (0, W/2),
    # P5 (-L/2, W/2)), matched to pooltool's capture circles by position.
    core_pockets = [(-length / 2, -width / 2), (0.0, -width / 2), (length / 2, -width / 2), (length / 2, width / 2), (0.0, width / 2),
                    (-length / 2, width / 2)]
    pocket_points = [from_pooltool((p.a, p.b), length, width) for p in table.pockets.values()]

    def pocket_map(p):
        return min(range(6), key=lambda k: math.dist(p, core_pockets[k]))

    engine = pooltool_engine(args.case)
    rng = random.Random(args.seed)
    results = []
    steps: list[dict] = []
    oracle_failures: list[tuple[int, str]] = []
    started = time.perf_counter()
    for shot in range(args.shots):
        balls = make_shot(rng, length, width, pocket_points)
        if 0 <= args.show != shot:
            continue
        try:
            pt_events, pt_finals, system = run_pooltool(balls, engine, table)
        except Exception as error:  # the oracle's own failures (e.g. make_kiss finds no real root) are counted, not compared
            oracle_failures.append((shot, repr(error)))
            continue
        rb_events, rb_finals, status = run_rbsim(balls, exe, args.case)
        r = compare(pt_events, pt_finals, rb_events, rb_finals, pocket_map)
        if args.show == shot:
            print(f"shot {shot}: balls {json.dumps(balls)}")
            a, b = normalise(pt_events), normalise(rb_events)
            for n in range(max(len(a), len(b))):
                x = a[n] if n < len(a) else None
                y = b[n] if n < len(b) else None
                mark = "" if x is not None and y is not None and (x[1], x[2]) == (y[1], y[2]) else "  <-- differs"
                dt = f"{y[0] - x[0]:+.3e}" if x is not None and y is not None else ""
                print(f"  {n:3d} pooltool {x!s:58} rbsim {y!s:58} dt {dt}{mark}")
            for ball in sorted(pt_finals):
                print(f"  ball {ball}: pooltool {pt_finals[ball]}  rbsim {rb_finals[ball]}")
            for s in step_check(system, exe, args.case, length, width):
                print(f"  step {s['k']:3d}: pooltool {s['expected']}  core {s['got']}  {'ok' if s['same'] else 'DIFFERS'} |dt| {s['dt_error']:.3g}")
            return 0
        if not args.no_steps:
            for s in step_check(system, exe, args.case, length, width):
                s["shot"] = shot
                steps.append(s)
        r["shot"] = shot
        r["status"] = status
        r["balls"] = balls
        results.append(r)
    elapsed = time.perf_counter() - started

    everything = results
    not_ok = sum(1 for r in everything if r["status"] != "Ok")
    # Facings are a model difference (module docstring): shots with a facing contact in either simulator are not compared.
    results = [r for r in everything if not r["facing"]]
    n = len(results)
    same = [r for r in results if r["same_sequence"]]
    within_1mm = sum(1 for r in results if r["max_pos"] <= 1e-3)
    within_5mm = sum(1 for r in results if r["max_pos"] <= 5e-3)
    times_ok = sum(1 for r in same if r["max_dt"] <= TIME_TOL)
    print(f"XREF-{args.case}: {len(everything)} shots in {elapsed:.0f} s (seed {args.seed}, pooltool spacer {const.MIN_DIST:g} m); rbsim status not Ok: "
          f"{not_ok}; pooltool failed on {len(oracle_failures)} more (not compared){': ' + str(oracle_failures[:3]) if oracle_failures else ''}")
    print(f"  whole shots without a facing contact ({n}; chaotic: a 1e-8 m difference grows by ~10^2 per contact):")
    print(f"    same event-type sequence: {len(same)} / {n} = {100 * len(same) / n:.1f} %")
    print(f"    all event times within {TIME_TOL:g} s (shots with the same sequence): {times_ok} / {len(same)}")
    print(f"    final positions within 1 mm: {within_1mm} / {n} = {100 * within_1mm / n:.1f} %; within 5 mm: {within_5mm} / {n} = "
          f"{100 * within_5mm / n:.1f} %")
    worst = sorted(results, key=lambda r: (r["same_sequence"], -r["max_pos"]))[: args.outliers]
    for r in worst:
        print(f"    shot {r['shot']:4d}: same sequence {r['same_sequence']}, max |dp| {r['max_pos']:.3g} m, max |dt| {r['max_dt']:.3g} s, "
              f"events {r['events']}, pocket mismatches {r['pocket_mismatch']}, first difference {r['first_diff']}")
    facing_steps = [s for s in steps if s["facing"]]
    steps = [s for s in steps if not s["facing"]]
    steps_same = sum(1 for s in steps if s["same"])
    steps_time = sum(1 for s in steps if s["same"] and s["dt_error"] <= TIME_TOL)
    if steps:
        errors = sorted(s["dt_error"] for s in steps if s["same"])
        median = errors[len(errors) // 2] if errors else math.nan
        p99 = errors[(len(errors) * 99) // 100] if errors else math.nan
        print(f"  one step from pooltool's state after every event: {len(steps)} steps (+{len(facing_steps)} with a facing contact, not "
              f"compared); same next event {steps_same} = {100 * steps_same / len(steps):.2f} %; its time within {TIME_TOL:g} s {steps_time} = "
              f"{100 * steps_time / len(steps):.2f} % (|dt| median {median:.2g} s, p99 {p99:.2g} s)")
        for s in [s for s in steps if not s["same"] or s["dt_error"] > TIME_TOL][: args.outliers]:
            print(f"    shot {s['shot']:4d} step {s['k']:3d}: pooltool {s['expected']}  core {s['got']}  |dt| {s['dt_error']:.3g}")
        for label, kinds in (("ball-ball", ("BB",)), ("cushion / jaw", ("C",))):
            post = sorted(s["post_error"] for s in steps if s["same"] and s["expected"][1] in kinds and not math.isnan(s["post_error"]))
            if post:
                print(f"    {label} resolution ({len(post)} contacts): post-contact |dv|, R |dw| vs pooltool median {post[len(post) // 2]:.2g}, "
                      f"p99 {post[(len(post) * 99) // 100]:.2g}, max {post[-1]:.2g} m/s; within 1e-9 m/s: {sum(1 for x in post if x <= 1e-9)}")
                if args.details:
                    worst = sorted((s for s in steps if s["same"] and s["expected"][1] in kinds and not math.isnan(s["post_error"])),
                                   key=lambda s: -s["post_error"])[: args.details]
                    for s in worst:
                        print(f"      shot {s['shot']} step {s['k']}: {s['post_error']:.3g} m/s {json.dumps(s['resolution'])}")
    if args.case == "01":
        passed = len(same) >= 0.99 * n and within_1mm == n and times_ok == len(same)
        print(f"  XREF-01 as specified (>= 99 % same sequence, all final positions within 1 mm, times within 1e-6 s): {'PASS' if passed else 'FAIL'}")
        if steps:
            local = steps_same >= 0.99 * len(steps) and steps_time >= 0.99 * len(steps)
            print(f"  XREF-01 one-step (>= 99 % of steps: same next event, time within 1e-6 s): {'PASS' if local else 'FAIL'}")
    else:
        passed = within_5mm >= 0.95 * n
        print(f"  XREF-02 as specified (>= 95 % of shots with final positions within 5 mm): {'PASS' if passed else 'FAIL'}")
        if steps:
            print(f"  XREF-02 one-step (information): same next event in {100 * steps_same / len(steps):.2f} % of steps")
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump({"shots": results, "steps": steps, "oracle_failures": oracle_failures}, f, indent=1, default=str)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
