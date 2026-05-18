# 2D/3D Map Editor

Jednoduchý tile-based editor map s 2D editorem a 3D náhledem, napsaný v C++ pomocí knihovny **raylib**.

## Funkce

**Editor (2D)**
- Kreslení buněk štětcem, gumou nebo tvary (čára, obdélník, kruh)
- Výběr, přesun, kopírování a vkládání oblastí
- Zrcadlení a rotace výběru
- Nastavení barvy buňky, typu (Block / Decor / Texture Only) a kolize
- Neomezené undo/redo (Ctrl+Z / Ctrl+Y)
- Dynamická mapa – automaticky se rozrůstá při editaci u okrajů

**3D náhled**
- First-person kamera (pohyb myší, Backspace = přepnutí kurzoru)
- Buňky vykresleny jako 3D kvádry s výškou a barvou
- Nastavitelná barva oblohy (F1)

## Ovládání (editor)
| Akce | Klávesa / tlačítko |
|---|---|
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Kopírovat / Vložit | Ctrl+C / Ctrl+V |
| Rotace výběru | R |
| Zrcadlení X / Y | Ctrl + Arrow Keys |
| Smazat výběr | Delete |

## Ukládání
Mapy se ukládají do binárního formátu `.bin` (tlačítka Save / Load v horní liště).
