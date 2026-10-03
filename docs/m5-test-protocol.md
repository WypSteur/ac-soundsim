# Protocole unique de validation M5 — GT86

Objectif : qualifier la GT86 sur **les quatre blocs**, sans confondre source,
transport, acoustique CSP et mix. Prévoir environ 45–60 min d'essais, puis le temps
d'annotation de la latence. Ce document est la procédure pratique ; les preuves
techniques et limites sont dans `m5-validation.md` et `runtime-cadence.md`.

L'implémentation/outillage est prêt pour cette qualification. **M5 n'est pas
validée** avant les résultats natifs. Le retour « le son est revenu » confirme
la récupération de la base après le fix de cadence, pas chaque case ci-dessous.

## 0 — Préparation, une seule fois

1. Utiliser le build Release et le bridge **0.0.11**, session GT86 **live**, pas
   un replay. Circuit avec une ligne droite et une zone calme, mêmes conditions.
2. Rétablir tes réglages cabine habituels ; ils restent session-only. Référence
   acceptée précédemment : gain 8, guard ON, trim -8 dB, mids -8 dB, highs -24 dB,
   crossover 2087 Hz, body 0 dB. Ce n'est pas une calibration GT86 stock ni une
   obligation de nouveaux defaults. Noter les valeurs effectivement utilisées.
3. Garder `3D CSP`, camera override `auto`, traitement cabine ON, peak guard ON,
   remplacement moteur natif ON. Conserver les volumes AC/Windows/FMOD/mods.
4. Autres applications audio silencieuses ; vérifier qu'AC sort sur la sortie
   Windows par défaut si tu enregistres le loopback. Aucun micro n'est capturé.
5. Attendre 5 s après entrée/reprise avant le baseline. Bridge : `running`,
   `valid/playing=true/true`, `Producer health: healthy`, moteur natif `MUTED`.
   Les RPM AC/crank doivent correspondre. Si ce n'est pas le cas, ne pas tester.
6. Ne pas lancer de compilation/test CPU pendant la conduite. Toute baisse de
   gain/guard/3D/cabine doit être consignée ; refaire les cas concernés ensuite.

Depuis la racine du repo, avant de lancer AC :

```powershell
.\scripts\start_runtime.ps1 -Audit
# Si le runtime est deja lance normalement, le stopper puis le relancer avec -Audit.
python scripts/m5_session.py new --label gt86-m5 --source-gate build/dynamic-gate.csv
```

Le script imprime un nouveau dossier. Pour les commandes suivantes, remplacer
la valeur ci-dessous par **ce dossier réel**, et garder ce terminal ouvert :

```powershell
$m5Run = 'C:\chemin\reel\artifacts\m5\prise-imprimee'
```

Dans `session.json`, renseigner operator, versions AC+CSP, circuit, sortie Windows,
volumes AC et réglages bridge. Hashes/commit de travail sont collectés ; un dossier
créé avec des changements non committés ne peut pas fermer le gate final.
`results.json` contient 23 cases ; seule la source peut être prévalidée par son
rapport de 16 scénarios. Toutes les observations natives commencent PENDING.

Faire un snapshot après préparation, puis avant/après chaque bloc :

```powershell
python scripts/m5_session.py snapshot $m5Run --label baseline
```

Il refuse les rapports anciens (>2,5 s), incomplets ou d'une autre version/cible.
Les captures/snapshots restent locaux sous artifacts ignorés ; aucun upload.

## 1 — M5A : dynamique moteur (8–10 min)

| Essai / ID | Action | PASS attendu |
| --- | --- | --- |
| A.acceleration | 3 montées rapides 800→7400 rpm, puis écoute cabine/externe | Montée continue, pas de trou/saut de hauteur/craquement |
| A.gears | 3 séquences de rapports pleine charge | Coupure/chute RPM/reprise immédiatement cohérentes |
| A.reprise | 3 décélérations franches puis remise des gaz | Pas de clic/trou ; reprise propre |
| A.clean | Réécouter les séquences aux deux caméras | Aucun défaut dynamique anormal |
| A.source | Rapport automatisé 30/60/90/120/144/165/240 Hz + jitter | 16 scénarios, 0 anomalie d'allumage/guard |

Les cadences ci-dessus sont celles du **producteur synthétique**, pas une preuve
qu'AC publie/rend chaque état à 240 Hz. En jeu, faire au moins un essai à ta cadence
FPS habituelle et un à 90 FPS si possible ; noter les FPS réels. Il n'est pas
nécessaire de refaire les sept caps en conduite pour recopier la CI.

Noter les compteurs late au début/après 5 s de stabilisation et à la fin. Aucun
nouveau late/fault/anomalie sur les séquences stables. Une impulsion ne se compte
pas à l'oreille : l'écoute et les compteurs sont des preuves complémentaires.
Late/fault sont visibles au bridge. L'allumage est consigné dans le log runtime
à 1 Hz (`firing_sequence_anomalies=0`) et dans la trace QPC de latence ; ce n'est
pas une mesure de latence à 1 Hz. Conserver les logs des segments/restarts testés :

```powershell
Copy-Item -Path 'logs\runtime\*' -Destination "$m5Run\evidence"
```

Faire cette copie après le bloc D aussi. Elle copie uniquement les logs locaux du
runtime, pas le répertoire global AC. Relever toute anomalie avant un reset.

```powershell
python scripts/m5_session.py snapshot $m5Run --label a-end
```

## 2 — M5B : propagation CSP (10 min)

Mettre une caméra libre/piste **immobile dans le monde**, ne suivant pas la
voiture. Pas de replay, pas de 2D audit. Effectuer les passes en conduite live.

| Essai / ID | Action | PASS attendu |
| --- | --- | --- |
| B.flyby | 2 passages par sens, approche→passage→éloignement | Source reste sur la voiture, aucune téléportation de volume/position |
| B.doppler | Passer à vitesse et RPM à peu près stabilisés (noter valeurs) | Variation au passage continue, distincte d'une accélération moteur |
| B.cone | Au ralenti/stable, observer avant, côté et derrière à distance comparable | Directivité arrière cohérente, pas de rupture grossière |
| B.distance | Proche→loin→proche sur un trajet continu | Atténuation continue ; retour propre, sans seuil marche/arrêt |
| B.camera | 3 transitions cabine→externe→cabine | Cabine filtrée, extérieur retrouvé ; pas de double source ni clic |

Le cône peut donner des niveaux différents selon l'angle : ne pas exiger le
même volume devant et derrière. Ne pas fabriquer un Doppler supplémentaire dans
SoundSim pour corriger une erreur de pose/CSP. Relever tout défaut précis.

```powershell
python scripts/m5_session.py snapshot $m5Run --label b-end
```

## 3 — M5C : mix hybride (5–8 min)

Comparer **natifs seuls** vs SoundSim, sans changer les volumes complémentaires.
Pour natifs seuls, utiliser `Mute SoundSim test source` : le bridge doit libérer
EngineInt/Ext. Réactiver avec `Enable SoundSim test source`. Le bouton `Restore
native engine` seul laisse SoundSim audible : ce serait un double moteur, pas
une référence native seule.

| Essai / ID | Action | PASS attendu |
| --- | --- | --- |
| C.engine | A/B natifs seuls puis SoundSim, cabine/externe | Continu natif absent pendant SoundSim, pas de doublon |
| C.backfire | Déclencher 3 fois si la configuration le permet | FX présent quand déclenché, niveau non modifié |
| C.limiter | 3 courts passages au limiteur | FX natif distinct conservé, équilibre acceptable |
| C.gear | Rapports/coupures de charge | Transmission/gear conservés |
| C.other | Rouler, provoquer bruit pneus/vent et autres FX existants | FX inchangés, aucune whitelist limitée à 3 événements |
| C.mix | Charge élevée, proche/externe et cabine | Pas de saturation/craquement/écrasement gênant du mix |

GT86 atmosphérique : turbo/flutter **N/A**, ne pas inventer un son à tester.
Si un FX ne se déclenche pas, garder PENDING et vérifier les conditions ; ne pas
le déclarer conservé sans écoute. S'il est intégré au bank EngineInt/Ext, le mute
peut aussi le supprimer : enregistrer la limite/FAIL, pas une fausse compatibilité
universelle. Le meter source/guard n'atteste pas le headroom du mix global.

```powershell
python scripts/m5_session.py snapshot $m5Run --label c-end
```

## 4 — M5D : robustesse + latence (15 min + annotation)

Ne pas redémarrer tout Windows ni terminer d'autres applications. Stop/restart
ci-dessous ne cible que le runtime normal SoundSim. Une capture ne démarre jamais
automatiquement ; ne la lancer qu'après avoir réduit les autres apps au silence.

| Essai / ID | Action | PASS attendu |
| --- | --- | --- |
| D.response | 3 coups de gaz francs + 2 rapports, sans limiteur/backfire | Réponse perceptivement immédiate/cohérente |
| D.pause | 3 pauses/reprises | Freeze/silence propres, source reprend sans ancien flux |
| D.restart | 3 stop/restart par scripts | Natifs reviennent ; nouveau stream SoundSim reprend sans doublon |
| D.session | Quitter/revenir en session 2 fois | Identité/reprise valides, réglages rétablis/notés |
| D.fallback | Stop SoundSim ; optionnellement terminer UNIQUEMENT ce runtime via le gestionnaire de tâches | Natif restauré après perte heartbeat ; pas de silence permanent |
| D.cadence | 10 min de roulage/charge normale après baseline | 0 nouveau late/fault/anomalie, health healthy, pas de saccades |

```powershell
.\scripts\stop_runtime.ps1
# Attendre le retour des natifs au bridge ET a l'oreille.
.\scripts\start_runtime.ps1 -Audit
```

En cas de garde `PCM stalled`/`cadence failure`, SoundSim se coupe et les natifs
reviennent. La garde reste latched jusqu'au nouveau runtime/reset moteur ou au
bouton explicite `Retry SoundSim after transport fault`. C'est une protection
contre un flux cassé, **pas** une tolérance de 100 ms d'erreurs/s pour valider M5.
Une erreur transitoire de restauration native reste `RESTORE PENDING` et est
réessayée ; si elle persiste, le fallback échoue et D.fallback ne peut pas PASS.
Ne pas injecter volontairement une charge CPU excessive sur le PC pour provoquer
ce cas : son fonctionnement logique est couvert par le mock.

### D.latency — prise puis cinq annotations minimum

En session stable, lancer la commande ci-dessous, attendre 3 s, faire 3 coups de gaz
séparés puis 2 réactions de rapport nettes. Noter l'événement ; ne pas le confondre
avec vent, limiter ou backfire. Choisir une durée permettant ces cinq événements :

```powershell
.\build\Release\soundsim-m5-capture.exe --seconds 45 --output "$m5Run\captures\latency-01"
python scripts/analyze_m5_latency.py "$m5Run\captures\latency-01"
```

La capture est le **mix système de la sortie par défaut**, pas un micro/source
solo. Examiner `aligned-runtime.csv` et le WAV dans un éditeur audio. Pour chaque
entrée RPM/throttle/gear choisie, identifier manuellement le **même** onset moteur.
Créer `annotations.csv` dans la prise avec ce header, puis 5 lignes réelles :

```csv
generation,heartbeat,audio_frame,uncertainty_frames,comment
```

`audio_frame` = indice à partir de zéro de frame multicanal, pas indice de sample par
canal. Si l'éditeur affiche des secondes, multiplier par le sample_rate de
capture.json. `uncertainty_frames` documente l'incertitude de placement. Un onset
ambigu, sans changement net ou mélangé à un FX ne constitue pas une mesure valide.

```powershell
python scripts/analyze_m5_latency.py "$m5Run\captures\latency-01" --annotations "$m5Run\captures\latency-01\annotations.csv" --output "$m5Run\evidence\latency.json"
```

Le rapport fournit minimum/médiane/p95/max, incertitudes et temps render/publish.
Il rejette un restart/fault/pause/late runtime ou un timestamp/discontinuité audio dans
la fenêtre. C'est **entrée observée runtime→onset loopback**, pas pédale→oreille.
Ne pas annoncer comme une garantie de latence le hint 40 ms ou la ring 2,56 s.

Choisir l'acceptation APRÈS mesure : renseigner `latency_report` avec
`evidence/latency.json`, `latency_accepted=true` et des notes d'acceptation dans
results.json. Facultatif : `--max-ms` avec un seuil explicite choisi, pas imposé
par le code. Si ce seuil est dépassé, le gate reste ouvert même avec une note PASS.

## 5 — Verdict final, une seule fiche

Dans `results.json`, chaque case obligatoire : PASS/FAIL/PENDING, note précise,
références relatives de preuve existante dans le dossier (snapshots, captures,
notes détaillées, vidéo éventuelle). Pour les cases auditives, une note d'écoute
doit décrire ce qui a réellement été entendu ; un screenshot `playing=true` seul
ne suffit pas. Conserver toutes les valeurs/fréquences/version/conditions.

```powershell
python scripts/m5_session.py report $m5Run --output "$m5Run\verdict-01.json"
```

Exit 2 + `OPEN` tant qu'il manque une case, une preuve, la configuration ou les
cinq mesures/acceptation de latence. Exit 1 si dossier invalide. Exit 0 +
`PASS_DECLARED_WITH_EVIDENCE` seulement si A/B/C/D sont complets. Ce programme
vérifie la **complétude des déclarations humaines**, pas l'acoustique à leur place.

Un FAIL bloque le bloc concerné ; corriger et refaire ses cas impactés. Ne pas
réutiliser les observations d'un ancien build pour une modification acoustique.
Seulement après les quatre PASS : GT86 référence complète, puis extraction des
paramètres et admission indépendante. Aucun deuxième moteur/son d'admission
fabriqué à partir d'un filtre de l'échappement n'est ajouté pendant M5.
