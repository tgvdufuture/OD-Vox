# Rapport de verification — OD Vox

Date : 2026-10-09 · cible : `C:\Program Files\Common Files\VST3\OD Vox.vst3`

| Controle | Resultat | Detail |
|----------|----------|--------|
| aucun recouvrement de cle chez l'hote | oui | 29 cles distinctes pour 29 parametres |
| tous les parametres declares sont exposes | oui | 29 cles presentes |
| aucun parametre inattendu | oui | seul `bypass` s'ajoute, il vient du format VST3 |
| le compte de parametres est coherent | oui | 30 cles vues par l'hote pour 29 parametres declares + 1 implicite |
| le plugin traite l'audio (sortie non nulle) | oui | sortie a +1.62 dB par rapport a l'entree (defauts : gains a 0 dB) |
| la sortie est numeriquement saine | oui | 0 echantillons non finis |
| reglages tonaux neutralises et curseurs a 0 %, la chaine est transparente | oui | sortie identique au bit pres (apres conversion float32) |
| l'ecart des defauts est la seule bande Air (+2,5 dB) | oui | Air mesure a 15 kHz : +2.69 dB (attendu ~+2,5) |
| les 6 taux d'echantillonnage passent | oui | 44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz, sortie finie |
| les tailles de bloc de 1 a 100 000 echantillons passent | oui | 1, 16, 64, 512, 4096, 100000 |
| les gains d'entree et de sortie sont exacts au dB | oui | input_gain_db -12 -> -12.00; input_gain_db -6 -> -6.00; input_gain_db +0 -> -0.00; input_gain_db +6 -> +6.00; input_gain_db +12 -> +12.00; output_gain_db -24 -> -24.00; output_gain_db -12 -> -12.00; output_gain_db -6 -> -6.00; output_gain_db +0 -> -0.00; output_gain_db +6 -> +6.00; output_gain_db +12 -> +12.00; output_gain_db +24 -> +24.00 |
| latence nulle | oui | impulsion en 256, retrouvee en 256 |
| le bypass est un vrai contournement, tous modules pousses (AC3 de F1.13) | oui | pire ecart 0.000000015 (-156.5 dB, exigee sous -120 dBFS), apres alignement du decalage d'hote (-4 ech.) |
| le gate attenue le sous-seuil de la valeur reglee (AC1 de US-03) | oui | reduction sous le seuil -79.99 dB pour le range interne 80 dB |
| le low cut (On) coupe le grave : ~-3 dB au coin 120 Hz, inaudible a 1 kHz | oui | -3.01 dB a 120 Hz, 0.00 dB a 1 kHz |
| le low cut (On) attenuue le grave d'au moins 20 dB par octave | oui | -48.2 dB a 30 Hz (24 dB/oct : deux octaves sous le coin, le filtre attenu au-dela de 40 dB) |
| aux curseurs a 0 %, la chaine n'ajoute rien dans le silence | oui | pire echantillon en sortie 0.000e+00 sur 1 s de silence |
| la table macro retenue est reproduite (F1.7b) | oui | nivelement 9.77 dB de 0 a 100 % (cible : 8,6 dB ; make-up brut de la table : 27,5 dB, compense par la GR croissante) |
| le make-up fait monter le niveau avec le curseur (fidelite F1.7b) | oui | sorties successives -24.4 / -18.4 / -14.6 dBFS |
| `comp_amount` 0 % est transparent (le curseur engage le module) | oui | identique au bit pres (float32) |
| a 100 %, la crete de sortie reste bornee (make-up vs GR) | oui | crete de sortie 0.371 (< 2,0 : le staging plafonne le make-up de table) |
| le de-ess reduit la sifflante ; le grave ne voit que le shelf lie (AC1) | oui | sifflante -3.13 dB (plafond ~−3 dB de la cible), grave -0.81 dB (shelf lie ~−1,2 dB) |
| les plosives suivent le curseur puis plafonnent (liees, cap 20 %) | oui | grave reduit de 0.48 dB a 20 % puis 0.81 dB a 100 % (loi liee 0,6 x amount, cap 0,20) |
| `drive_amount` 0 % est transparent au bit pres (AC2) | oui | identique au bit pres (float32) |
| l'engin fige Console reproduit la signature mesuree de la cible | oui | 3f -11.3 dB (cible -10.1), 5f -18.9 dB (cible -15.8), harmoniques paires au plancher : oui |
| le drive ne depasse pas +6 dBFS a 100 %, entree a -12 dBFS | oui | crete de sortie -12.00 dBFS |
| la bascule HQ ne change pas le niveau de sortie (< 1 dB, AC3) | oui | ecart RMS 0.000 dB entre HQ off et HQ on |
| le rapport de latence HQ est honnete (chemin sec aligne apres PDC hote) | oui | impulsion en 256, chemin sec retrouve en 256 (260 = sous-rapport, 252 = sur-rapport) |
| US-08 : doubler a 0 % et width a 100 % rend le module transparent | oui | sortie identique au bit pres (apres conversion float32) |
| US-08 AC4 : la matiere ajoutee par le doubler est decorrelee (< 0,5) | oui | correlation de (sortie 100 % − sortie 0 %) entre L et R : 0.395 |
| US-08 : le doubler ne change pas le niveau (sortie <= entree + 3 dB) | oui | sortie -14.35 dB contre -13.47 dB en entree |
| US-08 : le doubler desaccorde (l'energie quitte la raie exacte) | oui | raie a 220 Hz : -1.80 dB du niveau d'entree |
| US-08 AC1 : width a 100 % laisse le signal inchange | oui | sortie identique au bit pres (apres conversion float32) |
| US-08 AC1 : width a 0 % rend le signal strictement mono (correlation 1,0) | oui | correlation L/R = 1.0000 |
| US-08 : a 200 % de largeur, le grave au-dessus de la coupure reste serre | oui | correlation a 120 Hz = 1.00000 (exigee >= 0,98) |
| US-08 : a 200 % de largeur, l'aigu est bien elargi | oui | side a 3 kHz : 0.39759 contre 0.20000 en entree |
| US-08 AC3 : aucune composante subsonique ajoutee (plancher −80 dBFS) | oui | pire ajout -117.3 dBFS a 9 Hz (mesure fenetree 0-20 Hz) |
| la reponse de l'EQ suit la courbe affichee a ±0,5 dB (AC2 de US-04) | oui | pire ecart 0.22 dB (250 Hz : reglages (12.0, -12.0, 9.0, 12.0)) |
| le gain d'une bande atteint le filtre (AC1 de US-04, rev. ancres figees) | oui | centre +12.03 dB a 700 Hz (ancre Mid), une octave dessous +3.34 dB |
| l'Air par defaut (+2,5 dB) est un plateau haut au-dessus de 10 kHz | oui | 2.32 dB a 12 kHz, 2.69 dB a 15 kHz |
| `eq_on` Off court-circuite l'EQ, meme avec des bandes actives | oui | sortie identique au bit pres (apres conversion float32) |
| toutes les bandes a 0 dB : l'EQ est neutre au bit pres | oui | sortie identique au bit pres (apres conversion float32) |
| la calibration place la crete entre -12 et -6 dBFS (AC1 de US-06) | oui | crete mesuree -9.01 dBFS, gain applique +21.00 dB |
| le traitement est deterministe | oui | deux passes sur le meme signal donnent un resultat bit a bit identique |
| le silence en entree ne produit pas de bruit de fond | oui | sortie a 0.000e+00 sur 1 s de silence |
| un signal a +6 dBFS crete ne fait ni NaN ni divergence | oui | crete en sortie : 3.3615 |
| une entree a -12 dBFS crete ne ressort pas au-dessus de 0 dBFS | oui | crete en sortie : -12.04 dBFS |
| chaque controle balaie toute sa plage | oui | tous les controles repondent en butee basse et haute |
| les choix rendent bien leurs libelles extremes | oui | 1 controle(s) a 21 choix |
| delay sync 1/4 a 120 BPM : premier echo a 500 ms (AC1) | oui | premier echo 500.0 ms |
| delay libre : premier echo a la valeur reglee (300 ms) | oui | premier echo 300.0 ms (valeur libre lue 300 ms) |
| feedback interne figure : pas d'emballement (AC3, rev. ancres figees) | oui | echo 2 1.000, fin de rendu 0.000 (decroissance exigee) |
| ducking : les echos remontent apres la fin de la voix (AC2) | oui | attenuation pendant la voix +18.0 dB, relachement apres +0.1 dB → rebond +17.8 dB (release < 300 ms) |
| ping-pong : l'echo 1 sort a gauche, l'echo 2 a droite | oui | echo 1 L/R +123.4 dB, echo 2 R/L +111.9 dB |
| les 4 curseurs a 0 % sont transparents au bit pres (AC3) | oui | identique au bit pres (float32) |
| chaque curseur produit sa couleur : Short < Small < Big < Lush (AC1) | oui | persistances Short 0.30 s, Small 0.55 s, Big 0.95 s, Lush 1.50 s |
| les moteurs sont cumules : attaque ET traine (Small+Big) | oui | attaque -48.3 dBFS, traine -81.9 dBFS (planchers -60/-82 : la queue IR sous-estime la queue percue sur materiau continu ; Small seul serait eteint a 0,9 s) |
| le pre-delay preregle vaut 20 ms (AC2) | oui | onset 54.6 ms, attendu impulsion + 20 ms + reseau = 54.6 ms (delai de reseau inclus) |
| la queue decroit toujours, meme a 4 moteurs a fond (stabilite) | oui | fin -31.2 dB sous le debut, pic 0.700 (plafond doux a 0,8) |

## Conclusion

**CONFORME** — les 59 controles sont au vert.
