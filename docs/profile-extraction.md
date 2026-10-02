# Extraction progressive du profil FA20

Première tranche : le runtime charge réellement
`profiles/engines/subaru_fa20.yaml` au démarrage. L'ancien `makeFa20Baseline()`
devient un appel au chargeur, pas une seconde définition du moteur. Le fichier
est requis : absence, YAML invalide, version inconnue, clé manquante/dupliquée/
inconnue, nombre non fini/hors bornes et moteur non supporté sont rejetés.
Un seul document, taille maximale 64KiB. Validation aussi pour les profils
construits directement en C++. Parser yaml-cpp 0.8.0 épinglé au commit
`f7320141120f720aecc4c32be25586e7da9eb978`, notice MIT conservée.

Les champs chargés : identité FA20, géométrie, ordre d'allumage, valeurs RPM
par défaut et `reference_audio` (volume source, leveler, HF mix, noise, jitter,
convolution et gain de smooth_39). Le nom d'IR reste limité à smooth_39, avec
son chemin upstream audité ; pas de résolveur d'assets arbitraires pour l'instant.
Le preset dry désactive toujours noise/jitter/convolution ; legacy conserve son
DSP historique. La sortie gain8/EQ cabine reste indépendante dans CSP.

```powershell
.\scripts\start_runtime.ps1 -Profile profiles/engines/subaru_fa20.yaml
```

Ou `soundsim-runtime --profile chemin.yaml`. Le chemin par défaut est ancré
sur le checkout à la compilation, comme l'IR actuelle. Ceci est un outil de
développement, pas encore un paquet redistribuable autonome. Le profil est
chargé une fois par lancement ; redémarrer pour changer ses paramètres. Le
render n'effectue ni lecture de fichier ni parsing. AC reste l'autorité RPM ;
ces valeurs par défaut ne deviennent pas une coupure moteur indépendante.

Les culasses, conduits, fuel, cames, courbes de débit/avance et le résolveur
voiture restent spécialisés en C++. Le YAML voiture n'est toujours pas chargé.
Ce n'est donc **pas** un système multi-moteurs achevé ni un interpréteur `.mr`.
Extraire ensuite ces groupes progressivement, une tranche à la fois, après M5.

## Contrôle sonore

Le test de profil couvre les entrées erronées et une valeur modifiée. Le test
headless vérifie qu'un changement de volume source change le PCM sans changer
la combustion. Les trois WAV d'audit sont rendus en série avec srand(12345).
Comparer avant/après sur la même plateforme/toolchain :

```powershell
python scripts/compare_source_pcm.py --baseline artifacts/audio-audit --candidate build/audio-audit-test --output artifacts/profile-audit/pcm-regression.json
```

Aucune normalisation, compensation de gain ou comparaison des timings CPU.
Première tranche : les 573300 samples de chacun des trois presets correspondent
exactement à la base précédente sur le poste MSVC Release. Le rapport complet
reste dans les artifacts ignorés. Cela ne prouve ni fidélité réelle GT86 ni
identité à l'application Community Edition. La CI garde des metrics offline ;
un hash exact entre toolchains différentes n'est pas imposé sans qualification.
