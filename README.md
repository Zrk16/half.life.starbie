# Starbie

a tiny motion-controlled desk pet on a pixel-star PCB, basically a desktop tamagotchi (totally not a starboy). except mine also tells the time, checks the weather, and clicks your slides.

built for [Hack Club Half Life](https://halflife.hackclub.com), week 1. this is my first PCB ever.

![Starbie front render](images/starbie-render-front.png)

## what it does

starbie has 3 apps. **hold button 2** (about half a second) to switch between them.

| app | what it does | button 1 | tap button 2 |
| --- | --- | --- | --- |
| **pet** | the og starbie. wanders around the screen, reacts when you shake it | opens the radial menu (nap / play / feed / pet), tilt to pick | shows stats |
| **desk** | clock, date, outside weather, room temp + humidity | refresh weather | - |
| **remote** | bluetooth slide clicker for presentations | next slide | previous slide |

the weather comes from [Open-Meteo](https://open-meteo.com), which doesn't need an api key, so there's nothing secret in this repo.

## hardware

| part | job |
| --- | --- |
| Seeed XIAO ESP32-C3 | the brain (wifi + bluetooth) |
| 0.96" SSD1306 OLED | starbie's face |
| MPU6050 | tilt + shake sensing |
| DHT11 + 10k resistor | room temp and humidity |
| 2x Cherry MX switches |  buttons |

the board is a  star, about 96 x 92 mm.

![Starbie back render](images/starbie-render-back.png)

(back side: the ground pour, plus the 2 traces i had to move to the back layer)

## how i built it

1. **schematic** in kicad. first time ever, made a lot of mistakes (wrong chip, using labels instead of power symbols for ground) but got it clean.
2. **pixel star outline** and placed the parts inside the arms.
3. **routing.** did everything on the front first, DRC found 2 broken connections, fixed them on the back layer.
4. **ground pour and text** on the silkscreen, then exported the gerbers.
5. **simulated it in [Wokwi](https://wokwi.com)**
6. 
![routed board](journal/09-routed-drc-clean.png)

![stock firmware running in wokwi](journal/13-wokwi-stock-firmware-running.png)

the full story (with all the mistakes) is in [JOURNAL.md](JOURNAL.md).


## files

| path | what's in it |
| --- | --- |
| `starbie.kicad_sch` / `starbie.kicad_pcb` | schematic and board |
| `libs/` | the care package symbols, footprints and 3d models (so the project opens anywhere) |
| `gerbers/` + `starbie-gerbers.zip` | fab files, upload the zip to JLCPCB etc |
| `firmware/StarbieCustom/` | the firmware |
| `images/` + `journal/` | renders and build screenshots |

## status

- [x] schematic, pcb, gerbers
- [x] stock firmware tested in wokwi
- [ ] custom apps (desk + remote) fully tested
- [ ] order the board
- [ ] solder it and flash the real thing

## credits

based on [Starbie](https://github.com/SharKingStudios/Starbie) by Logan Peterson / SharKingStudios (MIT license, see `firmware/LICENSE-starbie-original.txt`) and the Half Life week 1 guide.
