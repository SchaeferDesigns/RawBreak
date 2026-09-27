# rbsim examples

`break9.json`: a 9 m/s 9-ball break on the 9-ft pro table (wooden-rack micro-gaps, rack seed 11, break cue, cue ball 10 cm behind
the head string and 12 cm off the long string, aimed at the apex ball, slight draw). Status Ok, 95 events, one CLI island of
660 steps, the 5 ball pocketed in P1; the rules record and facts are included.

Open `../viewer/index.html` in a browser and load the file, or serve the repository (`python -m http.server` in its root) and open
`Tools/rbsim/viewer/index.html?src=../examples/break9.json`.

Made with (from a Release build of `Tools/rbsim`):

```
rbsim --table 9ft-pro --rack 9ball --rack-gap wooden --seed 11 --cue break --ball 0:-0.735,0.12 --speed 9 --aim -5.006 \
      --offset 0,-0.1 --geometry --dt 0.02 --compact --no-states --record --facts --out break9_full.json
python round_json.py break9_full.json break9.json
```

Samples every 20 ms plus every segment boundary (the viewer interpolates linearly between samples); `round_json.py` rounds the
numbers to 6 significant digits (rbsim writes 17 for bitwise replays), which keeps the file under 0.5 MB.
