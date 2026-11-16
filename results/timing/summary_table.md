| config | start_late_avg_ms | jitter_p95_ms | jitter_p99_ms | jitter_max_ms | deadline_misses | rt_applied | pin_applied |
|---|---|---|---|---|---|---|---|
| idle | 0.227 | 0.164 | 0.254 | 0.738 | 0 | 0 | 0 |
| stress | 0.243 | 2.4 | 3.46 | 3.89 | 0 | 0 | 0 |
| stress+hybrid | 0.321 | 2.83 | 3.47 | 3.96 | 0 | 0 | 0 |
| stress+pin | 0.134 | 0.401 | 1.65 | 3.81 | 0 | 0 | 1 |
| stress+pin+hybrid | 0.00693 | 0.000108 | 0.212 | 3.06 | 0 | 0 | 1 |
| stress+rt | 0.062 | 0.651 | 2.02 | 3.82 | 0 | 1 | 0 |
| stress+rt+pin+hybrid | 0.0103 | 0.00439 | 0.496 | 3.68 | 0 | 1 | 1 |
