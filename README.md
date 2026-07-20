# PebbleDiver

Een one-button duikspel voor de Pebble Time 2 (emery).

Houd **SELECT** ingedrukt om omhoog te zwemmen, houd **OMLAAG** ingedrukt om
sneller te dalen, laat los om te zinken. Ontwijk rotsen en kwallen, verzamel
parels en luchtbellen, en duik zo diep mogelijk voordat je zuurstof op is.

Het doel: ergens in de diepte wacht de **magische Pebble**. Die verschijnt
alleen voor duikers met volledige uitrusting (alle upgrades op max), vanaf
200 m diepte — een glinsterende steen die van kleur wisselt. Pak hem en je
hebt het spel uitgespeeld; daarna draagt je duiker hem op het titelscherm.
Mis je hem, dan duikt hij 40 m dieper opnieuw op.

## Talen

De app volgt de taal van het horloge (`i18n_get_system_locale()`):
Engels (standaard/fallback), Nederlands, Frans, Duits en Spaans. De
vertaaltabel staat bovenin `src/c/main.c`.

## Gameplay

- **Zuurstof** loopt continu terug; een botsing met een rots of kwal kost 6
  seconden extra. Luchtbellen vullen 6 seconden bij.
- **Parels** zijn de valuta. Ze blijven bewaard tussen runs (ook als je een run
  afbreekt met TERUG → TERUG).
- Hoe dieper je komt, hoe sneller het water stroomt en hoe drukker het wordt.
- Het water kleurt donkerder naarmate je dieper komt.

## Winkel (OMLAAG vanaf titel of game-over)

| Upgrade      | Effect                          |
|--------------|---------------------------------|
| Zuurstoftank | +8 s zuurstof per level         |
| Vinnen       | sneller stijgen én dalen        |
| Lamp         | trekt parels/bellen naar je toe |

Elk 5 levels; kosten: 15, 40, 90, 160, 250 parels.

## Besturing

| Knop   | Titel     | In-game             | Winkel     |
|--------|-----------|---------------------|------------|
| SELECT | duiken    | omhoog zwemmen      | kopen      |
| OMLAAG | winkel    | sneller dalen       | selectie ↓ |
| OMHOOG | –         | –                   | selectie ↑ |
| TERUG  | afsluiten | pauze / run stoppen | sluiten    |

Na "Zuurstof op!" reageren SELECT/OMLAAG pas na een seconde, zodat je niet per
ongeluk direct een nieuwe run start.

## Bouwen

```sh
pebble build
pebble install --emulator emery
```

`tools/press.py` stuurt knopdrukken naar de draaiende emulator (via de
pypkjs-websocket), handig om het spel headless te testen. Het script heeft
`libpebble2` nodig; de eenvoudigste interpreter daarvoor is de uv-venv van
pebble-tool:

```sh
~/.local/share/uv/tools/pebble-tool/bin/python tools/press.py select sleep:400 hold:select:1500 back
```
