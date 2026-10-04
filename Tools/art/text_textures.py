#!/usr/bin/env python3
"""Text textures of the dive bar (venue-dive-bar 8.1-8.4, TXT route): every readable word in the venue is rendered here with
the OFL / Apache fonts in Art/Fonts (AI images garble text), fictional brands only (8.1; no real brand, logo or mascot).
Host Python with Pillow; deterministic (no clock, seeded jitter).

  python Tools/art/fetch_fonts.py            # once: the pinned fonts
  python Tools/art/text_textures.py          # -> Art/DiveBar/Textures/Labels/T_DB_Labels_BC.png + labels.json

Output: one 4096 x 4096 sRGB label atlas (bottle / can labels, the lamp shade logo and badge, the chalk wrapper, the jukebox
marquee and title-strip pages, the dart-machine marquee and its 7-segment score window, Deacon's brass plate, the wall signs and
paper: LOW CLEARANCE 10'-6", CASH ONLY, NO SMOKING, HOUSE RULES, league / band flyers, RESTROOMS, SHOT & A BEER) and labels.json
with every label's rectangle:
  "px":  [x0, y0, x1, y1] image pixels (top-left origin),
  "uv_ue": [u0, v0, u1, v1] Unreal UV (top-left origin),
  "uv_blender": [u0, v0, u1, v1] Blender UV (bottom-left origin; the FBX import flips V), what the prop generators use,
  "size_m": the physical size the label is designed for (so the generator keeps its aspect).
Consumers: Tools/blender/divebar/db_lathe_props.py (bottle / can labels), db_lamp.py (shade logo, badge), db_cue_rack.py (chalk
wrapper), db_jukebox.py (marquee, strips), db_dart.py (marquee, score), db_bar.py (Deacon's plate), db_signs.py (signs, flyers);
the materials MI_DB_Label_Atlas / MI_DB_Label_Gloss / MI_DB_Emissive_* sample the atlas through UV0.
Owner: M2-B.
"""

from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = Path(__file__).resolve().parents[2]
FONTS = REPO / "Art" / "Fonts"
OUT = REPO / "Art" / "DiveBar" / "Textures" / "Labels"
ATLAS = 4096


def font(family: str, size: int, variation: str | None = None) -> ImageFont.FreeTypeFont:
	files = {"BebasNeue": "BebasNeue/BebasNeue-Regular.ttf", "Oswald": "Oswald/Oswald[wght].ttf", "Pacifico": "Pacifico/Pacifico-Regular.ttf",
		"Lobster": "Lobster/Lobster-Regular.ttf", "Rye": "Rye/Rye-Regular.ttf", "Kalam": "Kalam/Kalam-Regular.ttf",
		"KalamBold": "Kalam/Kalam-Bold.ttf", "Caveat": "Caveat/Caveat[wght].ttf", "Courier": "CourierPrime/CourierPrime-Regular.ttf",
		"Elite": "SpecialElite/SpecialElite-Regular.ttf", "Marker": "PermanentMarker/PermanentMarker-Regular.ttf"}
	f = ImageFont.truetype(str(FONTS / files[family]), size)
	if variation:
		try:
			f.set_variation_by_name(variation)
		except Exception:  # noqa: BLE001 - static font
			pass
	return f


def text_c(draw: ImageDraw.ImageDraw, xy, s: str, f, fill, spacing: float = 0.0, anchor: str = "mm") -> None:
	"""Centred text with optional letter spacing (tracking, in px)."""
	if spacing <= 0.0:
		draw.text(xy, s, font=f, fill=fill, anchor=anchor)
		return
	widths = [draw.textlength(ch, font=f) for ch in s]
	total = sum(widths) + spacing * (len(s) - 1)
	x = xy[0] - total / 2.0
	for ch, w in zip(s, widths):
		draw.text((x, xy[1]), ch, font=f, fill=fill, anchor="lm")
		x += w + spacing


def fit_font(draw, s: str, family: str, max_w: float, max_h: float, variation: str | None = None) -> ImageFont.FreeTypeFont:
	size = int(max_h)
	while size > 8:
		f = font(family, size, variation)
		box = draw.textbbox((0, 0), s, font=f)
		if box[2] - box[0] <= max_w and box[3] - box[1] <= max_h:
			return f
		size = int(size * 0.94)
	return font(family, 8, variation)


def paper(w: int, h: int, color, seed: int, fibres: float = 0.05) -> Image.Image:
	"""Printed paper: base colour with a little fibre noise (the material adds the age / dirt)."""
	rng = random.Random(seed)
	img = Image.new("RGB", (w, h), color)
	noise = Image.effect_noise((max(8, w // 4), max(8, h // 4)), 30).resize((w, h), Image.BILINEAR)
	img = Image.blend(img, Image.merge("RGB", (noise, noise, noise)), fibres)
	d = ImageDraw.Draw(img)
	for _ in range(int(w * h * 0.00005)):
		x, y = rng.randrange(w), rng.randrange(h)
		d.point((x, y), fill=tuple(max(0, c - 40) for c in color))
	return img


def border(d, w, h, inset, color, width=3, round_r=0):
	if round_r:
		d.rounded_rectangle((inset, inset, w - inset, h - inset), round_r, outline=color, width=width)
	else:
		d.rectangle((inset, inset, w - inset, h - inset), outline=color, width=width)


# --------------------------------------------------------------------------------------------------------------------
# labels (each returns an RGB image at its design pixel size)
# --------------------------------------------------------------------------------------------------------------------


def lbl_ashby(w=900, h=420):
	img = paper(w, h, (232, 222, 196), 1)
	d = ImageDraw.Draw(img)
	border(d, w, h, 14, (40, 30, 25), 4)
	border(d, w, h, 24, (150, 40, 30), 2)
	# the ridge line and a horse barn (no bell: 8.1)
	pts = [(60, 250), (160, 200), (230, 225), (330, 170), (420, 215), (520, 180), (640, 230), (740, 195), (840, 245), (840, 262), (60, 262)]
	d.polygon(pts, fill=(70, 60, 50))
	d.polygon([(560, 262), (560, 225), (600, 200), (640, 225), (640, 262)], fill=(150, 40, 30))
	d.rectangle((590, 238, 610, 262), fill=(232, 222, 196))
	text_c(d, (w / 2, 110), "ASHBY RIDGE", font("Rye", 92), (35, 28, 24))
	text_c(d, (w / 2, 300), "KENTUCKY STRAIGHT BOURBON WHISKEY", font("Oswald", 34, "Bold"), (35, 28, 24), 3)
	text_c(d, (w / 2, 345), "AGED 4 YEARS  -  SMALL LOTS", font("Oswald", 26), (120, 40, 30), 2)
	text_c(d, (w / 2, 382), "750 ML   -   45% ALC/VOL (90 PROOF)", font("Oswald", 20), (60, 50, 45), 2)
	return img


def lbl_korvin(w=900, h=420):
	img = paper(w, h, (240, 240, 236), 2, 0.03)
	d = ImageDraw.Draw(img)
	d.rectangle((0, 250, w, 300), fill=(180, 22, 30))
	text_c(d, (w / 2, 140), "KORVIN", font("BebasNeue", 190), (180, 22, 30), 10)
	text_c(d, (w / 2, 275), "V O D K A", font("Oswald", 36, "Bold"), (245, 240, 235), 4)
	text_c(d, (w / 2, 340), "DISTILLED FIVE TIMES  -  CHARCOAL FILTERED", font("Oswald", 24), (60, 60, 60), 2)
	text_c(d, (w / 2, 380), "1.75 L   -   40% ALC/VOL", font("Oswald", 22), (60, 60, 60), 2)
	return img


def lbl_dockhand(w=900, h=420):
	img = paper(w, h, (26, 22, 20), 3, 0.02)
	d = ImageDraw.Draw(img)
	gold = (200, 160, 80)
	border(d, w, h, 16, gold, 3, 20)
	# sailor silhouette: head, cap, collar
	cx, cy = 150, 210
	d.ellipse((cx - 55, cy - 60, cx + 55, cy + 55), fill=gold)
	d.rectangle((cx - 62, cy - 78, cx + 62, cy - 50), fill=gold)
	d.polygon([(cx - 90, cy + 130), (cx - 40, cy + 50), (cx + 40, cy + 50), (cx + 90, cy + 130)], fill=gold)
	d.ellipse((cx - 50, cy - 55, cx + 50, cy + 50), fill=(26, 22, 20))
	d.ellipse((cx - 44, cy - 49, cx + 44, cy + 44), fill=gold)
	text_c(d, (560, 170), "Dockhand", font("Lobster", 120), gold)
	text_c(d, (560, 290), "SPICED RUM", font("Oswald", 48, "Bold"), (235, 225, 200), 8)
	text_c(d, (560, 350), "CARIBBEAN RUM WITH SPICES  -  35% ALC/VOL", font("Oswald", 20), gold, 2)
	return img


def lbl_wexmoor(w=800, h=420):
	img = paper(w, h, (228, 232, 220), 4)
	d = ImageDraw.Draw(img)
	green = (30, 80, 50)
	d.rectangle((0, 0, w, 70), fill=green)
	d.rectangle((0, h - 60, w, h), fill=green)
	for k in range(9):  # juniper sprig
		a = k * 0.55
		x, y = 400 + 150 * math.cos(a) * 0.6, 250 - 30 * math.sin(a * 2)
		d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(60, 70, 110))
	text_c(d, (w / 2, 150), "WEXMOOR", font("Oswald", 110, "Bold"), green, 12)
	text_c(d, (w / 2, 318), "LONDON DRY GIN", font("Oswald", 40), green, 6)
	text_c(d, (w / 2, h - 30), "47% ALC/VOL   -   750 ML", font("Oswald", 22), (225, 230, 215), 3)
	text_c(d, (w / 2, 35), "DISTILLED WITH TEN BOTANICALS", font("Oswald", 22), (225, 230, 215), 3)
	return img


def lbl_el_tordo(w=800, h=420):
	img = paper(w, h, (245, 240, 225), 5)
	d = ImageDraw.Draw(img)
	agave = (60, 110, 90)
	for k in range(9):
		a = math.radians(-70 + k * 17.5)
		d.polygon([(400 - 18, 330), (400 + 18, 330), (400 + 230 * math.sin(a), 330 - 200 * math.cos(a))], fill=agave)
	d.ellipse((520, 90, 600, 140), fill=(30, 30, 35))       # the bird
	d.polygon([(590, 110), (640, 100), (596, 124)], fill=(220, 150, 40))
	d.polygon([(530, 110), (470, 80), (540, 128)], fill=(30, 30, 35))
	text_c(d, (w / 2, 60), "EL TORDO", font("Rye", 80), (120, 40, 30))
	text_c(d, (w / 2, 368), "TEQUILA BLANCO  -  100% DE AGAVE", font("Oswald", 30, "Bold"), (40, 40, 40), 3)
	text_c(d, (w / 2, 402), "40% ALC/VOL", font("Oswald", 20), (40, 40, 40), 3)
	return img


def lbl_ember(w=800, h=420):
	img = paper(w, h, (150, 20, 18), 6, 0.03)
	d = ImageDraw.Draw(img)
	rng = random.Random(6)
	for k in range(12):
		x = 60 + k * 62 + rng.randint(-10, 10)
		d.polygon([(x - 26, 420), (x + 26, 420), (x + rng.randint(-15, 15), 300 - rng.randint(0, 60))], fill=(235, 120, 30))
		d.polygon([(x - 14, 420), (x + 14, 420), (x + rng.randint(-8, 8), 350 - rng.randint(0, 40))], fill=(250, 200, 60))
	text_c(d, (w / 2, 130), "EMBER", font("BebasNeue", 200), (250, 235, 200), 14)
	text_c(d, (w / 2, 258), "CINNAMON WHISKY", font("Oswald", 42, "Bold"), (250, 220, 170), 6)
	return img


def lbl_frostmint(w=800, h=420):
	img = paper(w, h, (236, 242, 236), 7, 0.02)
	d = ImageDraw.Draw(img)
	green = (20, 120, 70)
	for k in range(0, w, 60):
		d.polygon([(k, 0), (k + 30, 0), (k + 60, 60), (k + 30, 60)], fill=green)
	text_c(d, (w / 2, 200), "Frostmint", font("Pacifico", 120), green)
	text_c(d, (w / 2, 330), "PEPPERMINT SCHNAPPS", font("Oswald", 40, "Bold"), (40, 60, 50), 5)
	text_c(d, (w / 2, 380), "30% ALC/VOL  -  750 ML", font("Oswald", 22), (40, 60, 50), 3)
	return img


def beaver(d, cx, cy, s, fill):
	"""The Old Castor emblem: a beaver in profile on a river line (simple shapes)."""
	d.ellipse((cx - 60 * s, cy - 30 * s, cx + 40 * s, cy + 30 * s), fill=fill)       # body
	d.ellipse((cx + 20 * s, cy - 42 * s, cx + 70 * s, cy + 2 * s), fill=fill)       # head
	d.polygon([(cx - 55 * s, cy + 5 * s), (cx - 120 * s, cy + 25 * s), (cx - 115 * s, cy + 45 * s), (cx - 50 * s, cy + 25 * s)], fill=fill)
	d.ellipse((cx + 50 * s, cy - 30 * s, cx + 58 * s, cy - 22 * s), fill=(240, 230, 210))
	for k in range(3):
		y = cy + (45 + k * 14) * s
		d.arc((cx - 130 * s, y - 10 * s, cx + 130 * s, y + 10 * s), 200, 340, fill=fill, width=max(2, int(4 * s)))


def lbl_old_castor(w=700, h=380):
	img = paper(w, h, (238, 228, 205), 8)
	d = ImageDraw.Draw(img)
	red = (170, 30, 30)
	d.ellipse((30, 20, w - 30, h - 20), fill=red)
	d.ellipse((50, 40, w - 50, h - 40), outline=(238, 228, 205), width=4)
	text_c(d, (w / 2, 150), "Old Castor", font("Pacifico", 96), (245, 238, 220))
	text_c(d, (w / 2, 250), "L A G E R", font("Oswald", 44, "Bold"), (245, 238, 220), 6)
	text_c(d, (w / 2, 300), "BREWED IN PORT CASTOR SINCE 1889", font("Oswald", 20), (245, 238, 220), 2)
	return img


def lbl_hollenbeck(w=700, h=380):
	img = paper(w, h, (205, 212, 222), 9, 0.02)
	d = ImageDraw.Draw(img)
	blue = (25, 60, 140)
	d.rectangle((0, 250, w, 380), fill=blue)
	for k in range(6):
		d.line((0, 250 + k * 22, w, 250 + k * 22), fill=(60, 100, 180), width=2)
	text_c(d, (w / 2, 130), "Hollenbeck", font("Lobster", 120), blue)
	text_c(d, (w / 2, 300), "L I G H T", font("BebasNeue", 80), (230, 235, 245), 6)
	return img


def lbl_lantern_flats(w=1000, h=560):
	img = paper(w, h, (20, 110, 110), 10, 0.02)
	d = ImageDraw.Draw(img)
	orange = (240, 140, 40)
	cx = 250
	d.rectangle((cx - 50, 170, cx + 50, 330), outline=orange, width=10)       # lantern
	d.polygon([(cx - 70, 170), (cx + 70, 170), (cx, 110)], fill=orange)
	d.arc((cx - 30, 60, cx + 30, 120), 180, 360, fill=orange, width=8)
	d.ellipse((cx - 25, 215, cx + 25, 285), fill=(255, 210, 100))
	d.rectangle((cx - 70, 330, cx + 70, 350), fill=orange)
	text_c(d, (650, 200), "LANTERN", font("BebasNeue", 150), (245, 240, 225), 6)
	text_c(d, (650, 330), "FLATS", font("BebasNeue", 150), orange, 6)
	text_c(d, (650, 440), "INDIA PALE ALE  -  6.8% ABV  -  12 FL OZ", font("Oswald", 28, "Bold"), (245, 240, 225), 3)
	return img


def lbl_shade_logo(w=600, h=600):
	img = Image.new("RGB", (w, h), (18, 58, 36))     # the enamel green of MI_DB_Enamel_Green (sRGB)
	d = ImageDraw.Draw(img)
	cream = (235, 222, 190)
	d.ellipse((40, 40, w - 40, h - 40), outline=cream, width=10)
	d.ellipse((62, 62, w - 62, h - 62), outline=(170, 30, 30), width=6)
	beaver(d, w / 2 + 10, 190, 1.0, cream)
	text_c(d, (w / 2, 350), "Old Castor", font("Pacifico", 92), cream)
	text_c(d, (w / 2, 450), "LAGER", font("Oswald", 52, "Bold"), (190, 40, 36), 10)
	return img


def lbl_lamp_badge(w=900, h=320):
	img = Image.new("RGB", (w, h), (250, 236, 200))  # backlit cream (emissive: the texture multiplies the luminance)
	d = ImageDraw.Draw(img)
	d.rounded_rectangle((12, 12, w - 12, h - 12), 60, outline=(150, 25, 25), width=14)
	text_c(d, (w / 2, 130), "Old Castor", font("Pacifico", 110), (150, 25, 25))
	text_c(d, (w / 2, 245), "LAGER BEER  -  PORT CASTOR, OHIO", font("Oswald", 36, "Bold"), (60, 40, 30), 4)
	return img


def lbl_rail_rat(w=512, h=512):
	img = paper(w, h, (30, 70, 160), 11, 0.03)
	d = ImageDraw.Draw(img)
	white = (235, 238, 245)
	# rat silhouette
	d.ellipse((140, 200, 330, 300), fill=white)
	d.ellipse((300, 210, 380, 270), fill=white)
	d.polygon([(372, 230), (410, 244), (372, 256)], fill=white)
	d.ellipse((310, 190, 345, 225), fill=white)
	d.line([(140, 260), (90, 290), (60, 250), (40, 280)], fill=white, width=8)
	d.ellipse((352, 228, 362, 238), fill=(30, 70, 160))
	text_c(d, (w / 2, 110), "RAIL RAT", font("BebasNeue", 120), white, 6)
	text_c(d, (w / 2, 385), "BILLIARD CHALK", font("Oswald", 40, "Bold"), white, 4)
	text_c(d, (w / 2, 440), "BAR GRADE  -  BLUE", font("Oswald", 26), (200, 210, 235), 3)
	return img


def lbl_jukebox_marquee(w=1200, h=300):
	img = Image.new("RGB", (w, h), (12, 10, 14))
	d = ImageDraw.Draw(img)
	pink = (255, 90, 170)
	glow = Image.new("RGB", (w, h), (0, 0, 0))
	gd = ImageDraw.Draw(glow)
	text_c(gd, (w / 2, 120), "Marquee", font("Pacifico", 150), pink)
	glow = glow.filter(ImageFilter.GaussianBlur(10))
	img = Image.blend(img, glow, 0.6)
	d = ImageDraw.Draw(img)
	text_c(d, (w / 2, 120), "Marquee", font("Pacifico", 150), (255, 170, 220))
	text_c(d, (w / 2, 255), "STARLITE  CD-100", font("Oswald", 46, "Bold"), (210, 220, 255), 10)
	return img


BANDS = ["The Rust Belt Saints", "Delia Cruz & the Late Shift", "Highway 9 Outlaws", "Nine Ball Nancy", "The Dry County Band",
	"Canal Street Ghosts", "Mercy Lane", "The Keel River Boys", "Static Parish", "Lou & the Lake Effect", "The Harbor Street Kings",
	"Johnny Vance Trio", "Ore Boat", "The Tuesday Leaguers", "Marla Keene", "Blackwater Bend", "The Slow Burners", "Dixie Ferro",
	"Second Shift", "The Lakeshore Hounds"]
TITLES = ["Last Call in Port Castor", "Bank Shot Blues", "Shift Whistle", "Cold Lager Morning", "Neon on the Water",
	"Eight Ball Heart", "Lake Effect Love", "Rolling Toward the Jukebox", "Quarters on the Rail", "Low Bridge", "Canal Street Rain",
	"One More for the Road", "Steel Town Sunday", "Corner Pocket", "Midnight Rack", "Ohio Moon", "Tavern Light", "Break and Run",
	"Big Lou's Waltz", "Hollow Point Road", "Gravel and Gold", "Friday Money Night", "Ice in the Harbor", "Short Cue Shuffle",
	"Ain't Leaving Yet", "Chalk Dust Heart", "Loser Racks", "Night Train to Toledo", "Barstool Saint", "Brass Rail Rag",
	"Old Castor Waltz", "The Long Rail", "Jukebox Saturday", "Down at the Bridge", "Red Neon Sign", "Keep the Change",
	"Scratch Shot", "River Mill Girl", "Nickel Beer Days", "Deacon's Tune"]


def lbl_title_page(page: int, w=1024, h=1024):
	"""A jukebox title-strip page: 10 rows x 2 columns of strips (song / artist), typed; one hand-written strip (S7)."""
	rng = random.Random(100 + page)
	img = Image.new("RGB", (w, h), (238, 232, 214))
	d = ImageDraw.Draw(img)
	sw, sh = w // 2, h // 10
	f_song = font("Courier", 30)
	f_band = font("Oswald", 30, "Bold")
	colors = [(190, 40, 40), (40, 70, 160), (200, 130, 30), (30, 120, 70)]
	for col in range(2):
		for row in range(10):
			k = page * 20 + col * 10 + row
			x0, y0 = col * sw, row * sh
			c = colors[(k // 3) % len(colors)]
			d.rectangle((x0 + 4, y0 + 4, x0 + sw - 4, y0 + sh - 4), fill=(242, 236, 220), outline=c, width=3)
			d.line((x0 + 10, y0 + sh // 2, x0 + sw - 10, y0 + sh // 2), fill=c, width=2)
			num = f"{100 + page * 20 + col * 10 + row:03d}"
			d.text((x0 + 10, y0 + sh // 2), num, font=font("Oswald", 17), fill=(90, 90, 90), anchor="lm")
			if page == 0 and col == 1 and row == 4:
				text_c(d, (x0 + sw / 2, y0 + sh * 0.3), "DEACON'S PICK", font("Caveat", 44, "Bold"), (20, 20, 90))
				text_c(d, (x0 + sw / 2, y0 + sh * 0.75), "play it loud - D.", font("Caveat", 36), (20, 20, 90))
				continue
			title = TITLES[(k * 7 + page) % len(TITLES)]
			band = BANDS[(k * 3 + page * 5) % len(BANDS)]
			jitter = rng.uniform(-2, 2)
			text_c(d, (x0 + sw / 2 + 14 + jitter, y0 + sh * 0.28), title.upper()[:24], f_song, (30, 30, 30))
			text_c(d, (x0 + sw / 2 + 14, y0 + sh * 0.75), band.upper()[:26], f_band, c)
	return img


def lbl_dart_marquee(w=1000, h=300):
	img = Image.new("RGB", (w, h), (15, 12, 12))
	d = ImageDraw.Draw(img)
	red = (230, 40, 30)
	# hawk: wings as two triangles, head
	d.polygon([(120, 170), (40, 90), (200, 150)], fill=red)
	d.polygon([(200, 170), (300, 80), (210, 150)], fill=red)
	d.ellipse((180, 120, 230, 170), fill=(240, 235, 225))
	d.polygon([(226, 140), (252, 150), (226, 158)], fill=(240, 180, 40))
	text_c(d, (620, 120), "HAWKLINE", font("BebasNeue", 170), (245, 240, 235), 8)
	text_c(d, (620, 245), "3 6 0   ELECTRONIC DARTS", font("Oswald", 40, "Bold"), red, 6)
	return img


SEGMENTS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg",
	"9": "abcdfg", " ": ""}


def seven_segment(d, x: float, y: float, w: float, h: float, ch: str, on, off, t: float) -> None:
	"""One 7-segment digit (slanted 8 %), lit segments `on`, the unlit ones faintly visible (`off`, real LED displays ghost)."""
	def seg(x0, y0, x1, y1):
		sl = 0.08 * h

		def p(px, py):
			return (px + sl * (1.0 - (py - y) / h), py)
		if abs(x1 - x0) > abs(y1 - y0):   # horizontal
			return [p(x0 + t, y0), p(x1 - t, y0), p(x1, y0 + t / 2), p(x1 - t, y0 + t), p(x0 + t, y0 + t), p(x0, y0 + t / 2)]
		return [p(x0, y0 + t), p(x0 + t / 2, y0), p(x0 + t, y0 + t), p(x0 + t, y1 - t), p(x0 + t / 2, y1), p(x0, y1 - t)]
	hm = y + h / 2 - t / 2
	shapes = {"a": seg(x, y, x + w, y), "g": seg(x, hm, x + w, hm), "d": seg(x, y + h - t, x + w, y + h - t),
		"f": seg(x - t / 2, y, x - t / 2, y + h / 2), "e": seg(x - t / 2, y + h / 2, x - t / 2, y + h),
		"b": seg(x + w - t / 2, y, x + w - t / 2, y + h / 2), "c": seg(x + w - t / 2, y + h / 2, x + w - t / 2, y + h)}
	lit = SEGMENTS.get(ch, "")
	for name, poly in shapes.items():
		d.polygon(poly, fill=on if name in lit else off)


def lbl_dart_score(w=1000, h=210):
	"""The Hawkline's red LED score window (emissive through MI_DB_Emissive_DartScore): 501 for player 1, the credit counter."""
	img = Image.new("RGB", (w, h), (0, 0, 0))
	d = ImageDraw.Draw(img)
	on, off = (255, 26, 12), (16, 2, 1)
	for k, ch in enumerate(" 501"):
		seven_segment(d, 70 + k * 150, 28, 96, 154, ch, on, off, 20)
	for k, ch in enumerate("00"):
		seven_segment(d, 740 + k * 100, 70, 58, 96, ch, on, off, 13)
	d.text((720, 30), "CREDIT", font=font("Oswald", 30, "Bold"), fill=(150, 16, 8), anchor="lm")
	d.text((60, 196), "PLAYER 1", font=font("Oswald", 24, "Bold"), fill=(120, 12, 6), anchor="lb")
	return img.filter(ImageFilter.GaussianBlur(1.2))


def lbl_deacon_plate(w=700, h=180):
	img = Image.new("RGB", (w, h), (176, 140, 70))     # brass (the material adds the metal response)
	d = ImageDraw.Draw(img)
	d.rounded_rectangle((8, 8, w - 8, h - 8), 16, outline=(90, 65, 25), width=5)
	for sx in (30, w - 30):
		d.ellipse((sx - 10, h / 2 - 10, sx + 10, h / 2 + 10), fill=(110, 85, 40))
	text_c(d, (w / 2, 62), "RESERVED - DEACON", font("Elite", 58), (45, 30, 12))
	text_c(d, (w / 2, 128), "SINCE 1979", font("Elite", 46), (45, 30, 12))
	return img


# ---- wall signs and paper (db_signs.py; S1 / S5 / S19 / S20, M15 text-only until the DB-5 art) -------------------------------------


def weather(img: Image.Image, seed: int, fade: float = 0.12, dirt: float = 0.5, yellow: float = 0.0, scratches: int = 0) -> Image.Image:
	"""Decades on a wall: faded ink, nicotine yellowing, fly-specks and grime smudges, fine scratches (deterministic)."""
	rng = random.Random(seed)
	w, h = img.size
	out = Image.blend(img, Image.new("RGB", (w, h), (236, 228, 205)), fade)
	if yellow > 0.0:
		out = Image.blend(out, Image.new("RGB", (w, h), (196, 160, 90)), yellow)
	d = ImageDraw.Draw(out)
	for _ in range(int(w * h * 0.00012 * dirt)):
		x, y = rng.randrange(w), rng.randrange(h)
		r = rng.choice((1, 1, 1, 2, 3))
		c = rng.randint(40, 90)
		d.ellipse((x - r, y - r, x + r, y + r), fill=(c, c - 8, c - 16))
	smudge = Image.new("L", (w, h), 0)
	sd = ImageDraw.Draw(smudge)
	for _ in range(int(6 * dirt)):
		x, y = rng.randrange(w), rng.randrange(h)
		r = rng.randint(w // 14, w // 5)
		sd.ellipse((x - r, y - r * 0.6, x + r, y + r * 0.6), fill=rng.randint(20, 60))
	smudge = smudge.filter(ImageFilter.GaussianBlur(w / 30))
	out = Image.composite(Image.new("RGB", (w, h), (70, 58, 40)), out, smudge)
	d = ImageDraw.Draw(out)
	for _ in range(scratches):
		x, y = rng.randrange(w), rng.randrange(h)
		a = rng.uniform(0, math.pi)
		ln = rng.randint(w // 20, w // 5)
		d.line((x, y, x + ln * math.cos(a), y + ln * math.sin(a)), fill=(215, 210, 195), width=1)
	return out


def sign_low_clearance(w=1000, h=500):
	"""S1: the LOW CLEARANCE 10'-6" road sign (MUTCD-style yellow warning sign; generic public design) from the Canal Street underpass."""
	img = Image.new("RGB", (w, h), (238, 186, 22))
	d = ImageDraw.Draw(img)
	d.rounded_rectangle((14, 14, w - 14, h - 14), 46, outline=(18, 16, 14), width=18)
	text_c(d, (w / 2, 150), "LOW CLEARANCE", fit_font(d, "LOW CLEARANCE", "Oswald", w - 150, 150, "Bold"), (18, 16, 14))
	text_c(d, (w / 2, 345), "10'-6\"", font("Oswald", 190, "Bold"), (18, 16, 14), 6)
	for x, y in ((60, 60), (w - 60, 60), (60, h - 60), (w - 60, h - 60)):
		d.ellipse((x - 12, y - 12, x + 12, y + 12), fill=(120, 110, 90))
	return weather(img, 21, fade=0.18, dirt=1.4, yellow=0.08, scratches=60)


def sign_cash_only(w=640, h=320):
	"""S19: engraved two-ply plastic sign by the ATM."""
	img = Image.new("RGB", (w, h), (236, 234, 226))
	d = ImageDraw.Draw(img)
	d.rounded_rectangle((10, 10, w - 10, h - 10), 18, outline=(170, 24, 24), width=10)
	text_c(d, (w / 2, 118), "CASH ONLY", fit_font(d, "CASH ONLY", "Oswald", w - 90, 140, "Bold"), (170, 24, 24))
	text_c(d, (w / 2, 238), "ATM INSIDE  -  NO CHECKS", font("Oswald", 48, "Bold"), (30, 30, 30), 3)
	return weather(img, 22, fade=0.05, dirt=0.8, yellow=0.12, scratches=20)


def sign_no_smoking(w=480, h=560):
	"""S5: the no-smoking notice by the door (the generic circle-and-slash symbol; the law it cites is the real Ohio act)."""
	img = Image.new("RGB", (w, h), (240, 238, 232))
	d = ImageDraw.Draw(img)
	d.rectangle((8, 8, w - 8, h - 8), outline=(20, 20, 20), width=6)
	cx, cy, r = w / 2, 190, 150
	d.rectangle((cx - 95, cy - 12, cx + 70, cy + 14), fill=(30, 30, 30))           # a generic cigarette pictogram
	d.rectangle((cx + 70, cy - 12, cx + 95, cy + 14), fill=(120, 110, 100))
	for k in range(3):
		d.arc((cx + 70 + k * 6, cy - 70 - k * 18, cx + 110 + k * 6, cy - 20 - k * 18), 90, 270, fill=(90, 90, 90), width=5)
	d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(200, 20, 20), width=26)
	d.line((cx - r * 0.7, cy - r * 0.7, cx + r * 0.7, cy + r * 0.7), fill=(200, 20, 20), width=26)
	text_c(d, (w / 2, 400), "NO SMOKING", fit_font(d, "NO SMOKING", "Oswald", w - 60, 84, "Bold"), (20, 20, 20))
	text_c(d, (w / 2, 468), "SMOKE-FREE WORKPLACE ACT", font("Oswald", 30, "Bold"), (20, 20, 20), 2)
	text_c(d, (w / 2, 510), "OHIO REVISED CODE CHAPTER 3794", font("Oswald", 24), (60, 60, 60), 2)
	return weather(img, 23, fade=0.06, dirt=0.6, yellow=0.22, scratches=10)


def sign_house_rules(w=640, h=830):
	"""S20: the house rules, hand-lettered with a marker on a letter sheet, taped to the paneling next to the chalkboard."""
	img = paper(w, h, (238, 234, 222), 24, 0.04)
	d = ImageDraw.Draw(img)
	rng = random.Random(24)
	text_c(d, (w / 2 + 6, 112), "HOUSE RULES", font("Marker", 64), (18, 18, 60))
	d.line((110, 162, w - 100, 158), fill=(18, 18, 60), width=6)
	lines = ["NO JUMP SHOTS", "DON'T SIT ON", "THE TABLE!", "WINNER STAYS", "QUARTERS ON", "THE RAIL = YOUR", "SPOT", "CALL YOUR", "POCKET"]
	y = 220
	for k, s in enumerate(lines):
		indent = 0 if s[0].isalpha() and k not in (2, 4, 5, 6, 8) else 30
		d.text((70 + indent + rng.uniform(-6, 6), y), s, font=font("Marker", 52), fill=(180, 20, 20) if k < 3 else (20, 20, 20), anchor="lm")
		y += 62 if k not in (1, 3, 5, 7) else 58
	d.text((w - 200, h - 70), "- Terri", font=font("Kalam", 50), fill=(20, 20, 20), anchor="lm")
	return weather(img, 25, fade=0.04, dirt=0.9, yellow=0.25)


def flyer_league(w=560, h=860):
	"""The CV8L league-night flyer (fictional league and teams, 8.1)."""
	img = paper(w, h, (244, 220, 60), 26, 0.03)
	d = ImageDraw.Draw(img)
	d.rectangle((0, 0, w, 150), fill=(20, 20, 20))
	text_c(d, (w / 2, 58), "CASTOR VALLEY", font("BebasNeue", 76), (244, 220, 60), 4)
	text_c(d, (w / 2, 118), "8 - BALL  LEAGUE", font("Oswald", 40, "Bold"), (244, 244, 244), 5)
	d.ellipse((w / 2 - 110, 190, w / 2 + 110, 410), fill=(15, 15, 15))
	d.ellipse((w / 2 - 55, 245, w / 2 + 55, 355), fill=(244, 244, 240))
	text_c(d, (w / 2, 300), "8", font("Oswald", 96, "Bold"), (15, 15, 15))
	text_c(d, (w / 2, 480), "TUESDAY", font("BebasNeue", 120), (15, 15, 15), 4)
	text_c(d, (w / 2, 575), "LEAGUE NIGHT  7:30", font("Oswald", 46, "Bold"), (170, 20, 20), 2)
	text_c(d, (w / 2, 660), "LOW BRIDGE BOMBERS", font("Oswald", 40, "Bold"), (15, 15, 15), 2)
	text_c(d, (w / 2, 710), "vs", font("Kalam", 40), (15, 15, 15))
	text_c(d, (w / 2, 760), "CANAL STREET SHARKS", font("Oswald", 40, "Bold"), (15, 15, 15), 2)
	text_c(d, (w / 2, 822), "$5 PITCHERS - OLD CASTOR", font("Oswald", 28), (60, 50, 20), 2)
	return weather(img, 27, fade=0.18, dirt=0.7, yellow=0.1)


def flyer_band(w=560, h=860):
	"""A band flyer for Saturday (fictional band, 8.1), photocopied, half faded."""
	img = paper(w, h, (236, 236, 230), 28, 0.05)
	d = ImageDraw.Draw(img)
	text_c(d, (w / 2, 90), "LIVE", font("Marker", 110), (20, 20, 20))
	text_c(d, (w / 2, 190), "SATURDAY", font("BebasNeue", 96), (20, 20, 20), 6)
	d.rectangle((40, 250, w - 40, 560), fill=(40, 40, 40))
	for k in range(9):    # a photocopied crowd: rough blobs
		x = 70 + k * 52
		d.ellipse((x, 420 - (k % 3) * 18, x + 46, 470 - (k % 3) * 18), fill=(120, 120, 120))
		d.rectangle((x + 4, 460 - (k % 3) * 18, x + 42, 560), fill=(95, 95, 95))
	text_c(d, (w / 2, 330), "THE RUST BELT", font("BebasNeue", 78), (236, 236, 230), 4)
	text_c(d, (w / 2, 395), "SAINTS", font("BebasNeue", 78), (236, 236, 230), 8)
	text_c(d, (w / 2, 630), "NO COVER  -  9 PM", font("Oswald", 52, "Bold"), (20, 20, 20), 3)
	text_c(d, (w / 2, 700), "w/ Nine Ball Nancy", font("Kalam", 44), (20, 20, 20))
	text_c(d, (w / 2, 772), "THE LOW BRIDGE TAVERN", font("Oswald", 30, "Bold"), (20, 20, 20), 3)
	return weather(img, 29, fade=0.3, dirt=0.8, yellow=0.14)


def sign_restrooms(w=720, h=200):
	"""Engraved black plastic 'RESTROOMS' arrow sign (points toward the corridor)."""
	img = Image.new("RGB", (w, h), (22, 22, 24))
	d = ImageDraw.Draw(img)
	d.rounded_rectangle((8, 8, w - 8, h - 8), 16, outline=(200, 196, 186), width=5)
	# on the left wall facing +Y the corridor (+X) is to the reader's right
	d.polygon([(w - 40, h / 2), (w - 120, h / 2 - 50), (w - 120, h / 2 - 20), (w - 170, h / 2 - 20), (w - 170, h / 2 + 20), (w - 120, h / 2 + 20),
		(w - 120, h / 2 + 50)], fill=(220, 216, 206))
	text_c(d, (w / 2 - 85, h / 2 + 4), "RESTROOMS", font("Oswald", 84, "Bold"), (220, 216, 206), 5)
	return weather(img, 30, fade=0.0, dirt=0.4, scratches=30)


def sign_shot_and_beer(w=640, h=440):
	"""S19 prices: a marker sign on a torn piece of beer-case cardboard."""
	img = Image.new("RGB", (w, h), (178, 140, 92))
	noise = Image.effect_noise((w // 4, h // 4), 18).resize((w, h), Image.BILINEAR)
	img = Image.blend(img, Image.merge("RGB", (noise, noise, noise)), 0.06)
	d = ImageDraw.Draw(img)
	d.text((50, 110), "SHOT & A BEER", font=fit_font(d, "SHOT & A BEER", "Marker", w - 100, 90), fill=(20, 20, 20), anchor="lm")
	d.text((180, 250), "$6", font=font("Marker", 170), fill=(170, 20, 20), anchor="lm")
	d.text((380, 380), "domestic only", font=font("Kalam", 38), fill=(30, 30, 30), anchor="lm")
	return weather(img, 31, fade=0.0, dirt=1.0, yellow=0.05)


LABELS = [
	# name, renderer, physical size (m) the label is made for
	("bottle_ashby_ridge", lbl_ashby, (0.180, 0.084)),
	("bottle_korvin", lbl_korvin, (0.200, 0.093)),
	("bottle_dockhand", lbl_dockhand, (0.180, 0.084)),
	("bottle_wexmoor", lbl_wexmoor, (0.160, 0.084)),
	("bottle_el_tordo", lbl_el_tordo, (0.160, 0.084)),
	("bottle_ember", lbl_ember, (0.150, 0.079)),
	("bottle_frostmint", lbl_frostmint, (0.150, 0.079)),
	("bottle_old_castor", lbl_old_castor, (0.090, 0.049)),
	("bottle_hollenbeck", lbl_hollenbeck, (0.090, 0.049)),
	("can_lantern_flats", lbl_lantern_flats, (0.207, 0.116)),
	("shade_old_castor", lbl_shade_logo, (0.140, 0.140)),
	("lamp_badge", lbl_lamp_badge, (0.300, 0.107)),
	("chalk_rail_rat", lbl_rail_rat, (0.022, 0.022)),
	("jukebox_marquee", lbl_jukebox_marquee, (0.600, 0.150)),
	("jukebox_strips_0", lambda: lbl_title_page(0), (0.400, 0.400)),
	("jukebox_strips_1", lambda: lbl_title_page(1), (0.400, 0.400)),
	("dart_marquee", lbl_dart_marquee, (0.500, 0.150)),
	("deacon_plate", lbl_deacon_plate, (0.100, 0.026)),
	("dart_score", lbl_dart_score, (0.340, 0.070)),
	("sign_low_clearance", sign_low_clearance, (0.914, 0.457)),
	("sign_cash_only", sign_cash_only, (0.300, 0.150)),
	("sign_no_smoking", sign_no_smoking, (0.203, 0.254)),
	("sign_house_rules", sign_house_rules, (0.216, 0.279)),
	("flyer_league", flyer_league, (0.279, 0.432)),
	("flyer_band", flyer_band, (0.279, 0.432)),
	("sign_restrooms", sign_restrooms, (0.360, 0.100)),
	("sign_shot_and_beer", sign_shot_and_beer, (0.340, 0.230)),
]


def pack(images: list[tuple[str, Image.Image]], size: int, pad: int = 8) -> dict[str, tuple[int, int, int, int]]:
	"""Shelf packing, tallest first. Returns name -> (x0, y0, x1, y1)."""
	order = sorted(images, key=lambda t: -t[1].height)
	x = y = shelf = 0
	rects = {}
	for name, img in order:
		w, h = img.width, img.height
		if x + w + pad > size:
			x, y = 0, y + shelf + pad
			shelf = 0
		if y + h > size:
			raise RuntimeError(f"atlas full at {name}")
		rects[name] = (x, y, x + w, y + h)
		x += w + pad
		shelf = max(shelf, h)
	return rects


def main() -> int:
	images = [(name, fn()) for name, fn, _ in LABELS]
	sizes = {name: size for name, _, size in LABELS}
	rects = pack(images, ATLAS)
	atlas = Image.new("RGB", (ATLAS, ATLAS), (128, 128, 128))
	for name, img in images:
		x0, y0, x1, y1 = rects[name]
		# 4 px bleed of the edge colours so mips / bilinear never mix in the neighbour
		bled = img.resize((img.width + 8, img.height + 8), Image.NEAREST)
		atlas.paste(bled, (x0 - 4 if x0 >= 4 else x0, y0 - 4 if y0 >= 4 else y0))
		atlas.paste(img, (x0, y0))
	OUT.mkdir(parents=True, exist_ok=True)
	atlas.save(OUT / "T_DB_Labels_BC.png", optimize=True)
	meta = {"atlas": "Art/DiveBar/Textures/Labels/T_DB_Labels_BC.png", "size_px": ATLAS, "owner": "M2-B", "labels": {}}
	for name, _ in images:
		x0, y0, x1, y1 = rects[name]
		inset = 1.5 / ATLAS
		u0, u1 = x0 / ATLAS + inset, x1 / ATLAS - inset
		v0, v1 = y0 / ATLAS + inset, y1 / ATLAS - inset
		meta["labels"][name] = {"px": [x0, y0, x1, y1], "uv_ue": [round(u0, 6), round(v0, 6), round(u1, 6), round(v1, 6)],
			"uv_blender": [round(u0, 6), round(1.0 - v1, 6), round(u1, 6), round(1.0 - v0, 6)], "size_m": list(sizes[name])}
	(OUT / "labels.json").write_text(json.dumps(meta, indent=1, sort_keys=True), encoding="utf-8")
	print(f"[text_textures] {len(images)} labels -> {OUT / 'T_DB_Labels_BC.png'}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
