#!/usr/bin/env python3
"""Generates the shipped humanoid walk and run clips (engine/assets/animations).

Joint angle curves follow clinical gait data (hip/knee/ankle sagittal angles
over one stride), with pelvis rotation, list and sway, thorax counter-rotation,
a stabilised head and arms swinging against the legs. Pelvis height is solved
with forward kinematics so the lowest point of the stance shoe stays on the
ground, and the clip's native ground speed is measured from the stance foot so
AvatarController can match playback rate to movement speed.

Usage: generate_locomotion_clips.py [output_dir]
"""

import math
import os
import sys

# Bind-pose local translations, mirroring buildHumanoidSkeleton() in
# engine/src/core/RiggedAvatar.cpp. Every bind rotation is identity.
JOINTS = {
    "pelvis": ("root", (0.0, 1.0, 0.0)),
    "spine_lower": ("pelvis", (0.0, 0.16, 0.0)),
    "spine_upper": ("spine_lower", (0.0, 0.3, 0.0)),
    "neck": ("spine_upper", (0.0, 0.35, 0.0)),
    "head": ("neck", (0.0, 0.2, 0.0)),
    "arm_L_upper": ("spine_upper", (-0.405, 0.2, 0.0)),
    "arm_L_lower": ("arm_L_upper", (-0.47, 0.0, 0.0)),
    "hand_L": ("arm_L_lower", (-0.48, 0.0, 0.0)),
    "arm_R_upper": ("spine_upper", (0.405, 0.2, 0.0)),
    "arm_R_lower": ("arm_R_upper", (0.47, 0.0, 0.0)),
    "hand_R": ("arm_R_lower", (0.48, 0.0, 0.0)),
    "leg_L_upper": ("pelvis", (-0.144, -0.1, 0.0)),
    "leg_L_lower": ("leg_L_upper", (0.0, -0.36, 0.0)),
    "foot_L": ("leg_L_lower", (0.0, -0.54, 0.05)),
    "leg_R_upper": ("pelvis", (0.144, -0.1, 0.0)),
    "leg_R_lower": ("leg_R_upper", (0.0, -0.36, 0.0)),
    "foot_R": ("leg_R_lower", (0.0, -0.54, 0.05)),
}
ORDER = list(JOINTS.keys())

# Sole contact points in the foot joint's frame (heel, toe), matching the
# flat part of appendShoe()'s sole; the stance foot rolls over them.
SOLE_POINTS = [(0.0, -0.16, -0.12), (0.0, -0.16, 0.24)]

ARM_REST_DEG = 82.0  # arms hang slightly away from the hips


def quat_axis(axis, degrees):
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    n = math.sqrt(sum(a * a for a in axis))
    return (axis[0] / n * s, axis[1] / n * s, axis[2] / n * s, math.cos(half))


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qx(d):
    return quat_axis((1, 0, 0), d)


def qy(d):
    return quat_axis((0, 1, 0), d)


def qz(d):
    return quat_axis((0, 0, 1), d)


def forward_kinematics(locals_):
    world = {"root": ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))}
    for name in ORDER:
        parent = JOINTS[name][0]
        pos, rot = locals_[name]
        ppos, prot = world[parent]
        wp = qrot(prot, pos)
        world[name] = ((ppos[0] + wp[0], ppos[1] + wp[1], ppos[2] + wp[2]), qmul(prot, rot))
    return world


def sole_low(world, foot):
    pos, rot = world[foot]
    best = None
    for point in SOLE_POINTS:
        p = qrot(rot, point)
        y = pos[1] + p[1]
        if best is None or y < best[0]:
            best = (y, pos[2] + p[2])
    return best


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vscale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vlen(a):
    return math.sqrt(vdot(a, a))


def vnorm(a):
    n = vlen(a)
    return vscale(a, 1.0 / n) if n > 1e-9 else (0.0, -1.0, 0.0)


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def from_to(a, b):
    a = vnorm(a)
    b = vnorm(b)
    c = vcross(a, b)
    d = vdot(a, b)
    if d < -0.999999:
        return quat_axis((1.0, 0.0, 0.0), 180.0)
    q = (c[0], c[1], c[2], 1.0 + d)
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def hermite(p0, p1, m0, m1, u):
    u2, u3 = u * u, u * u * u
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1


HEEL = SOLE_POINTS[0]
BALL = SOLE_POINTS[1]
THIGH = 0.36
SHIN_BIND = (0.0, -0.54, 0.05)
SHIN = vlen(SHIN_BIND)
GROUND = -0.16


class Gait:
    """Foot trajectories with heel strike, flat foot and toe roll during
    stance (the contact point moves back at exactly `speed`, so a clip
    played at speed/`speed` never slides) and a lifted Hermite arc in
    swing. Legs are solved with two-bone IK; the pelvis rides as high as the
    stance leg allows, which produces the natural vertical bob."""

    def __init__(self, name, duration, speed, stance, heel_pitch, toe_pitch, heel_end, heel_off, swing_lift,
                 stance_knee, pelvis_yaw, pelvis_list, pelvis_sway, lean, arm_swing, elbow_base, elbow_swing,
                 thorax_counter, foot_width, contact_ahead, arm_out=0.0):
        self.name = name
        self.duration = duration
        self.speed = speed
        self.stance = stance
        self.heel_pitch = heel_pitch
        self.toe_pitch = toe_pitch
        self.heel_end = heel_end
        self.heel_off = heel_off
        self.swing_lift = swing_lift
        self.stance_knee = stance_knee
        self.pelvis_yaw = pelvis_yaw
        self.pelvis_list = pelvis_list
        self.pelvis_sway = pelvis_sway
        self.lean = lean
        self.arm_swing = arm_swing
        self.elbow_base = elbow_base
        self.elbow_swing = elbow_swing
        self.thorax_counter = thorax_counter
        self.foot_width = foot_width
        self.arm_out = arm_out
        travel = speed * stance * duration
        # Where the heel lands relative to the hip; runners land closer under the body.
        self.heel_strike_z = contact_ahead * travel - 0.25 * (BALL[2] - HEEL[2])

    def stance_foot(self, q):
        """Ankle position (y, z) and pitch (degrees, toe up positive) at stance phase q."""
        ground_z = self.heel_strike_z - self.speed * q * self.duration
        if q < self.heel_off:
            pitch = self.heel_pitch * (1.0 - smoothstep(0.0, self.heel_end, q))
            pivot_local = HEEL
            pivot_z = ground_z
        else:
            pitch = -self.toe_pitch * smoothstep(self.heel_off, self.stance, q)
            pivot_local = BALL
            pivot_z = ground_z + (BALL[2] - HEEL[2])
        r = qrot(qx(-pitch), pivot_local)
        return (GROUND - r[1], pivot_z - r[2], pitch)

    def foot(self, q):
        q %= 1.0
        if q < self.stance:
            return self.stance_foot(q)
        eps = 1e-3
        y0, z0, p0 = self.stance_foot(self.stance - eps)
        y0b, z0b, _ = self.stance_foot(self.stance - 2 * eps)
        y1, z1, p1 = self.stance_foot(0.0)
        y1b, z1b, _ = self.stance_foot(eps)
        swing_span = 1.0 - self.stance
        dy0, dz0 = (y0 - y0b) / eps * swing_span, (z0 - z0b) / eps * swing_span
        # Arrive with a softened forward velocity so the foot settles into
        # heel strike instead of overshooting and snapping back.
        dy1, dz1 = (y1b - y1) / eps * swing_span, 0.35 * (z1b - z1) / eps * swing_span
        u = (q - self.stance) / swing_span
        y = hermite(y0, y1, dy0, dy1, u) + self.swing_lift * math.sin(math.pi * u ** 0.75) ** 1.4
        z = hermite(z0, z1, dz0, dz1, u)
        pitch = p0 + (p1 - p0) * smoothstep(0.1, 0.95, u)
        return (y, z, pitch)

    def pelvis_rotation(self, phase):
        two_pi = 2.0 * math.pi
        yaw = self.pelvis_yaw * math.cos(two_pi * phase)
        roll = -self.pelvis_list * math.sin(two_pi * (phase - 0.05))
        sway = -self.pelvis_sway * math.sin(two_pi * (phase - 0.05))
        return yaw, roll, sway

    def max_hip_height(self, phase, side, knee_degrees):
        """Highest pelvis lift at which this leg reaches its ankle target with
        at least `knee_degrees` of knee flexion."""
        yaw, roll, sway = self.pelvis_rotation(phase)
        offset = 0.0 if side == "L" else 0.5
        y, z, _ = self.foot(phase + offset)
        rot = qmul(qy(yaw), qz(roll))
        pelvis = vadd(JOINTS["pelvis"][1], (sway, 0.0, 0.0))
        hip = vadd(pelvis, qrot(rot, JOINTS["leg_%s_upper" % side][1]))
        x = math.copysign(self.foot_width, hip[0])
        reach = math.sqrt(THIGH ** 2 + SHIN ** 2 + 2 * THIGH * SHIN * math.cos(math.radians(knee_degrees)))
        horizontal = (x - hip[0]) ** 2 + (z - hip[2]) ** 2
        return (y + math.sqrt(max(reach ** 2 - horizontal, 0.0))) - hip[1]

    def in_stance(self, phase, side):
        offset = 0.0 if side == "L" else 0.5
        return (phase + offset) % 1.0 < self.stance

    def stance_phase(self, phase, side):
        offset = 0.0 if side == "L" else 0.5
        return ((phase + offset) % 1.0) / self.stance

    def solve_leg(self, side, pelvis_pos, pelvis_rot, phase):
        offset = 0.0 if side == "L" else 0.5
        y, z, pitch = self.foot(phase + offset)
        hip = vadd(pelvis_pos, qrot(pelvis_rot, JOINTS["leg_%s_upper" % side][1]))
        target = (math.copysign(self.foot_width, hip[0]), y, z)
        to_target = vsub(target, hip)
        d = min(vlen(to_target), THIGH + SHIN - 1e-4)
        t_dir = vnorm(to_target)
        cos_a = (THIGH ** 2 + d ** 2 - SHIN ** 2) / (2 * THIGH * d)
        cos_a = min(max(cos_a, -1.0), 1.0)
        forward = qrot(pelvis_rot, (0.0, 0.0, 1.0))
        pole = vnorm(vsub(forward, vscale(t_dir, vdot(forward, t_dir))))
        knee = vadd(hip, vadd(vscale(t_dir, THIGH * cos_a), vscale(pole, THIGH * math.sqrt(1 - cos_a ** 2))))
        ankle = vadd(hip, vscale(t_dir, d))

        thigh_dir_local = qrot(qinv(pelvis_rot), vsub(knee, hip))
        q_hip = from_to((0.0, -1.0, 0.0), thigh_dir_local)
        thigh_world = qmul(pelvis_rot, q_hip)
        shin_dir_local = qrot(qinv(thigh_world), vsub(ankle, knee))
        q_knee = from_to(SHIN_BIND, shin_dir_local)
        shin_world = qmul(thigh_world, q_knee)
        q_foot = qmul(qinv(shin_world), qx(-pitch))
        return q_hip, q_knee, q_foot

    def pose(self, phase, pelvis_lift):
        L = {name: (JOINTS[name][1], (0.0, 0.0, 0.0, 1.0)) for name in ORDER}
        two_pi = 2.0 * math.pi
        yaw, roll, sway = self.pelvis_rotation(phase)
        pelvis_rot = qmul(qy(yaw), qz(roll))
        bx, by, bz = JOINTS["pelvis"][1]
        pelvis_pos = (bx + sway, by + pelvis_lift, bz)
        L["pelvis"] = (pelvis_pos, pelvis_rot)

        L["spine_lower"] = (JOINTS["spine_lower"][1], qmul(qy(-yaw * 0.45), qz(-roll * 0.5)))
        thorax_yaw = -yaw * self.thorax_counter
        L["spine_upper"] = (JOINTS["spine_upper"][1], qmul(qy(thorax_yaw), qmul(qx(self.lean), qz(-roll * 0.3))))
        net_yaw = yaw - yaw * 0.45 + thorax_yaw
        L["neck"] = (JOINTS["neck"][1], qmul(qy(-net_yaw * 0.5), qx(-self.lean * 0.45)))
        L["head"] = (JOINTS["head"][1], qmul(qy(-net_yaw * 0.5), qx(-self.lean * 0.35)))

        for side, offset in (("L", 0.0), ("R", 0.5)):
            p = (phase + offset) % 1.0
            q_hip, q_knee, q_foot = self.solve_leg(side, pelvis_pos, pelvis_rot, phase)
            L["leg_%s_upper" % side] = (JOINTS["leg_%s_upper" % side][1], q_hip)
            L["leg_%s_lower" % side] = (JOINTS["leg_%s_lower" % side][1], q_knee)
            L["foot_%s" % side] = (JOINTS["foot_%s" % side][1], q_foot)

            # Arms swing against the same-side leg: forward while it trails.
            swing = -self.arm_swing * math.cos(two_pi * (p - 0.03))
            mirror = -1.0 if side == "L" else 1.0
            rest = qz(-mirror * (ARM_REST_DEG - self.arm_out))
            # Negative X rotation brings a hanging arm forward.
            upper = qmul(qx(-swing), rest)
            L["arm_%s_upper" % side] = (JOINTS["arm_%s_upper" % side][1], upper)
            # Elbow flexion about the axis that is world X in the hanging
            # pose, so the forearm bends forward rather than sideways.
            elbow = self.elbow_base + self.elbow_swing * max(0.0, swing / max(self.arm_swing, 1e-6))
            axis = qrot(qinv(rest), (1.0, 0.0, 0.0))
            L["arm_%s_lower" % side] = (JOINTS["arm_%s_lower" % side][1], quat_axis(axis, -elbow))
        return L


def solve_lift(gait, samples):
    """Pelvis lift per sample: as high as the stance legs allow (knee flexion
    follows gait.stance_knee over stance), smoothed; ballistic in flight."""
    lifts = []
    for i in range(samples):
        phase = i / samples
        limits = []
        for side in ("L", "R"):
            if gait.in_stance(phase, side):
                u = gait.stance_phase(phase, side)
                knee = gait.stance_knee[0] + (gait.stance_knee[1] - gait.stance_knee[0]) * math.sin(math.pi * u)
                limits.append(gait.max_hip_height(phase, side, knee))
        reachable = min(gait.max_hip_height(phase, side, 3.0) for side in ("L", "R"))
        lifts.append(min(min(limits), reachable) if limits else None)

    if any(l is None for l in lifts):
        n = samples
        i = 0
        while i < n:
            if lifts[i] is not None:
                i += 1
                continue
            start = i
            while lifts[i % n] is None:
                i += 1
            end = i
            before = lifts[(start - 1) % n]
            after = lifts[end % n]
            span = end - start + 1
            flight_seconds = span / n * gait.duration
            peak = 9.81 * flight_seconds * flight_seconds / 8.0
            for k in range(start, end):
                u = (k - start + 1) / span
                phase = (k % n) / n
                reachable = min(gait.max_hip_height(phase, side, 3.0) for side in ("L", "R"))
                lifts[k % n] = min(before + (after - before) * u + 4.0 * peak * u * (1.0 - u), reachable)

    smoothed = []
    for i in range(samples):
        window = [lifts[(i + k) % samples] for k in (-1, 0, 1)]
        smoothed.append(min(lifts[i], 0.25 * window[0] + 0.5 * window[1] + 0.25 * window[2]))
    return smoothed


def validate(gait, lifts, samples):
    """Lowest sole point per sample relative to the ground, and the worst
    stance-foot slip in units/s at the clip's native speed."""
    worst_penetration = 0.0
    min_swing_clearance = 1e9
    for i in range(samples):
        phase = i / samples
        world = forward_kinematics(gait.pose(phase, lifts[i]))
        for side in ("L", "R"):
            low = sole_low(world, "foot_" + side)[0] - GROUND
            if gait.in_stance(phase, side):
                worst_penetration = max(worst_penetration, abs(low))
            else:
                min_swing_clearance = min(min_swing_clearance, low)
    return worst_penetration, min_swing_clearance


def fmt(v):
    s = "%.6f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def write_clip(gait, path, samples):
    lifts = solve_lift(gait, samples)
    poses = [gait.pose(i / samples, lifts[i]) for i in range(samples)]
    poses.append(poses[0])
    tracks = ["pelvis", "spine_lower", "spine_upper", "neck", "head", "arm_L_upper", "arm_L_lower", "arm_R_upper",
              "arm_R_lower", "leg_L_upper", "leg_L_lower", "foot_L", "leg_R_upper", "leg_R_lower", "foot_R"]
    lines = ["ANIMCLIP 1", "NAME " + gait.name, "DURATION " + fmt(gait.duration), "LOOPING 1"]
    for track in tracks:
        lines.append("TRACK " + track)
        for i, pose in enumerate(poses):
            t = gait.duration * i / samples
            (px, py, pz), (x, y, z, w) = pose[track]
            if w < 0.0:
                x, y, z, w = -x, -y, -z, -w
            lines.append("KEY %s %s %s %s %s %s %s %s 1 1 1 0" % tuple(fmt(v) for v in (t, px, py, pz, x, y, z, w)))
    lines.append("END")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return validate(gait, lifts, samples)


WALK = Gait(
    "Walk", duration=1.0, speed=1.5, stance=0.6, heel_pitch=16.0, toe_pitch=32.0, heel_end=0.1, heel_off=0.38,
    swing_lift=0.07, stance_knee=(8.0, 20.0), pelvis_yaw=5.0, pelvis_list=3.5, pelvis_sway=0.022, lean=3.0,
    arm_swing=16.0, elbow_base=14.0, elbow_swing=16.0, thorax_counter=0.9, foot_width=0.125, contact_ahead=0.52)

RUN = Gait(
    "Run", duration=0.6, speed=5.0, stance=0.33, heel_pitch=6.0, toe_pitch=28.0, heel_end=0.05, heel_off=0.12,
    swing_lift=0.26, stance_knee=(16.0, 33.0), pelvis_yaw=6.0, pelvis_list=3.0, pelvis_sway=0.012, lean=10.0,
    arm_swing=24.0, elbow_base=72.0, elbow_swing=14.0, thorax_counter=1.1, foot_width=0.11, contact_ahead=0.36,
    arm_out=4.0)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "assets", "animations")
    for gait, samples in ((WALK, 32), (RUN, 24)):
        path = os.path.join(out_dir, gait.name.lower() + ".anim")
        penetration, clearance = write_clip(gait, path, samples)
        print("%s: %s -- native ground speed %.2f units/s, stance contact error %.4f, min swing clearance %.3f" %
              (gait.name, os.path.normpath(path), gait.speed, penetration, clearance))


if __name__ == "__main__":
    main()
