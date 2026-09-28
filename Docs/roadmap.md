# Roadmap (Arbeitsplan)

Stand: 28.09.2026. Der Nutzer ist tagsüber in der Schule und schaut ab ca. 18:00 Uhr auf die Ergebnisse.
Claude arbeitet autonom durch alle Limit-Fenster (Wiederaufnahme per Zeitplan-Check alle ~30 min).

## Fenster 1 (bis ca. 13:10)
- [ ] Unreal-Welle A fertig: UE-1 Tisch-Meshes, UE-2 Kugeln & Playback, UE-5a Input & Stoß, UE-6a Simulation (fertig, Review läuft), UE-6b Match-Ablauf – jeweils mit Review
- [ ] Runde 3 / WP-10: Validierung gegen Messwerte + pooltool, Taschenkanten-Überlappungen, 15-Kugel-Break-Performance
- [ ] Merge + Tests + Push, danach sofort Welle B starten

## Fenster 2 (ca. 13:10–18:00)
- [ ] Unreal-Welle B: UE-3 Materialien & Shader (Kugeln, Tuch, Holz), UE-4 Queue, UE-5b Spielfigur/Kamera, UE-7 Overlay/Replay, UE-8 Testraum/Grafik-Settings/M1-Screenshots
- [ ] Integration **M1 = erste spielbare Version**: 9-Ball-Training + Hot-Seat im Testraum
- [ ] **Spielbarer Build** (gepackte .exe) + kurze Startanleitung, M1-Screenshots
- [ ] Danach: Planung Vertical Slice „Dive Bar" (Raum-Layout, Licht, Props-Liste, Material-Pipeline), Sound-Plan

## Fenster 3 (ab ca. 18:00) – Agents arbeiten durch, unabhängig vom Playtest
- [ ] Vertical Slice Dive Bar: Raum-Layout, Modellierung (Blender-Pipeline), eigene Materialien, Licht inkl. Neon; ggf. Meshy/Higgsfield-Tests
- [ ] Sound: Kugel-Klicks, Banden, Taschen, Bar-Atmo – gesteuert von den Physik-Events
- [ ] KI-Gegner: Stoßplanung über den Simulator + Skill-Profile aus dem Human-Factors-Modell
- [ ] UI/UX: Hauptmenü-Szene, Settings-Menü mit Grafik-Presets, Mockups
- [ ] Sobald der Nutzer M1 gespielt hat: Feedback (Stoß-Gefühl, Kamera, Steuerung) parallel einarbeiten

## Später
- Körper & Hände (MetaHuman – braucht einen Schritt vom Nutzer), Karriere, KI-Gegner, weitere Spielarten/Venues, Steam-Store-Page, Trailer (erst nach Freigabe durch den Nutzer)
