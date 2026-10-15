# Starbie

a tiny motion-controlled desk pet on a pixel-star PCB, basically a desktop tamagotchi (totally not a starboy). except mine also tells the time, checks the weather, and clicks your slides.

built for [Hack Club Half Life](https://halflife.hackclub.com), week 1. this is my first PCB ever.

![Starbie front render](images/starbie-render-front.png)

## what it does

starbie has 3 apps. **hold button 2** (about half a second) to switch between them.

3 apps are:
- Pet(OG)
- Desk(clock, weather, and timer)
- CLicker(like a presentation clicker)

the weather comes from [Open-Meteo](https://open-meteo.com),



this is the board

![Starbie back render](images/starbie-render-back.png)

(back side: the ground pour, plus the 2 traces i had to move to the back layer)

## how i built it

1. **Kicad**, made a lot of mistakes but I did it in the end)
2. **star** placed the parts inside the arms.
3. **routing.** made some mistakes but i got it in the end
4. **ground pour and text** on the silkscreen, then exported the gerberts.
5. **simulated it in [Wokwi](https://wokwi.com)**
6. 
![routed board](journal/09-routed-drc-clean.png)

![stock firmware running in wokwi](journal/13-wokwi-stock-firmware-running.png)




## credits

based on [Starbie](https://github.com/SharKingStudios/Starbie) by Logan Peterson / SharKingStudios (MIT license, see `firmware/LICENSE-starbie-original.txt`) and the Half Life week 1 guide.
