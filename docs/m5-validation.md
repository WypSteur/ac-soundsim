# M5 — GT86 reference implementation : quatre gates indépendants

La GT86 ne devient la reference implementation complète qu'après PASS de
**M5A + M5B + M5C + M5D**. Une bonne impression sonore générale ne remplace pas
ces critères. CI/source, mock et écoute native sont trois niveaux de preuve.

| Bloc | Preuves disponibles | Statut global |
| --- | --- | --- |
| M5A dynamique moteur | Correction prouvée, gate source multi-cadences Release/Debug | PARTIEL — écoute dynamique native à faire |
| M5B propagation CSP | Paramètres/pose testés en mock | À VALIDER EN JEU |
| M5C mix hybride | Ownership/fallback + autres FMOD intacts testés en mock | À VALIDER À L'OREILLE |
| M5D latence/robustesse | IPC + QPC audit testés, capture/analyse préparées | PARTIEL — mesures et essais natifs à faire |

Aucune ligne n'est marquée PASS natif sans session, paramètres, preuves et retour
d'écoute. Aucun seuil de latence arbitraire n'est choisi automatiquement.

## Référence et préparation commune

Cible unique `ks_toyota_gt86`, FA20D public + smooth_39, master source0.25,
gain bridge8, guard actif. Les niveaux et transferts cabine ne sont pas changés.
Dernière référence utilisateur : trim -8dB, mids -8dB, highs -24dB /2087Hz,
body0dB ; paramètres de session, pas nouveaux defaults. Les sliders ne sont pas
persistés : les noter/rétablir avant chaque essai. Pas de prétention GT86 stock.

Utiliser une session **live**, pas le replay AC (volontairement muté).
Noter circuit/version AC+CSP, caméra, réglages bridge/AC, vitesse, RPM et mods.
Ne modifier aucun niveau FMOD complémentaire/global entre les essais.
Faire les coups de gaz avec les autres applications audio silencieuses.

## M5A — dynamique moteur

Séquences natives requises :

- accélération rapide 800→7400rpm ;
- montées des rapports pleine charge, coupure/reprise cohérente ;
- décélération rapide + reprise des gaz ;
- zéro impulsion manquée/doublée ; aucun clic, trou ni saut de hauteur anormal.

L'allumage dynamique corrigé est une précondition, pas un critère facultatif.
Gate source strict : 30/60/90/120/144/165/240Hz + cadence irrégulière30..240Hz
et retards de50ms, chacun avec deux conditions initiales de phase/paquet.
Derniers runs Release et Debug : 16/16 scénarios, `firing_sequence_anomalies=0`, cadence
PCM exacte, phase/RPM AC exacts, gaz stables et zéro sample sur les rails.
Au-dessus de150Hz, les paquets sont coalescés comme dans le runtime : ce ne sont
pas240 états effectivement rendus par seconde. Pas de lissage RPM ou prédiction.

`M5A_dynamic_gate` échoue désormais si une anomalie apparaît. Tests séparés :
`ignition_dynamic_regression`, `ignition_90hz_known_upstream` (attend le défaut
historique) et `ignition_90hz_fixed` (exige zéro). Le contrôle historique n'est
pas une tolérance d'erreur du moteur normal.

### Preuve exacte du défaut 90Hz

La trace inclut les pas SANS étincelle, quatre lignes par pas, sans allocation/
écriture de fichier pendant le render. À t=3.826712018s, cylindre2, cycle122 :

```text
RPM précédent/courant : 2046.6667 / 1973.3333
avance (rad)          : 1.220101502 / 1.205440738
seuil précédent s0   : 1538.160298757
vilebrequin r0       : 1538.165651100
seuil courant s1     : 1538.174959521
vilebrequin r1       : 1538.175022845

s0 < r0 < s1 < r1
```

Le cylindre avait déjà été déclenché sur s0 ; le seuil retardé s1 tombe dans le
nouvel intervalle et l'upstream le redéclenche **dans le même cycle**. Ce n'était
donc pas la preuve d'une étincelle manquée. Une étincelle répétée n'est pas non
plus la preuve d'une double combustion audible : la chambre garde ses guards.

Correction uniquement dans l'adaptateur externe : croisement de
phase+avance sur le pas (mouvement des deux trajectoires), identité
cylindre/cycle720° pour interdire un doublon lors d'un recul/recroisement,
gestion démarrage/coupure/reprise. La mécanique/RPM AC reste inchangée ;
gaz, fuel, courbes, IR et DSP public inchangés. L'upstream reste épinglé/non édité.
Un test compare aussi le PCM statique corrigé au contrôle upstream.

Reproduire les deux traces, hors jeu :

```powershell
.\build\Release\soundsim-ignition-audit.exe --output artifacts/m5/ignition-upstream --upstream
.\build\Release\soundsim-ignition-audit.exe --output artifacts/m5/ignition-fixed
```

Le CSV garde `upstream_fired` et `selected_fired` pour voir exactement le
déclenchement écarté. Les chemins générés sont ignorés par Git.

## M5B — propagation CSP

SoundSim = source ; CSP = monde acoustique. Aucun Doppler/distance artificiel
n'est ajouté à la synthèse. Valider en conduite live avec une caméra piste/free
fixe pendant que la GT86 passe, dans les deux sens :

- approche → passage → éloignement, sans saut de volume/position ;
- avant/arrière de l'échappement : cône/orientation cohérents ;
- proche → loin → proche : atténuation continue ;
- Doppler continu, pas de glissement/pitch ajouté par une autre couche ;
- transitions cabine/externe cohérentes.

Pose/vitesse, cône, distances et demande Doppler existent déjà dans le bridge.
Un API/mock PASS ne prouve pas leur rendu CSP natif ni l'occlusion/réverbération.

## M5C — mix hybride AC/FMOD

Pendant un stream SoundSim valide/playing en GT86 :

| Événement | Attendu natif |
| --- | --- |
| EngineInt / EngineExt continus | Absents, gain zéro réversible |
| Backfire | Présent quand déclenché |
| Limiter | Présent si événement distinct du bank |
| Transmission / gear | Présents quand déclenchés |
| Pneus / vent | Présents |
| Turbo / flutter éventuels | Conservés, N/A sur GT86 atmosphérique |
| Autres FX du mod | Inchangés, pas de whitelist limitée à trois sons |

Tester **à l'oreille**, en plus des readbacks/mock : aucun doublon du continu,
pas de FX écrasé, équilibre sonore acceptable et absence de saturation finale.
Le repo ne touche qu'à EngineInt/Ext. Si un bank embedde un FX dans ces événements,
le mute entier peut aussi supprimer ce FX : constater la limite, ne pas inventer
une compatibilité universelle ni corriger leurs gains un par un. Gain zéro ne
prouve pas que le calcul natif soit bypassé.

## M5D — latence et robustesse

Essais natifs requis : coup de gaz brutal → réponse sonore ; rapport → réaction
immédiatement cohérente ; pause/reprise ; runtime stoppé/redémarré ; sortie/
réentrée de session ; restauration native automatique quand SoundSim tombe.
Les tests IPC/mock couvrent ces états mais pas l'audibilité de la restauration.

### Capture synchronisée

Le collecteur à1Hz reste utile aux configurations, pas à une mesure de latence.
Nouvelle trace **opt-in**, MMF séparée136octets : observation du RPM/throttle/gear
au runtime, QPC début/end render/publication, séquences/génération, faults/late/
allumage. Les ABIs state192/status368/audio restent inchangées. Pas d'IO disque
sur le render. Cette horloge ne mesure pas le délai pédale physique→publisher AC.

```powershell
.\scripts\stop_runtime.ps1
.\scripts\start_runtime.ps1 -Audit
# Lancer la GT86 en live, attendre que le bridge soit valid/playing.
.\build\Release\soundsim-m5-capture.exe --seconds 20 --output artifacts/m5/latency-take-01
```

Capture déclenchée explicitement, jamais au démarrage du runtime. Aucun micro,
aucune lecture audio, aucun upload. Refus sans GT86 live + runtime normal/audit
frais. Elle enregistre **le mix système de la sortie par défaut**, autres apps
incluses : les rendre silencieuses, pas de données privées à publier. Vérifier
qu'AC utilise cette même sortie Windows ; sinon le loopback ne mesure pas son audio.
Le dossier
doit être nouveau ; plafond64MiB, durée1..120s. Débuter les coups de gaz après
quelques secondes, loin du limiteur/backfire pour éviter les confusions.

Fichiers : `loopback.wav` IEEEfloat32 non normalisé, `audio-packets.csv` (QPC
100ns/positions/flags), `runtime-telemetry.csv`, `capture.json`.
Le QPC WASAPI est converti depuis100ns, celui du runtime depuis sa fréquence ;
pas de comparaison aveugle de timestamps de domaines différents.
[Contrat GetBuffer Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer),
[portée du loopback](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

### Mesure du décalage

```powershell
python scripts/analyze_m5_latency.py artifacts/m5/latency-take-01
```

Produit `aligned-runtime.csv` : choisir l'événement throttle/RPM et son heartbeat,
puis repérer manuellement **le même** changement sonore dans le WAV (éditeur audio).
La table respecte les timestamps par paquet ; une lacune n'est pas remplie par
un faux temps WAV. Mesurer ensuite avec l'indice de frame audio annoté :

```powershell
# Remplacer les valeurs par celles de LA capture ; pas des mesures pré-remplies.
python scripts/analyze_m5_latency.py artifacts/m5/latency-take-01 --input-heartbeat 1234 --audio-frame 480000
```

`--generation` désambiguïse un heartbeat après restart ; `--max-ms` est un seuil
explicitement choisi, sans default ni PASS automatique M5. Les sorties existantes
ne sont pas écrasées. Les flags timestamp/discontinuité dans la fenêtre, événements
fault/non-running, indices invalides et délai négatif sont rejetés.

La mesure est **télémétrie observée runtime→onset loopback annoté**, plus les étapes
render/publication. Elle inclut la réponse source/CSP/mix et dépend du choix d'onset.
Ce n'est ni un curseur consommateur, ni le délai total pédale→oreille. Vérifier
que l'onset appartient au moteur moddé et non à un FX/autre app ; refaire plusieurs
coups de gaz, consigner dispersion/incertitude et acceptation à l'oreille.
Aucune valeur de latence n'a encore été mesurée dans une session native M5.

## Journal et gate final

Conserver pour chaque case : PASS/FAIL/PENDING/N/A justifié, prise/capture,
conditions/réglages/versions, observation précise, validateur. Une fiche vierge
est dans `m5-results-template.md`. Les snapshots complémentaires :

```powershell
python scripts/capture_m5.py --label m5b-flyby --seconds 120
```

Les captures personnelles restent sous artifacts ignorés ; aucune upload CI.
Un vieux snapshot immobile, `eventValid=true` ou une CI verte ne sont pas des
preuves perceptives. Chaque bloc doit réussir indépendamment :

- M5A : dynamique/allumage, rapports, décélération/reprise et écoute sans défaut ;
- M5B : fly-by, Doppler/distance/orientation sans rupture ;
- M5C : FX conservés, continu natif absent, mix acceptable ;
- M5D : latence mesurée/acceptée et pause/restart/session/fallback PASS.

À ce moment seulement : **GT86 reference implementation complète**.
Admission indépendante et autres moteurs restent après ce gate.
