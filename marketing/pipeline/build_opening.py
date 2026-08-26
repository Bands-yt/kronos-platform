import math, random, os, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

W, H = 1920, 1080
FPS = 30
DURATION = 6.4
NFRAMES = int(DURATION * FPS)

VOID    = "#0a0908"
GUNMETAL= "#34373b"
STEEL   = "#6a6f76"
CRIMSON = "#ff2a3c"
EMBER   = "#5c0e14"
BONE    = "#f2ede4"
AMBER   = "#ff6a2e"   # warm horizon glow -- stays in the crimson/ember family, not a new hue family

CX, CY = 960, 560   # cage/burst focal point (matches thumbnail's rotation pivot)

# ---------- timeline (seconds) ----------
T_PUSHIN_END   = 1.10
T_SHAKE_END    = 1.60
T_BURST        = 1.75
T_FLASH_END    = 1.87
T_SHARDS_END   = 3.10
T_LANDSCAPE_IN = 3.50
T_WIPE_START   = 3.65
T_WIPE_END     = 4.05
T_WORDMARK_IN  = 4.55
T_TAGLINE_IN   = 5.05
T_HUD_IN       = 5.55
T_END          = DURATION

# ---------- easing ----------
def clamp01(x): return 0.0 if x < 0 else (1.0 if x > 1 else x)
def seg(t, a, b):
    if b <= a: return 1.0 if t >= b else 0.0
    return clamp01((t - a) / (b - a))
def lerp(a, b, t): return a + (b - a) * t
def ease_out_cubic(t): t = clamp01(t); return 1 - (1 - t) ** 3
def ease_in_cubic(t): t = clamp01(t); return t ** 3
def ease_in_out(t):
    t = clamp01(t)
    return 3 * t * t - 2 * t * t * t
def smoothstep(a, b, t): return ease_in_out(seg(t, a, b))

random.seed(11)

# ============================================================
#  Cage bars (same visual language as the thumbnail)
# ============================================================
BAR_XS = list(range(-20, W + 60, 128))
CROSS_YS = [140, 430, 720, 1010]

def cage_svg(shake_px=0.0, opacity=1.0):
    if opacity <= 0.002:
        return ""
    parts = []
    for x in BAR_XS:
        jx = x + random.uniform(-shake_px, shake_px) if shake_px > 0 else x
        parts.append(f'<rect x="{jx:.1f}" y="-60" width="22" height="{H+140}" fill="{GUNMETAL}" opacity="{0.9*opacity:.3f}"/>'
                      f'<rect x="{jx:.1f}" y="-60" width="4" height="{H+140}" fill="{STEEL}" opacity="{0.7*opacity:.3f}"/>')
    for y in CROSS_YS:
        jy = y + random.uniform(-shake_px, shake_px) if shake_px > 0 else y
        parts.append(f'<rect x="-60" y="{jy-7:.1f}" width="{W+120}" height="14" fill="{GUNMETAL}" opacity="{0.85*opacity:.3f}"/>')
    return "\n".join(parts)

# ============================================================
#  Shard fragments -- radial explosion from the burst point
# ============================================================
N_SHARDS = 26
_shards = []
for i in range(N_SHARDS):
    ang = random.uniform(0, 2 * math.pi)
    speed = random.uniform(340, 980)
    _shards.append(dict(
        ang=ang, speed=speed,
        spin=random.uniform(-540, 540),
        size=random.uniform(24, 62),
        r0=random.uniform(20, 90),          # starting radius from center (bar debris, not a single point)
        seed=random.uniform(0, 10),
    ))

def shard_shape(seed, size):
    pts = []
    for a in (20, 100, 200, 300):
        rr = size * (0.65 + 0.35 * ((math.sin(seed * a) + 1) / 2))
        ang = math.radians(a + 12 * math.sin(seed))
        pts.append((rr * math.cos(ang), rr * math.sin(ang)))
    return " ".join(f"{px:.1f},{py:.1f}" for px, py in pts)

def shards_svg(t):
    if t < T_BURST:
        return ""
    dt = t - T_BURST
    fade = 1.0 - smoothstep(T_SHARDS_END - 0.9, T_SHARDS_END, t)
    if fade <= 0.002:
        return ""
    parts = []
    for s in _shards:
        dist = s["r0"] + s["speed"] * dt * (1.0 - 0.18 * dt)   # mild drag
        cx = CX + math.cos(s["ang"]) * dist
        cy = CY + math.sin(s["ang"]) * dist
        rot = s["spin"] * dt
        pts = shard_shape(s["seed"], s["size"])
        parts.append(
            f'<g transform="translate({cx:.1f},{cy:.1f}) rotate({rot:.1f})">'
            f'<polygon points="{pts}" fill="{STEEL}" stroke="{CRIMSON}" stroke-width="2.2" '
            f'opacity="{0.95*fade:.3f}" filter="url(#shardGlow)"/></g>'
        )
    return "\n".join(parts)

# ============================================================
#  Sparrow -- flat vector silhouette, simple wing-flap rig
# ============================================================
def sparrow_svg(cx, cy, scale, flap_t, color=VOID, glow=False):
    flap = math.sin(flap_t * 2 * math.pi) * 34
    body = "M -30,0 C -30,-14 -8,-20 14,-14 C 30,-10 36,-2 40,0 C 36,2 30,10 14,14 C -8,20 -30,14 -30,0 Z"
    tail = "M -30,0 L -54,-10 L -46,0 L -54,10 Z"
    head = f'<circle cx="34" cy="-4" r="7.5" fill="{color}"/>'
    wing_r = f'<path d="M -4,-2 C 18,{-6-flap} 46,{-4-flap*1.4} 58,{2-flap*0.4} C 40,{6-flap*0.6} 12,{8-flap*0.2} -4,-2 Z" fill="{color}"/>'
    wing_l = f'<path d="M -4,2 C 18,{6+flap} 46,{4+flap*1.4} 58,{-2+flap*0.4} C 40,{-6+flap*0.6} 12,{-8+flap*0.2} -4,2 Z" fill="{color}" opacity="0.85"/>'
    glow_filter = ' filter="url(#shardGlow)"' if glow else ""
    return (f'<g transform="translate({cx:.1f},{cy:.1f}) scale({scale:.3f})"{glow_filter}>'
            f'{wing_l}<path d="{body}" fill="{color}"/><path d="{tail}" fill="{color}"/>{head}{wing_r}'
            f'</g>')

def sparrow_flight(t):
    if t < T_BURST:
        return ""
    p = ease_out_cubic(seg(t, T_BURST, T_LANDSCAPE_IN + 0.3))
    # gentle arc: fast rise then a long glide toward the upper right
    x = lerp(CX, 1620, p)
    y = lerp(CY - 20, 300, ease_in_out(p)) - 90 * math.sin(p * math.pi) * 0.6
    scale = lerp(1.55, 0.55, p)
    flap_speed = lerp(7.2, 3.0, p)   # flaps fast at launch, settles into a glide cadence
    flap_t = t * flap_speed
    fade_out = 1.0 - smoothstep(T_WIPE_START, T_WIPE_START + 0.25, t)
    if fade_out <= 0.002:
        return ""
    return f'<g opacity="{fade_out:.3f}">{sparrow_svg(x, y, scale, flap_t, color=BONE, glow=True)}</g>'

# ============================================================
#  Landscape reveal
# ============================================================
def landscape_svg(t):
    a = smoothstep(T_BURST + 0.15, T_LANDSCAPE_IN, t)
    if a <= 0.002:
        return ""
    horizon_y = 760
    mountains1 = f'<polygon points="0,{horizon_y+40} 260,{horizon_y-70} 520,{horizon_y-10} 780,{horizon_y-110} 1040,{horizon_y-30} 1320,{horizon_y-140} 1620,{horizon_y-40} 1920,{horizon_y-90} 1920,{H} 0,{H}" fill="{VOID}" opacity="{0.92*a:.3f}"/>'
    mountains2 = f'<polygon points="0,{horizon_y+90} 340,{horizon_y+10} 700,{horizon_y+60} 1080,{horizon_y-20} 1460,{horizon_y+50} 1920,{horizon_y+0} 1920,{H} 0,{H}" fill="#150c0a" opacity="{0.96*a:.3f}"/>'
    return (
        f'<rect width="{W}" height="{H}" fill="url(#dawnSky)" opacity="{a:.3f}"/>'
        f'<circle cx="1500" cy="{horizon_y-60}" r="150" fill="url(#sunGlow)" opacity="{a:.3f}"/>'
        f'{mountains1}{mountains2}'
    )

# ============================================================
#  Title card (adapted from the thumbnail composition)
# ============================================================
def title_card_svg(t):
    # backdrop (bg/cage/beam) snaps in fast right after the wipe; the
    # wordmark/tagline/HUD below do their own separately-timed entrances
    p = smoothstep(T_WIPE_END, T_WIPE_END + 0.15, t)
    if p <= 0.001:
        return ""
    wordmark_p = ease_out_cubic(seg(t, T_WORDMARK_IN, T_WORDMARK_IN + 0.5))
    tagline_p  = ease_out_cubic(seg(t, T_TAGLINE_IN, T_TAGLINE_IN + 0.5))
    hud_p      = seg(t, T_HUD_IN, T_HUD_IN + 0.5)

    bg = f'<rect width="{W}" height="{H}" fill="{VOID}"/>'
    cage = f'<g transform="rotate(-3 {CX} {CY})" opacity="0.85">{cage_svg()}</g>'

    beam_x1, beam_y1, beam_x2, beam_y2 = 60, 1000, 1560, 120
    dx, dy = beam_x2 - beam_x1, beam_y2 - beam_y1
    beam_angle = math.degrees(math.atan2(dy, dx))
    bcx, bcy = (beam_x1 + beam_x2) / 2, (beam_y1 + beam_y2) / 2
    beam_len = math.hypot(dx, dy) + 700
    beam = (f'<g transform="rotate({beam_angle:.2f} {bcx:.1f} {bcy:.1f})">'
            f'<rect x="{bcx-beam_len/2:.1f}" y="{bcy-190:.1f}" width="{beam_len:.1f}" height="380" fill="url(#beamGrad)" filter="url(#beamBlur)"/>'
            f'</g>')

    tagline_dx = lerp(-2200, 0, tagline_p)
    wordmark = (f'<g transform="rotate(-3 {CX} {CY}) translate(0,{lerp(30,0,wordmark_p):.1f})" opacity="{wordmark_p:.3f}">'
                f'<text x="70" y="600" class="kronos" font-size="360" fill="{BONE}" '
                f'textLength="1360" lengthAdjust="spacingAndGlyphs" filter="url(#titleGlow)">KRONOS</text></g>')
    tagline = (f'<g transform="rotate(-1.4 850 730) translate({tagline_dx:.1f},0)" opacity="{tagline_p:.3f}">'
               f'<polygon points="72,656 300,646 620,652 980,644 1320,650 1560,644 1596,700 1560,748 1300,754 980,762 620,756 300,762 90,754 58,706" fill="{CRIMSON}"/>'
               f'<text x="108" y="732" class="tagline" font-size="126" fill="{VOID}" letter-spacing="3" '
               f'textLength="1440" lengthAdjust="spacingAndGlyphs">FREEDOM AT LAST</text></g>')

    hud_tag_full = "KRONOS ENGINE · VULKAN 1.3 · REAL-TIME"
    reveal_n = max(0, min(len(hud_tag_full), int(len(hud_tag_full) * clamp01(hud_p))))
    hud_tag_txt = hud_tag_full[:reveal_n]
    corner_dash = lerp(0, 210, seg(t, T_HUD_IN - 0.15, T_HUD_IN + 0.2))
    hud = (f'<g stroke="{CRIMSON}" stroke-width="3" fill="none" opacity="0.8">'
           f'<path d="M 46 100 L 46 46 L 100 46" stroke-dasharray="{corner_dash:.1f} 400"/>'
           f'<path d="M {W-100} {H-46} L {W-46} {H-46} L {W-46} {H-100}" stroke-dasharray="{corner_dash:.1f} 400"/>'
           f'</g>'
           f'<text x="46" y="1032" class="tech" font-size="24" fill="{BONE}" letter-spacing="2" opacity="0.6">{hud_tag_txt}</text>')

    return f'<g opacity="{p:.3f}">{bg}{cage}{beam}{wordmark}{tagline}{hud}</g>'

# ============================================================
#  Wipe transition
# ============================================================
def wipe_svg(t):
    p = seg(t, T_WIPE_START, T_WIPE_END)
    if p <= 0.001 or p >= 0.999:
        return ""
    x = lerp(-400, W + 400, ease_in_cubic(p))
    return (f'<g transform="rotate(-18 {x:.1f} {H/2})">'
            f'<rect x="{x-260:.1f}" y="-200" width="520" height="{H+400}" fill="{BONE}" opacity="{0.9*(1-abs(p-0.5)*1.1):.3f}" filter="url(#beamBlur)"/>'
            f'</g>')

# ============================================================
#  Frame assembly
# ============================================================
STYLE = f'''
@font-face {{ font-family:'Anton Local'; src:url(data:font/woff2;base64,ANTON_B64) format('woff2'); font-weight:400; }}
@font-face {{ font-family:'Oswald Local Bold'; src:url(data:font/woff2;base64,OSWALD700_B64) format('woff2'); font-weight:700; }}
@font-face {{ font-family:'JBMono Local'; src:url(data:font/woff2;base64,MONO_B64) format('woff2'); font-weight:500; }}
.kronos {{ font-family:'Anton Local', Impact, 'Arial Narrow', sans-serif; }}
.tagline {{ font-family:'Oswald Local Bold', Impact, sans-serif; font-weight:700; }}
.tech {{ font-family:'JBMono Local', 'Courier New', monospace; font-weight:500; }}
'''

DEFS = f'''
<radialGradient id="vignette" cx="50%" cy="46%" r="75%">
  <stop offset="0%" stop-color="#000000" stop-opacity="0"/>
  <stop offset="68%" stop-color="#000000" stop-opacity="0"/>
  <stop offset="100%" stop-color="#000000" stop-opacity="0.65"/>
</radialGradient>
<linearGradient id="beamGrad" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0%" stop-color="{CRIMSON}" stop-opacity="0"/>
  <stop offset="42%" stop-color="{EMBER}" stop-opacity="0.85"/>
  <stop offset="50%" stop-color="{CRIMSON}" stop-opacity="1"/>
  <stop offset="58%" stop-color="{EMBER}" stop-opacity="0.85"/>
  <stop offset="100%" stop-color="{CRIMSON}" stop-opacity="0"/>
</linearGradient>
<radialGradient id="coreGlow" cx="50%" cy="50%" r="50%">
  <stop offset="0%" stop-color="{BONE}" stop-opacity="1"/>
  <stop offset="35%" stop-color="{CRIMSON}" stop-opacity="0.9"/>
  <stop offset="100%" stop-color="{CRIMSON}" stop-opacity="0"/>
</radialGradient>
<linearGradient id="dawnSky" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0%" stop-color="#160907"/>
  <stop offset="55%" stop-color="#2a0f0c"/>
  <stop offset="78%" stop-color="{EMBER}"/>
  <stop offset="100%" stop-color="{AMBER}"/>
</linearGradient>
<radialGradient id="sunGlow" cx="50%" cy="50%" r="50%">
  <stop offset="0%" stop-color="#fff3e6" stop-opacity="1"/>
  <stop offset="45%" stop-color="{AMBER}" stop-opacity="0.9"/>
  <stop offset="100%" stop-color="{AMBER}" stop-opacity="0"/>
</radialGradient>
<filter id="beamBlur" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="22"/></filter>
<filter id="softBlur" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="10"/></filter>
<filter id="shardGlow" x="-80%" y="-80%" width="260%" height="260%">
  <feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
</filter>
<filter id="titleGlow" x="-30%" y="-30%" width="160%" height="160%">
  <feGaussianBlur stdDeviation="14" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
</filter>
'''

def frame_svg(t):
    push = lerp(1.0, 1.12, ease_in_out(seg(t, 0, T_PUSHIN_END)))
    shake = 0.0
    if t > T_PUSHIN_END:
        shake = lerp(0, 9, seg(t, T_PUSHIN_END, T_SHAKE_END))
    if t >= T_BURST:
        shake = 0.0

    cage_opacity = 1.0
    if t >= T_BURST:
        cage_opacity = 1.0 - smoothstep(T_BURST, T_BURST + 0.22, t)

    core_a = smoothstep(T_PUSHIN_END, T_SHAKE_END, t) * (1.0 - smoothstep(T_BURST - 0.05, T_BURST + 0.05, t))
    pulse = 1.0 + 0.25 * math.sin(t * 26)
    core_r = lerp(10, 46, smoothstep(T_PUSHIN_END, T_SHAKE_END, t)) * pulse

    flash_a = 0.0
    if T_BURST <= t <= T_FLASH_END:
        flash_a = (1.0 - seg(t, T_BURST, T_FLASH_END)) ** 1.6

    scene_a = 1.0 - smoothstep(T_WIPE_START + 0.05, T_WIPE_END, t)

    body = []
    body.append(f'<rect width="{W}" height="{H}" fill="{VOID}"/>')
    body.append(f'<g opacity="{scene_a:.3f}">')
    body.append(landscape_svg(t))
    body.append(f'<g transform="translate({CX} {CY}) scale({push:.4f}) translate({-CX} {-CY}) rotate(-3 {CX} {CY})" opacity="{cage_opacity:.3f}">')
    body.append(cage_svg(shake_px=shake))
    body.append('</g>')
    if core_a > 0.002:
        body.append(f'<circle cx="{CX}" cy="{CY}" r="{core_r:.1f}" fill="url(#coreGlow)" opacity="{core_a:.3f}" filter="url(#softBlur)"/>')
    body.append(shards_svg(t))
    body.append(sparrow_flight(t))
    body.append('</g>')  # scene_a group
    if flash_a > 0.002:
        body.append(f'<rect width="{W}" height="{H}" fill="{BONE}" opacity="{flash_a:.3f}"/>')
        body.append(f'<rect width="{W}" height="{H}" fill="{CRIMSON}" opacity="{flash_a*0.5:.3f}"/>')
    body.append(wipe_svg(t))
    body.append(title_card_svg(t))
    body.append(f'<rect width="{W}" height="{H}" fill="url(#vignette)"/>')

    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">'
            f'<defs><style>{STYLE}</style>{DEFS}</defs>{"".join(body)}</svg>')

def load_fonts():
    base = os.path.join(SCRIPT_DIR, "fonts")
    def rd(name):
        with open(f"{base}/{name}") as f: return f.read().strip()
    return rd("Anton_400.b64"), rd("Oswald_700.b64"), rd("JetBrains+Mono_500.b64")

if __name__ == "__main__":
    anton, oswald700, mono = load_fonts()
    STYLE = STYLE.replace("ANTON_B64", anton).replace("OSWALD700_B64", oswald700).replace("MONO_B64", mono)

    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRIPT_DIR, "frames")
    os.makedirs(out_dir, exist_ok=True)

    which = sys.argv[2] if len(sys.argv) > 2 else "all"
    if which == "all":
        for i in range(NFRAMES):
            t = i / FPS
            svg = frame_svg(t)
            with open(f"{out_dir}/f_{i:04d}.svg", "w") as f:
                f.write(svg)
        print(f"wrote {NFRAMES} frame SVGs to {out_dir}")
    else:
        idxs = [int(x) for x in which.split(",")]
        for i in idxs:
            t = i / FPS
            svg = frame_svg(t)
            with open(f"{out_dir}/f_{i:04d}.svg", "w") as f:
                f.write(svg)
        print("wrote", idxs)
