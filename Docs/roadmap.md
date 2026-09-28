# Roadmap (Arbeitsplan)

Stand: 28.09.2026. Der Nutzer ist tagsüber in der Schule und schaut ab ca. 18:00 Uhr auf die Ergebnisse.
Claude arbeitet autonom durch alle Limit-Fenster (der PC läuft durch, die Fortsetzung nach einem Limit ist beim Nutzer automatisiert).

## Fenster 1 (bis ca. 13:10)
- [x] Unreal-Welle A fertig: UE-1 Tisch-Meshes, UE-2 Kugeln & Playback, UE-5a Input & Stoß, UE-6a Simulation (fertig, Review läuft), UE-6b Match-Ablauf – jeweils mit Review
- [x] Runde 3 / WP-10 (gemergt; 7 Kalibrier-Abweichungen bewusst offen, O-18..O-22): Validierung gegen Messwerte + pooltool, Taschenkanten-Überlappungen, 15-Kugel-Break-Performance
- [x] Merge + Tests + Push (100/100 UE-Tests, 896/896 Core-Tests), Welle B gestartet

## Fenster 2 (ca. 13:10–18:00)
- [x] Unreal-Welle B: UE-3 Materialien & Shader (Kugeln, Tuch, Holz), UE-4 Queue, UE-5b Spielfigur/Kamera, UE-7 Overlay/Replay, UE-8 Testraum/Grafik-Settings/M1-Screenshots
- [x] Integration **M1 = erste spielbare Version**: 9-Ball-Training + Hot-Seat im Testraum
- [x] **Spielbarer Build** (gepackte .exe) + kurze Startanleitung, M1-Screenshots
- [x] Danach: Planung Vertical Slice „Dive Bar" (Raum-Layout, Licht, Props-Liste, Material-Pipeline), Sound-Plan

## Fenster 3 (ab ca. 18:00) – Agents arbeiten durch, unabhängig vom Playtest
- [ ] Vertical Slice Dive Bar: Raum-Layout, Modellierung (Blender-Pipeline), eigene Materialien, Licht inkl. Neon; ggf. Meshy/Higgsfield-Tests
- [ ] Sound: Kugel-Klicks, Banden, Taschen, Bar-Atmo – gesteuert von den Physik-Events. Vergleichstest: echte Aufnahmen (lizenzfreie Libraries) vs. physikbasierte Klick-Synthese vs. KI-SFX (Higgsfield Mirelo); Higgsfield-SFX/Musik sind nur in deren Game-Pipeline nutzbar (nicht für uns) → Klicks per Synthese, Rest aus lizenzfreien Libraries (Sonniss GDC, Freesound CC0, Pixabay) bzw. ElevenLabs SFX; Sprachzeilen per Higgsfield-TTS (~2 Credits Test); Jukebox-/Menü-Musik: Claude schreibt Prompts, Nutzer generiert mit Gemini (kostenlos) – vorher Googles Nutzungsbedingungen für kommerzielle Nutzung prüfen
- [ ] (läuft seit Fenster 2) KI-Gegner: Stoßplanung über den Simulator + Skill-Profile aus dem Human-Factors-Modell
- [ ] UI/UX: Hauptmenü-Szene, Settings-Menü mit Grafik-Presets, Mockups
- [ ] Sobald der Nutzer M1 gespielt hat: Feedback (Stoß-Gefühl, Kamera, Steuerung) parallel einarbeiten

## Später
- Pool hall mit ~8 Tischen: KI-Stammgäste spielen echte simulierte Partien an den Nachbartischen; später online geteilte Hallen (nur Stoß-Eingaben übertragen, deterministische Physik rechnet überall identisch). Voraussetzung ab jetzt: mehrere Tische + Matches pro Level.
- Vom Tisch gesprungene Kugeln: Übergabe an Unreal-Physik (Boden, rollt unter Hocker), Aufheben als Chore.
- Körper & Hände (MetaHuman – braucht einen Schritt vom Nutzer), Karriere, KI-Gegner, weitere Spielarten/Venues, Steam-Store-Page, Trailer (erst nach Freigabe durch den Nutzer)


## M2 (läuft seit 28.09. abends)
- [ ] Dive Bar v1 (The Low Bridge Tavern, DB-0..DB-3 ohne Meshy-Props), Sound v1 (alle Physik-Sounds, Raumklang), Pause-/Settings-Menü v1, Kugeln fallen vom Tisch auf den Boden, Vorbereitung Mehr-Tisch-Räume → Integration + gepackter M2-Build
