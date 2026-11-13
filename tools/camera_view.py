"""How far ahead of the car the race camera shows the ground: the camera of 1.0.1 and App::raceCamera's lead.

Follows the camera at 60 fps for a car driving straight at a speed in each screen direction until it settles, then
casts the screen's corners onto the ground (Renderer::render: target at focus.z - 0.06 h, eye at focus.z + 0.36 h,
height h, fovy 45) and prints the metres from the car to the edge of the picture the way it drives, and where the car
sits on the screen (x, y in per cent from the top left), for 16:9 and 16:10 (docs/DESIGN.md "Seeing the road").

    python tools/camera_view.py [UP DOWN SIDE]     (the lead's shares of the height; default App::raceCamera's)
"""
import math
import sys

FOVY = 45.0


def norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def camera(fx, fz, h):
    p = (fx, h, fz + h * 0.36)
    t = (fx, 0.0, fz - h * 0.06)
    f = norm(tuple(t[i] - p[i] for i in range(3)))
    r = norm(cross(f, (0, 1, 0)))
    u = cross(r, f)
    return p, f, r, u


def ground(cam, sx, sy, aspect):
    """The ground point under a screen point (sx, sy in -1..1, sy up)."""
    p, f, r, u = cam
    th = math.tan(math.radians(FOVY) / 2)
    d = tuple(f[i] + sx * th * aspect * r[i] + sy * th * u[i] for i in range(3))
    k = -p[1] / d[1]
    return (p[0] + d[0] * k, p[2] + d[2] * k)


def screen(cam, x, z, aspect):
    """Where a ground point lands on the screen (0..1 from the top-left)."""
    p, f, r, u = cam
    v = (x - p[0], -p[1], z - p[2])
    dz = sum(v[i] * f[i] for i in range(3))
    th = math.tan(math.radians(FOVY) / 2)
    sx = sum(v[i] * r[i] for i in range(3)) / (dz * th * aspect)
    sy = sum(v[i] * u[i] for i in range(3)) / (dz * th)
    return (0.5 + sx * 0.5, 0.5 - sy * 0.5)


def ahead(cam, car, d, aspect):
    """Metres from the car to the picture's edge along d (the ground quad of the screen)."""
    q = [ground(cam, -1, 1, aspect), ground(cam, 1, 1, aspect), ground(cam, 1, -1, aspect), ground(cam, -1, -1, aspect)]
    best = 1e9
    for i in range(4):
        a, b = q[i], q[(i + 1) % 4]
        e = (b[0] - a[0], b[1] - a[1])
        den = d[0] * e[1] - d[1] * e[0]
        if abs(den) < 1e-9:
            continue
        w = (a[0] - car[0], a[1] - car[1])
        t = (w[0] * e[1] - w[1] * e[0]) / den
        s = (w[0] * d[1] - w[1] * d[0]) / den
        if t > 0 and 0 <= s <= 1:
            best = min(best, t)
    return best


def old_cam(speed, d, frames=600, dt=1 / 60):
    car = [0.0, 0.0]
    vel = (d[0] * speed, d[1] * speed)
    focus = list(car)
    h = 64.0
    for _ in range(frames):
        car[0] += vel[0] * dt
        car[1] += vel[1] * dt
        goal = (car[0] + vel[0] * 0.5, car[1] + vel[1] * 0.5)
        a = min(1.0, dt * 4.0)
        focus[0] += (goal[0] - focus[0]) * a
        focus[1] += (goal[1] - focus[1]) * a
        h += ((58 + speed * 0.55) - h) * min(1.0, dt * 1.5)
    return car, focus, h


def new_cam(speed, d, up, down, side, frames=600, dt=1 / 60):
    car = [0.0, 0.0]
    vel = (d[0] * speed, d[1] * speed)
    lead = [0.0, 0.0]
    h = 64.0
    for _ in range(frames):
        car[0] += vel[0] * dt
        car[1] += vel[1] * dt
        h += ((58 + speed * 0.55) - h) * min(1.0, dt * 1.5)
        reach = min(1.0, max(0.0, (speed - 3.0) / 12.0))
        dx, dz = d
        want = (dx * side * h * reach, dz * (down if dz > 0 else up) * h * reach)
        b = min(1.0, dt * 1.6)
        lead[0] += (want[0] - lead[0]) * b
        lead[1] += (want[1] - lead[1]) * b
    return car, [car[0] + lead[0], car[1] + lead[1]], h   # the anchor goes with the car


def table(name, fn, speed):
    print('%s at %.0f km/h' % (name, speed * 3.6))
    for aspect, an in ((16 / 9, '16:9'), (16 / 10, '16:10')):
        row = []
        for dn, d in (('up', (0, -1)), ('down', (0, 1)), ('left', (-1, 0)), ('right', (1, 0))):
            car, focus, h = fn(speed, d)
            cam = camera(focus[0], focus[1], h)
            a = ahead(cam, car, d, aspect)
            sp = screen(cam, car[0], car[1], aspect)
            row.append('%s %5.1f m (car at %2.0f%%,%2.0f%%)' % (dn, a, sp[0] * 100, sp[1] * 100))
        print('  %-5s h %.1f  ' % (an, h) + '   '.join(row))


if __name__ == '__main__':
    up, down, side = (float(x) for x in sys.argv[1:4]) if len(sys.argv) >= 4 else (0.18, 0.465, 0.35)
    for v in (30.0, 15.0, 50.0):
        table('before', old_cam, v)
        table('after (up %.3f down %.3f side %.3f)' % (up, down, side), lambda s, d: new_cam(s, d, up, down, side), v)
