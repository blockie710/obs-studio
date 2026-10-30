# Default Clustering Configurations

This document describes the default configuration values for hierarchical
clustering in the NexusIO ClusterEngine, plus defaults for all other
clustering algorithms. It covers the hierarchical defaults, when they are
applied, and how to override them.

## Default Values

The following defaults are used when no explicit configuration is provided
for the hierarchical clustering algorithm. Additional defaults for k-means,
DBSCAN, and MinHash LSH are also documented.

### Hierarchical Clustering (primary deliverable for task t_49fb1398)

| Parameter         | Default Value | Description                                                        |
|-------------------|---------------|--------------------------------------------------------------------|
| `linkage`         | `ward`        | Ward's minimum-variance linkage criterion                          |
| `distance_metric` | `euclidean`   | Euclidean (L2) distance                                            |
| `num_clusters`    | `3`           | Target number of clusters to produce after agglomeration           |

### K-Means

| Parameter          | Default Value | Description                                     |
|--------------------|---------------|-------------------------------------------------|
| `k`                | `3`           | Number of clusters                              |
| `max_iterations`   | `100`         | Maximum Lloyd iterations                        |
| `init_method`      | `kmeans++`    | Centroid initialization strategy                 |
| `distance_metric`  | `euclidean`   | Distance metric (inherited)                     |

### DBSCAN

| Parameter          | Default Value | Description                                     |
|--------------------|---------------|-------------------------------------------------|
| `epsilon`          | `0.3`         | Neighborhood radius in normalized feature space |
| `min_points`       | `3`           | Core-point threshold                            |
| `distance_metric`  | `euclidean`   | Distance metric (inherited)                     |

### MinHash LSH

| Parameter               | Default Value | Description                               |
|-------------------------|---------------|-------------------------------------------|
| `num_hash_functions`    | `128`         | Number of hash functions per signature    |
| `lsh_bands`             | `32`          | Number of LSH bands                       |
| `similarity_threshold`  | `0.8`         | Jaccard similarity threshold for pairs    |
| `distance_metric`       | N/A           | MinHash uses similarity_threshold instead |

## Why These Defaults?

### Linkage: `ward`

Ward's method minimizes the total within-cluster variance at each merge step.
It tends to produce balanced, roughly equal-sized clusters, which is the most
sensible default for scene-graph layout where no strong prior shape is assumed.

### Distance Metric: `euclidean`

Ward's method is **mathematically defined only under Euclidean distance**
(see validation rule V5 in `clustering_config_spec.md` §5). Using any other
metric (manhattan, cosine, etc.) with Ward linkage is a semantic error that
the validator rejects at load time. Because `ward` is the default linkage,
`euclidean` is the only valid default metric — choosing otherwise would
guarantee a configuration error on every unconfigured invocation.

### Number of Clusters: `3`

A default of 3 provides a reasonable starting granularity for scene
segmentation: foreground, midground, and background layers, which is the
canonical triage in broadcast compositing workflows. Users working with
different source counts can override freely.

## When Are Defaults Applied?

Defaults are applied in this order (highest precedence last):

1. **In-file defaults** — the schema's `default` fields
   (`clustering_config.schema.json`).
2. **Explicit file values** — values present in the user's JSON/YAML/TOML
   config.
3. **Environment variables** — `NEXUS_CLUSTERING_HIERARCHICAL_*` overrides
   (highest precedence).

When a parameter is **omitted** from the config file and no environment
variable is set, the value from `clustering_defaults.py` is used.

## How to Override the Defaults

### Via configuration file

Provide explicit values in the `hierarchical` block:

```json
{
  "algorithm": "hierarchical",
  "distance_metric": "manhattan",
  "hierarchical": {
    "num_clusters": 5,
    "linkage": "complete"
  }
}
```

### Via environment variables

Environment variables take highest precedence and override both file values
and in-file defaults:

```bash
NEXUS_CLUSTERING_HIERARCHICAL_NUM_CLUSTERS=5
NEXUS_CLUSTERING_HIERARCHICAL_LINKAGE=complete
NEXUS_CLUSTERING_HIERARCHICAL_DISTANCE_METRIC=manhattan
```

### Programmatically (Python)

Import the dataclass or named constants and pass them explicitly:

```python
from clustering_defaults import HierarchicalDefaults, DEFAULT_HIERARCHICAL_LINKAGE

config = HierarchicalDefaults(
    num_clusters=5,
    linkage="complete",
    distance_metric="manhattan",
)

# Or pass a plain dict to hierarchical_clustering() — omitted keys
# fall back to defaults automatically:
from clustering import hierarchical_clustering
labels = hierarchical_clustering(X, config={"linkage": "complete"})
```

Or access the flat dictionary for generic iteration:

```python
from clustering_defaults import DEFAULTS

hier_defaults = DEFAULTS["hierarchical"]
print(hier_defaults["linkage"])        # 'ward'
print(hier_defaults["distance_metric"]) # 'euclidean'
print(hier_defaults["num_clusters"])   # 3
```

Or use the convenience accessor:

```python
from clustering_defaults import get_hierarchical_defaults

hd = get_hierarchical_defaults()
print(hd.linkage, hd.distance_metric, hd.num_clusters)
# ward euclidean 3
```

## Important Validation Notes

- **Ward + non-Euclidean**: Ward linkage with any distance metric other than
  Euclidean is rejected at validation time (rule V5). This is why the default
  metric must be `euclidean`.
- **Num clusters range**: `num_clusters` must be >= 1. The upper bound
  (`num_clusters < n_sources`) is checked at cluster time when the number of
  input sources is known, since it depends on the data.
- These defaults are mirrored as in-class initializers in the C++
  `HierarchicalWardConfig` struct in `ClusterEngine.hpp` so the Python and
  C++ runtimes never drift apart.

## Verification

```bash
python3 _verify_defaults.py
```

All three hierarchical defaults are confirmed: `ward`, `euclidean`, `3`.

## Environment Notes

This documentation lives at `docs/clustering_defaults.md` in the workspace.
A copy is also attached to task `t_b81e857f` in the Kanban board for
historical reference.
