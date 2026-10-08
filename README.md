# CCN-DTN Simulation

Cette expérience vise à étudier la faisabilité de la mise en place d’un réseau de digital twins (DTN) pour améliorer les performances d’un réseau CCN. En prenant comme point de départ l’allocation dynamique du cache, elle examine si le digital twin (DT) peut adapter la répartition du cache d’un réseau CCN aux variations des requêtes, améliorer les performances du réseau et réduire la consommation énergétique totale en tenant compte de son propre surcoût.

## 1. Environnement

Ubuntu 20.04 sur VMware16 + Python 3.8 + ndnSIM2.9  
Pour produire des graphiques, installer matplotlib>=3.5.

## 2. Structure du projet et description des fichiers

```text
ccn-dtn-v2/
├── requirements-optional.txt    # Dépendances pour les graphiques
├── config/energy.conf           # Coefficients énergétiques des expériences
├── topologies/                  # topologies : tree.txt 
├── scripts/                     # Scripts d’installation, d’exécution, d’analyse et de tracé
└── scratch/                     # Code source C++ de la simulation
```

### Scripts

| Fichier                      | Description                                                                                                                                                             |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `scripts/install.py`         | Copie les fichiers sources dans ns-3/scratch|
| `scripts/run_experiments.py` | Compile et exécute le code de simulation selon les paramètres définis                                                                                                   |
| `scripts/analyze.py`         | Vérifie les sorties et génère les résultats statistiques de synthèse                                                                                         |
| `scripts/plot.py`            | Produit les diagrammes en barres de la consommation énergétique par composante et les courbes des quotas de cache de chaque groupe                                  |
| `scripts/sensitivity.py`     | Évalue l’impact du coût énergétique du DT sur l’énergie totale et les économies, sans relancer les simulations|

### Fichiers sources

| Fichier                   | Description                                                                                                           |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `ccn-dtn-v2.cpp`          | Point d’entrée : initialise et exécute la simulation                                                                  |
| `experiment.hpp`          | Déclaration de la classe `Experiment`: Coordonne l’ensemble de la simulation : réseau, Digital Twins, décisions de contrôle, mesures et résultats.|
| `experiment-config.hpp`   | Tous les paramètres de ligne de commande des scénarios, leurs valeurs par défaut et leur validation                   |
| `experiment-state.hpp`    | Compteurs cumulés, instantanés par fenêtre, historique des LO et mise à jour EWMA                                     |
| `experiment-network.hpp`  | Création de la topologie, attribution des rôles, fonctions de rappel du trafic et ajustement des quotas de cache      |
| `experiment-control.hpp`  | Interrogation périodique de l’état des routeurs par le nœud D et envoi des commandes de contrôle du cache             |
| `experiment-decision.hpp` | Simulation des allocations candidates et prise de décision dans le nœud D à partir de l’historique des LO             |
| `experiment-metrics.hpp`  | Export des données CSV : énergie, utilisation du cache, etc.                                                          |
| `workload.hpp`            | Prégénération des séquences de requêtes des consommateurs à partir des paramètres de trafic et de la graine aléatoire |
| `replay-consumer.hpp`     | Application Consumer personnalisée                                                                                    |
| `control-app.hpp`         | Application Control personnalisée                                                                                     |
| `measured-lru.hpp`        | Politique LRU du cache réel                                                                                           |
| `twin-model.hpp`          | Création du modèle de digital twin utilisé pour les simulations et les décisions internes au nœud D               |
| `dtn-core.hpp`            | Contient les fonctions principales d’allocation du cache, de prédiction du trafic et de calcul énergétique.                                                                                                                      |
| `energy-config.hpp`       | Lecture de la configuration énergétique                                                                               |
| `official-trace.hpp`      | Analyse des sorties des Tracers                                                                                       |

## 3. Exemple d’utilisation

Ouvrir le dossier du projet de simulation. En supposant que les sources de ns-3 se trouvent dans `~/Desktop/ndnSIM/ns-3`, exécuter les commandes suivantes :

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-smoke \
  --workload changing --duration 12 --switch-time 6 \
  --periods 5 --seeds 1
```

Cette commande compile automatiquement le projet et exécute une fois chacun des modes. Après l’exécution, lancer :

```bash
python3 scripts/analyze.py ./results-smoke
python3 scripts/plot.py ./results-smoke
```
Analyse les résultats de simulation et génère les fichiers statistiques et génère les graphiques à partir des résultats analysés.

Remarque : plot.py nécessite matplotlib>=3.5.

### Modes expérimentaux

| Mode        | Fonction                                                                      |
| ----------- | ----------------------------------------------------------------------------- |
| `static`    | Quotas fixes et uniformes de 50/50/50, servant de référence                   |
| `uneven`    | Quotas fixes et non uniformes de 80/20/50                                     |
| `noop`      | Exécute les communications de contrôle et le modèle, mais conserve les quotas 50/50/50 |
| `heuristic` | Ajuste les quotas selon le taux de requêtes et la longueur des chemins        |
| `shadow`    | Évalue et ajuste les quotas en rejouant les requêtes historiques              |
| `adaptive`  | Adapte la période de mise à jour à partir du fonctionnement de shadow         |
| `manual`    | Modifie manuellement les quotas à 30 et 60 secondes, pour le débogage         |

### Paramètres d’exécution par lots

| Paramètre                   | Valeur par défaut               | Utilisation                                                                                                                        |
| --------------------------- | ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `--output`                  | `results/`                      | Répertoire de sortie, par exemple `--output ./results-test` ; utiliser un autre nom lors d’une nouvelle exécution                  |
| `--seeds`                   | `1`                             | Nombre de graines aléatoires par groupe ; `--seeds 10` exécute une fois chacune des graines de 1 à 10                              |
| `--modes`                   | Les six modes autres que manual | Modes à exécuter, par exemple `--modes static shadow`                                                                              |
| `--workload`                | `changing`                      | `stable` conserve la charge ; `changing` échange les charges forte et faible des deux consommateurs à l’instant spécifié           |
| `--periods`                 | `5`                             | Intervalle, en secondes, entre les mises à jour des quotas ; `--periods 1 5 10 20` teste séparément ces quatre intervalles         |
| `--duration`                | `120`                           | Durée simulée de génération des requêtes applicatives, en secondes                                                                 |
| `--switch-time`             | `60`                            | Instant d’échange des charges, en secondes ; uniquement pour changing, strictement supérieur à 0 et inférieur à duration           |
| `--energy-config`           | `config/energy.conf`            | Fichier de configuration énergétique à utiliser                                                                                    |
| `--topology`                | `topologies/tree.txt`           | Fichier de topologie réseau à utiliser                                                                                             |
| `--adaptive-initial-period` | `5`                             | Délai initial, en secondes, avant la première mise à jour en mode adaptive ; les intervalles suivants sont ajustés automatiquement |
| `--min-period`              | `1`                             | Intervalle minimal entre mises à jour, en secondes                                                                                 |
| `--max-period`              | `20`                            | Intervalle maximal entre mises à jour, en secondes                                                                                 |
| `--horizon`                 | `30`                            | Durée de l’historique récent des requêtes utilisé par le modèle pour évaluer les quotas, en secondes                               |
| `--dry-run`                 | Désactivé                       | Liste uniquement les tâches prévues, sans les exécuter                                                                             |
| `--help`                    | —                               | Affiche l’aide des paramètres                                                                                                      |

Les intervalles multiples ne concernent que **‘noop’**, **‘heuristic’** et **‘shadow’**. 
Les modes **'static'**, **'uneven'** et **'manual'** ne sont pas répétés pour chaque intervalle ; **'adaptive'** utilise son propre intervalle initial. Tous les intervalles définis doivent être compris entre min-period et max-period.

## 4. Étapes expérimentales

### 1 : Test de bon fonctionnement (6 simulations)

Vérifier le fonctionnement des scripts et de l’environnement en exécutant chaque mode une fois :

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-smoke \
  --workload changing --duration 12 --switch-time 6 \
  --periods 5 --seeds 1
python3 scripts/analyze.py ./results-smoke
python3 scripts/plot.py ./results-smoke
```

### 2 : Comparaison des six modes (60 simulations)

Comparer les six modes par défaut, chacun avec 10 graines :

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-changing-5 \
  --topology ./topologies/tree.txt \
  --workload changing --duration 120 --switch-time 60 \
  --periods 5 --seeds 10
python3 scripts/analyze.py ./results-v2-changing-5
python3 scripts/plot.py ./results-v2-changing-5
```

### 3 : Comparaison des intervalles de mise à jour (150 simulations par type de charge)

Charge variable :

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-periods-changing \
  --workload changing --duration 120 --switch-time 60 \
  --periods 1 5 10 20 --seeds 10
python3 scripts/analyze.py ./results-v2-periods-changing
python3 scripts/plot.py ./results-v2-periods-changing
```

Charge stable :

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-periods-stable \
  --workload stable --duration 120 \
  --periods 1 5 10 20 --seeds 10
python3 scripts/analyze.py ./results-v2-periods-stable
python3 scripts/plot.py ./results-v2-periods-stable
```

### 4 : Recalcul des résultats pour différents coûts du DT

À partir des résultats de la deuxième expérience, multiplier le coût du DT par 0.25, 0.5, 1, 2 et 4, sans relancer la simulation :

```bash
python3 scripts/sensitivity.py ./results-v2-changing-5 \
  --scales 0.25 0.5 1 2 4
```

Le fichier `sensitivity.csv` est généré. Pour analyser une autre expérience, remplacer le répertoire des résultats par celui qui convient.

### Facultatif : Modification manuelle des quotas de cache

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-manual --modes manual \
  --workload changing --duration 70 --switch-time 35 \
  --periods 5 --seeds 1
python3 scripts/analyze.py ./results-manual
python3 scripts/plot.py ./results-manual
```

Les quotas passent à 80/20/50 à 30 secondes, puis à 20/80/50 à 60 secondes.

## 5. Description des fichiers de sortie

Chaque groupe d’expériences est enregistré dans le répertoire indiqué par `--output`. Chaque simulation dispose d’un sous-répertoire distinct, selon le format suivant :

```text
results-v2-changing-5/
├── topology.txt                  # Copie de la topologie utilisée pour ce lot
├── energy.conf                   # Copie des coefficients énergétiques de ce lot
├── source_versions.json          # Versions des dépendances et empreinte du source d’entrée
├── static_changing_p5_seed1/      # Résultats d’une simulation
├── shadow_changing_p5_seed1/
├── ...
├── all_summaries.csv              # Généré après l’analyse
├── comparison.csv
├── paired.csv
├── energy_breakdown.png           # Généré après le tracé
└── sensitivity.csv                # Généré après l’analyse de sensibilité
```

### Sorties de chaque simulation

| Fichier          | Contenu et utilisation                                                                                           |
| ---------------- | ---------------------------------------------------------------------------------------------------------------- |
| `run.log`        | Journal d’exécution du script                                                                                    |
| `parameters.txt` | Mode, graine, paramètres du trafic et du modèle, sources statistiques et contenu de la configuration énergétique |
| `topology.txt`   | Copie de la topologie lue pour cette simulation                                                                  |
| `requests.csv`   | Séquence de requêtes prégénérée pour cette simulation                                                            |
| `deliveries.csv` | Résultat de chaque requête : réussite ou expiration du délai |
| `timeline.csv` | État de chaque nœud chaque seconde : quota, occupation du cache, trafic applicatif/de contrôle et compteurs de cache/opérations. Les octets et compteurs sont cumulés.|
| `allocations.csv` | Modifications effectives des quotas des trois routeurs |
| `lo_history.csv` | Historique des états des LO |
| `control.csv` | Journal des événements de contrôle |
| `predictions.csv` | Taux de succès de cache prédits/observés et erreurs |
| `network-faces.csv` | Nœuds et identifiants des faces réseau (Face ID) |
| `l3-rate-trace.txt` | Journal L3RateTracer |
| `app-delay-trace.txt` | Journal AppDelayTracer |
| `l2-drop-trace.txt` | Statistiques de pertes de paquets L2RateTracer, pour observer la congestion des files d’attente |
| `traffic-audit.csv` | Vérification de la cohérence entre le trafic des événements personnalisés et L3RateTracer |
| `summary.csv` | Résultats statistiques de la simulation courante |
| `cache_quotas.png` | Graphique en escalier des quotas R1/R2/R3, généré après le tracé |

### Sorties de l’analyse et du tracé

| Fichier | Usage et remarques |
|---|---|
| `all_summaries.csv` | Regroupe les résultats summary de toutes les groupes de simulations|
| `comparison.csv` | Statistiques et comparaisons par groupe de simulations |
| `paired.csv` | Comparaison de chaque mode autre que static avec le mode static ayant les mêmes workload|
| `energy_breakdown.png` | Diagramme en barres de la consommation énergétique de chaque groupe de simulations |
| `sensitivity.csv` | Résultats de l’analyse pour différents coûts énergétiques du DT |

### Principaux résultats à consulter

- Performances globales : `comparison.csv`, notamment consommation énergétique, taux de réussite et délai moyen.
- Économies d’énergie par rapport à static : `paired.csv` ; une valeur positive de `mean_saved_j` indique une économie.
- Ajustement du cache : `cache_quotas.png` et `allocations.csv`.
- Processus de contrôle et intervalles réels de mise à jour : `control.csv`.
- Ensemble des indicateurs de chaque exécution : `summary.csv` ; la consommation énergétique est une estimation du modèle utilisant les coefficients configurés.
