#!/usr/bin/env python3
"""
Patch TFT_eSPI pour le M5StickC Plus2 (écran ST7789 135x240).

Pourquoi ce patch existe :
Pour un panneau 135x240 en vue paysage (rotation 1 ou 3), TFT_eSPI utilise
par défaut colstart=40 et rowstart=53 (rotation 1) / rowstart=52 (rotation
3). Ce sont des valeurs génériques qui ne correspondent pas exactement à la
dalle réellement montée sur le M5StickC Plus2, ce qui laisse un résidu de
pixels visibles sur les bords de l'écran :
  - bande NOIRE = zone du panneau jamais dessinée (offset pas assez grand)
  - ligne BLANCHE/claire = décalage inverse, qui expose une zone déjà
    écrite du GRAM au lieu du bord réel du panneau (offset trop grand)

Axes (vérifié dans TFT_eSPI.cpp, fonction setWindow) :
  - colstart décale l'axe X de l'écran tel qu'affiché  -> gauche/droite
  - rowstart décale l'axe Y de l'écran tel qu'affiché  -> haut/bas

Ce script s'exécute avant chaque build de l'environnement m5stickc_plus2 et
patche directement colstart/rowstart dans le fichier driver installé par
PlatformIO (TFT_Drivers/ST7789_Rotation.h). Comme ce fichier appartient à
la librairie TFT_eSPI (téléchargée via lib_deps), il serait sinon
écrasé/réinitialisé à chaque réinstallation propre de la lib (rm -rf .pio,
mise à jour de version, etc.) - ce script le re-patch systématiquement.

CORRECTION IMPORTANTE : la "grosse bande noire en haut" observée n'était PAS
un problème de colstart/rowstart. La vraie cause : sur les petits écrans
(SMALL_DISPLAY), display_update() efface tout l'écran à chaque frame
(fillScreen), mais le header (24px en haut, dessiné par drawHeader()) n'était
redessiné qu'au tout premier affichage - dès la frame suivante, le clear
effaçait le header et rien ne le repeignait, laissant une bande noire
permanente à cet endroit. Corrigé dans src/display/display.cpp,
display_update() : le header est maintenant redessiné à chaque frame sur
SMALL_DISPLAY, pas seulement au premier affichage.

Les deltas ci-dessous sont donc remis à 0 (valeurs stock TFT_eSPI, qui
étaient correctes depuis le début - les 3 essais précédents ajustaient un
paramètre qui ne réglait pas le vrai problème). Ce script reste disponible
si, une fois le vrai bug corrigé, il reste un résidu de quelques pixels dû
au colstart/rowstart générique - dans ce cas seulement, ajuste comme décrit
ci-dessous.

Historique des essais précédents (offset, piste abandonnée) :
  - rowstart stock (53/52), colstart stock (40) -> semblait montrer une
    grosse bande noire en haut, en fait le vrai bug ci-dessus
  - rowstart +6, colstart stock                 -> fine bande noire en haut
                                                     + fine ligne blanche à
                                                     gauche (bruit introduit
                                                     par un mauvais réglage,
                                                     pas une amélioration)
  - rowstart +8, colstart -2                     -> idem, toujours pas la
                                                     bonne piste

Réglage fin (seulement si un résidu subsiste après le vrai fix) : ajuste
ROWSTART_DELTA et COLSTART_DELTA ci-dessous, un axe à la fois si possible
pour ne pas mélanger les effets. Recompile et reflashe après CHAQUE
changement (pio run -e m5stickc_plus2 -t upload) :

  ROWSTART_DELTA (axe haut/bas en vue paysage) :
    - bande/ligne encore visible EN HAUT -> augmenter (+1, +2...)
    - bande/ligne apparaît EN BAS        -> diminuer

  COLSTART_DELTA (axe gauche/droite en vue paysage) :
    - bande/ligne visible à GAUCHE  -> diminuer (colstart trop grand) si
      c'est une ligne blanche/claire, ou augmenter si c'est une bande noire
    - bande/ligne visible à DROITE  -> l'inverse

Avance par pas de 1 à 2 - les gros deltas ne servent qu'à dégrossir.
"""

import re
from pathlib import Path

Import("env")  # noqa: F821  (injecté par PlatformIO)

# ============================================================
# Valeurs à ajuster (voir explications ci-dessus)
# ============================================================
ROWSTART_DELTA = 0   # remis à 0 (stock) : la vraie cause de la bande noire
                      # était ailleurs (voir display.cpp display_update) -
                      # laisser à 0 sauf si un résidu de quelques pixels
                      # subsiste une fois le vrai bug corrigé
COLSTART_DELTA = 0    # idem, remis à 0 (stock)

# Valeurs par défaut de TFT_eSPI pour _init_width == 135
DEFAULT_COLSTART = 40
DEFAULT_ROWSTART_BY_ROTATION = {1: 53, 3: 52}


def patch_rotation_file():
    project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    lib_dir = project_dir / ".pio" / "libdeps" / "m5stickc_plus2" / "TFT_eSPI"
    rotation_file = lib_dir / "TFT_Drivers" / "ST7789_Rotation.h"

    if not rotation_file.exists():
        print(f"[patch_tft_espi_m5stick] {rotation_file} introuvable, patch ignoré (lib pas encore installée)")
        return

    content = rotation_file.read_text()
    target_colstart = DEFAULT_COLSTART + COLSTART_DELTA
    total_count = 0

    for rotation, default_rowstart in DEFAULT_ROWSTART_BY_ROTATION.items():
        target_rowstart = default_rowstart + ROWSTART_DELTA

        # On distingue les deux blocs "if (_init_width == 135)" (rotation 1
        # vs 3) via leur POSITION dans le fichier (celui qui suit "case 1:"
        # vs celui qui suit "case 3:"), pas via la valeur de rowstart -
        # celle-ci change à chaque run (déjà patchée ou non), donc on ne
        # peut pas s'appuyer dessus pour identifier le bloc de façon fiable.
        case_marker = f"case {rotation}:"
        case_pos = content.find(case_marker)
        if case_pos == -1:
            print(f"[patch_tft_espi_m5stick] ATTENTION: repère '{case_marker}' introuvable")
            continue
        next_case_pos = content.find("case ", case_pos + len(case_marker))
        search_end = next_case_pos if next_case_pos != -1 else len(content)

        pattern = re.compile(
            r"(if \(_init_width == 135\)\s*\{\s*colstart = )\d+(;\s*rowstart = )\d+(;\s*\})"
        )
        m = pattern.search(content, case_pos, search_end)
        if not m:
            print(f"[patch_tft_espi_m5stick] ATTENTION: bloc 135 non trouvé dans la section rotation {rotation}")
            continue

        replacement = f"{m.group(1)}{target_colstart}{m.group(2)}{target_rowstart}{m.group(3)}"
        content = content[:m.start()] + replacement + content[m.end():]
        total_count += 1
        print(f"[patch_tft_espi_m5stick] rotation {rotation}: colstart -> {target_colstart}, rowstart -> {target_rowstart}")

    if total_count > 0:
        rotation_file.write_text(content)
        print(f"[patch_tft_espi_m5stick] Terminé - colstart={target_colstart}, "
              f"rowstart rotation1={53 + ROWSTART_DELTA}, rotation3={52 + ROWSTART_DELTA}")


patch_rotation_file()
