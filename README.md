# OctoSlice

**Sample slicer instrument (VST3) — by OctoBio**

Glisse un sample, découpe-le (grille, transitoires ou à la main), puis rejoue chaque découpe au clavier pour recréer une mélodie ou un beat dans ton DAW. Interface épurée, ambiance spatiale : le fond étoilé, les nébuleuses et les particules réagissent au son et aux réglages.

![OctoSlice](docs/screenshot.png)

## Fonctions

**Découpe**
- **Grid** : découpe régulière 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, sur une longueur de sample de 1/2 à 8 mesures — ou **Auto** (calculée à partir du tempo du DAW).
- **Transient** : détection automatique des attaques (spectral flux + énergie), réglages *Sensitivity* et *Min length*.
- **Manual** : double-clic sur la forme d'onde pour ajouter une découpe, glisser un marqueur pour le déplacer, clic droit (ou Alt-clic) pour le supprimer. Toucher un marqueur en mode Grid/Transient bascule automatiquement en Manual en gardant les découpes existantes.
- **Zero-X** : cale les découpes sur le passage par zéro le plus proche (pas de clics).

**Jeu**
- Une découpe par touche, à partir de la note **ROOT** (C3 par défaut — convention Bitwig/FL), jusqu'à 128 découpes.
- **One-shot** (la découpe joue jusqu'au bout) ou **Gate** (s'arrête au relâchement de la touche).
- **Mono** : chaque nouvelle découpe coupe la précédente (comportement slicer classique). Sinon polyphonique (16 voix).
- **Reverse**, **Pitch** (±24 demi-tons), enveloppe **Attack / Release**, filtre **Tone**, réverbe **Space**, **Gain**.
- Clic sur une découpe = pré-écoute directe dans le plugin.

**DAW**
- **DRAG MIDI** : glisse ce bouton sur une piste de ton DAW → un clip MIDI avec une note par découpe, au timing d'origine (tempo du DAW). Tu n'as plus qu'à réarranger les notes.
- Le sample est **sauvegardé dans le projet** (jusqu'à 90 s, compressé en FLAC) : rien ne casse si tu déplaces le fichier d'origine.

## Installation (Windows)

1. Va dans l'onglet **Actions** (ou **Releases**) du dépôt et télécharge `OctoSlice-Windows-x64.zip`.
2. Copie le dossier `OctoSlice.vst3` dans `C:\Program Files\Common Files\VST3\`.
3. Dans Bitwig : *Settings → Locations → Plug-ins*, relance un scan si besoin. OctoSlice apparaît dans les instruments (OctoBio).

`OctoSlice.exe` est une version autonome pour tester sans DAW.

## Compiler soi-même

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release        # télécharge JUCE 8.0.15 automatiquement
cmake --build build --config Release --target OctoSlice_VST3
```

Tests hors-ligne (découpe, lecture, sauvegarde d'état, export MIDI, captures de l'UI) :

```bash
cmake -B build -DOCTOSLICE_BUILD_TESTS=ON && cmake --build build --target OctoSliceTests
./build/OctoSliceTests_artefacts/Release/OctoSliceTests
```

Validé avec [pluginval](https://github.com/Tracktion/pluginval) au niveau de rigueur 7.

## Raccourcis

| Action | Geste |
|---|---|
| Charger un sample | glisser-déposer n'importe où, ou bouton LOAD |
| Écouter une découpe | clic |
| Ajouter une découpe | double-clic |
| Déplacer | glisser le marqueur |
| Supprimer | clic droit / Alt-clic sur le marqueur |
| Zoom / défilement | molette / Maj + molette |

## Licence

Construit avec [JUCE](https://juce.com) — distribué sous licence **AGPL-3.0** (voir `LICENSE`).
