# M5 — fiche de session native (à copier dans artifacts/m5)

Date / validateur :
Commit runtime / version bridge / version CSP :
Voiture : ks_toyota_gt86 ; circuit/session live :
Volumes AC / sortie Windows / autres mods :
Gain / guard / cabine (trim,mids,highs,crossover,body) :
Spatial/cone/distance/source offset / caméra :
Captures associées :
Procédure : docs/m5-test-protocol.md ; dossier/json/verdict : scripts/m5_session.py
Le JSON contient 23 cases précises, les notes ci-dessous peuvent être jointes comme
preuve détaillée mais ne remplacent pas leur saisie PASS/FAIL/PENDING.

| Bloc / essai | Résultat initial | Preuve / observation / défaut |
| --- | --- | --- |
| M5A source/allumage multi-cadences | PENDING, joindre rapport automatisé | |
| M5A accélération800→7400 | PENDING | |
| M5A rapports pleine charge | PENDING | |
| M5A décélération/reprise | PENDING | |
| M5A aucun clic/trou/saut de hauteur | PENDING | |
| M5B fly-by approche/passage/éloignement | PENDING | |
| M5B avant/arrière de l'échappement | PENDING | |
| M5B proche/loin/proche | PENDING | |
| M5B Doppler continu, pas de saut volume/pose | PENDING | |
| M5B transitions cabine/externe | PENDING | |
| M5C continu EngineInt/Ext absent | PENDING | |
| M5C backfire/limiter distinct | PENDING | |
| M5C transmission/gear | PENDING | |
| M5C pneus/vent/autres FX inchangés | PENDING | |
| M5C turbo/flutter éventuels | N/A GT86 atmosphérique, vérifier cible | |
| M5C équilibre/headroom final | PENDING | |
| M5D coup de gaz/réaction rapport | PENDING | |
| M5D latence mesurée, dispersion/incertitude | PENDING | |
| M5D seuil choisi + acceptation perceptive | PENDING | |
| M5D pause/reprise | PENDING | |
| M5D runtime stop/restart + restauration native | PENDING | |
| M5D sortie/réentrée en session | PENDING | |
| M5D panne/perte stream -> fallback natif | PENDING | |
| M5D 10min de cadence normale sans nouveau late/fault/anomalie | PENDING | |

Remplacer PENDING uniquement par preuve explicite, pas par impression globale.
PASS de chaque bloc : M5A ___ / M5B ___ / M5C ___ / M5D ___
GT86 reference implementation complète : NON (tant qu'une case obligatoire reste ouverte).
