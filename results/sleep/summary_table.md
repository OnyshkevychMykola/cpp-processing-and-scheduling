| scenario | approach | jitter_p99_ms | deadline_misses | underrun_pct | proc_avg_ms | pub_late_avg_ms | data_age_avg_ms | skipped_ticks | jitter_max_ms | data_age_max_ms |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | A | 0.371 | 42 | 0 | 3.23 | 3.49 | 4.79 | 41 | 0.716 | 6.32 |
| baseline | B | 0.381 | 0 | 0.08 | 3.27 | 0.289 | 6.14 | 0 | 0.705 | 8.82 |
| delayed | A | 0.364 | 28 | 43.5 | 3.28 | 2.05 | 13.2 | 28 | 0.601 | 96.6 |
| delayed | B | 0.285 | 0 | 42.4 | 3.21 | 0.208 | 15.3 | 0 | 0.663 | 96.2 |
| load-ramp | A | 0.326 | 724 | 0 | 8.71 | 8.99 | 10.3 | 1.57e+03 | 0.606 | 22.6 |
| load-ramp | B | 0.331 | 0 | 54.5 | 8.18 | 0.266 | 18.2 | 0 | 0.682 | 42.3 |
