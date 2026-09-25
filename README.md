# RAW BREAK

First-Person, ultra-realistisches Pool-Billard – mit dem rohen „sieht aus wie echtes Footage“-Gefühl von *Bodycam*,
übertragen auf den Billardtisch: echte Spielerperspektive, Hände, Queue, Kreide, Raytracing, echte Physik, echte Regeln.

## Aufbau

| Pfad | Inhalt |
|---|---|
| `RawBreak.uproject` | Unreal-Engine-5.8-Projekt |
| `Source/RawBreak/` | Spielmodul (Unreal): Kamera, Spieler, Tisch-Actors, UI, Spielablauf |
| `Source/BilliardsCore/` | **Engine-unabhängiger Kern**: Ereignis-basierte Billard-Physik + Regel-Engine (WPA). Kein Unreal-Code – wird von Unreal *und* standalone per CMake gebaut |
| `Tests/Core/` | Unit-Tests für den Kern |
| `Docs/specs/` | Recherchierte & verifizierte Spezifikationen (Physik, Equipment, Regeln, Rendering) |
| `Docs/architecture.md` | Architektur von BilliardsCore: Datenfluss, Event-Loop, Toleranzen, Traceability, Arbeitspakete |
| `Tools/rbsim/` | CLI: simuliert einen Stoß und schreibt JSON (Event-Log, Trajektorien, Tischgeometrie) |
| `Config/` | Unreal-Projekteinstellungen (Hardware-Raytracing, Lumen, DX12/SM6) |

## Kern bauen & testen (ohne Unreal)

Voraussetzungen: Visual Studio 2022 (C++), CMake ≥ 3.25.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Spiel bauen (Unreal)

1. Unreal Engine 5.8 über den Epic Games Launcher installieren.
2. Rechtsklick auf `RawBreak.uproject` → *Generate Visual Studio project files*.
3. `RawBreak.sln` öffnen, Konfiguration *Development Editor*, bauen und starten.

Große Binärdateien (Assets, Texturen, Audio) liegen per **Git LFS** im Repo – vor dem Klonen `git lfs install` ausführen.
