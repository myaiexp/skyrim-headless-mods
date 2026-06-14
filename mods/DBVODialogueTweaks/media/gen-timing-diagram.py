import math

W, H = 1600, 900
BG="#14161b"; TXT="#eceef2"; SUB="#98a0ad"; SLATE="#8b93a3"; BLUE="#6ea8d8"
BAND="#262932"; BANDB="#3a3f4b"; AMBER="#e0a85c"

TX0, TX1 = 200, 1400
TW = TX1 - TX0
def tx(t): return TX0 + t*TW

ROW = 470

# single positive timeline — what the mod does, no comparison
LINE0, LEND = 0.00, 0.42
GAP0, GAP1  = 0.42, 0.47
REP0, REP1  = 0.47, 0.95

def wave(x0n, x1n, cy, color, maxamp, seed=0.0):
    x0, x1 = tx(x0n), tx(x1n); pitch, bw = 8, 4.5; out=[]; i=0; x=x0
    while x < x1 - bw:
        e = (0.30 + 0.42*abs(math.sin(i*0.55+seed)) + 0.30*abs(math.sin(i*0.17+seed*1.7)) + 0.18*math.sin(i*1.31+seed))
        e = max(0.12, min(1.0, e)); h = maxamp*e
        out.append(f'<rect x="{x:.1f}" y="{cy-h:.1f}" width="{bw}" height="{2*h:.1f}" rx="1.6" fill="{color}"/>')
        x += pitch; i += 1
    return "\n".join(out)

p=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Liberation Sans, DejaVu Sans, sans-serif">']
p.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
p.append(f'<text x="120" y="130" font-size="50" font-weight="700" fill="{TXT}">The reply lands when your line ends</text>')
p.append(f'<text x="122" y="176" font-size="26" fill="{SUB}">A small SKSE plugin watches your voiced line and cues the NPC&#39;s reply the moment it stops.</text>')

xm = tx(LEND)
# line-end marker
p.append(f'<line x1="{xm}" y1="{ROW-150}" x2="{xm}" y2="{ROW+70}" stroke="{AMBER}" stroke-width="2.5" stroke-dasharray="6 6"/>')
p.append(f'<text x="{xm}" y="{ROW-162}" font-size="24" font-weight="700" fill="{AMBER}" text-anchor="middle">line ends — detected</text>')

# gap band
g0, g1 = tx(GAP0), tx(GAP1)
p.append(f'<rect x="{g0}" y="{ROW-46}" width="{g1-g0}" height="92" rx="5" fill="{BAND}" stroke="{BANDB}" stroke-width="1.5"/>')

# waveforms
p.append(wave(LINE0, LEND, ROW, SLATE, 40, 0.3))
p.append(wave(REP0, REP1, ROW, BLUE, 38, 2.1))

# segment labels above
p.append(f'<text x="{tx(LINE0)+6}" y="{ROW-70}" font-size="22" fill="{SUB}">your voiced line</text>')
p.append(f'<text x="{tx(REP0)+6}" y="{ROW-70}" font-size="22" fill="{BLUE}">NPC reply</text>')

# bracket under your line: "the plugin watches this audio"
by = ROW+78
p.append(f'<path d="M{tx(LINE0)} {by-10} L{tx(LINE0)} {by} L{xm} {by} L{xm} {by-10}" fill="none" stroke="{BANDB}" stroke-width="1.8"/>')
p.append(f'<text x="{(tx(LINE0)+xm)/2-60}" y="{by+30}" font-size="20" fill="{SUB}" text-anchor="middle">the plugin watches this audio for the real end</text>')

# gap callout below, baseline aligned with the bracket label
gc = (g0+g1)/2
p.append(f'<line x1="{gc}" y1="{ROW+46}" x2="{gc}" y2="{ROW+88}" stroke="{BANDB}" stroke-width="1.5"/>')
p.append(f'<text x="{gc}" y="{ROW+108}" font-size="20" fill="{SUB}" text-anchor="middle">gap (default 250 ms)</text>')

# time axis
ay=720
p.append(f'<line x1="{TX0}" y1="{ay}" x2="{TX1}" y2="{ay}" stroke="{BANDB}" stroke-width="2"/>')
p.append(f'<polygon points="{TX1},{ay} {TX1-14},{ay-7} {TX1-14},{ay+7}" fill="{BANDB}"/>')
p.append(f'<text x="{TX0}" y="{ay+34}" font-size="20" fill="{SUB}">time</text>')
p.append('</svg>')
open("/home/mse/Projects/skyrim-headless-mods/mods/DBVODialogueTweaks/media/timing-diagram.svg","w").write("\n".join(p)); print("ok")
