# M5 — qualification de la GT86 en jeu

Statut : **ouverte**. La base cabine/extérieur 0.0.10 est acceptée à l'écoute,
pas la qualification complète de conduite, de propagation ou de latence.
La CI Windows est hors jeu : elle n'installe ni AC, ni CSP, ni FMOD propriétaire.

## Référence à préserver

PCM source FA20D public + smooth_39, master source 0.25, sortie bridge gain8,
peak guard actif. Les réglages cabine restent ceux de la session : la dernière
référence utilisateur est trim -8dB, mids -8dB, highs -24dB / 2087Hz, body 0dB.
Ce ne sont pas des nouveaux défauts livrés ni une calibration GT86 d'origine.
Noter les sliders réels avant chaque essai : ils ne sont pas persistés.

## Procédure manuelle

Utiliser une session **live** avec `ks_toyota_gt86`. Le replay AC est volontairement
muté par la sécurité du bridge : il ne permet donc pas de qualifier les fly-by.
Pour ceux-ci, utiliser une caméra piste/free fixe pendant une conduite live.
Garder les volumes AC et les autres mods identiques ; ne pas régler leurs FMOD.

| Essai | À écouter / observer | Résultat actuel |
| --- | --- | --- |
| Ralenti, intérieur puis extérieur, A/B cabine | Pas de saut de niveau brutal, source non tassée | Base acceptée par l'utilisateur |
| Accélération rapide 800→7400 RPM | Suivi immédiat, pas d'escalier/clic/perte d'allumage | Écoute formelle à faire |
| Montée des rapports à pleine charge | Coupure et reprise crédibles, aucun trou anormal | À faire |
| Décélération rapide et reprises de gaz | Pas de clic ni répétition/disparition anormale d'impulsion | Limite offline détectée, à traiter |
| Caméra fixe, passage proche dans les deux sens | Doppler cohérent, orientation échappement avant/arrière | À faire en live |
| Éloignement proche→loin→proche | Atténuation continue ; pas de doublon natif | À faire |
| Cabine/externe durant la conduite | Filtrage et transition cohérents, gain contrôlable | Base acceptée, dynamique à faire |
| Backfire, transmission, pneus, autres événements du mod | Toujours audibles et à leurs niveaux d'origine | Politique testée en mock, écoute à faire |
| Pause, reprise, perte du runtime, sortie de session | Restauration native ; pas de silence permanent | Automatisé IPC/mock, natif à confirmer |
| Coups de gaz enregistrés avec télémétrie synchronisée | Retard perceptuel acceptable | Non mesuré |

Noter pour chaque essai : circuit, version CSP, caméra, réglages, vitesse/RPM,
PASS/FAIL et description précise. Une capture audio/vidéo aide à comparer ; aucun
seuil arbitraire de latence n'est déclaré réussi sans mesure synchronisée.

## Collecteur de preuves, sans mutation du jeu

Depuis le projet, lancer avant la séquence de conduite :

```powershell
python scripts/capture_m5.py --label acceleration --seconds 120
python scripts/capture_m5.py --label flyby --seconds 120
```

Chaque invocation crée un dossier distinct sous `artifacts/m5/`, avec snapshots
bruts et résumé JSON. Le collecteur lit seulement le fichier du bridge, ne lance
pas AC et ne change aucun réglage. Le bridge publie environ 1 snapshot/s : un
changement de rapport ou un glitch bref peut lui échapper. Ces fichiers ne
mesurent ni le curseur consommateur, ni les underruns, ni la latence audio.
Le résumé reste explicitement `PENDING_MANUAL_LISTENING_REVIEW`, même s'il n'y a
pas d'erreur. Un snapshot ancien immobile ne prouve pas une nouvelle session.
Les preuves générées sont ignorées par Git ; vérifier chemins locaux et contenu
avant de les partager. Conserver séparément le log runtime de la même session.

## Caractérisation offline et limite identifiée

`soundsim-dynamic-test` alimente le vrai FA20D public par états synthétiques à
60/90/144Hz, tenus constants entre mises à jour, rendu à 150 blocs/s. Il teste
phase continue, autorité RPM, cadence PCM, gaz finis et absence de samples sur
les rails. Il **rapporte**, mais ne certifie pas, le compteur d'anomalies de
séquence d'allumage : ce test de caractérisation n'est pas une clôture M5.

Sur la décélération 7400→800 en 1s à 90Hz, une anomalie est observée vers
t=3.8267s / RPM=1973.33, sans guard des gaz. L'upstream teste le franchissement
de l'angle d'allumage recalculé avec l'avance RPM courante contre l'ancien angle
vilebrequin. Quand l'avance bouge par paliers, le seuil peut traverser cette
borne sans déclenchement : cause compatible avec l'observation, à confirmer par
une trace par étincelle avant correction. Ne pas masquer le compteur ni ajuster
le timbre à l'aveugle ; conserver un A/B PCM et contrôler les étincelles.
Pour cette séquence fixe, les compteurs finaux mesurés en Release sont
respectivement 0 / 1 / 0 à 60 / 90 / 144Hz. Ils ne sont pas un taux d'erreur
général et ne sont pas utilisés comme critères de réussite d'allumage.

Prochaine correction prioritaire : qualifier cette discontinuité d'avance/
déclenchement avec RPM externe, puis refaire les essais dynamiques. **Pas de bus
admission ni de deuxième moteur avant ce point et la validation native M5.**

## Gate de clôture

Tous les essais ci-dessus documentés, anomalies dynamiques comprises ; aucun
défaut audio récurrent ; conservation des événements complémentaires ; versions
et paramètres consignés. Les preuves source, mock et native restent distinctes.
Un `ctest` vert ne suffit jamais à déclarer Doppler, mix final ou M5 validés.
