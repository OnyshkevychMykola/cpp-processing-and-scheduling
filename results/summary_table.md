| scenario | approach | jitter_p99_ms | deadline_misses | underrun_pct | proc_avg_ms | pub_late_avg_ms | data_age_avg_ms | skipped_ticks | jitter_max_ms | data_age_max_ms |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | A | 0.304 | 0 | 0 | 3.21 | 3.48 | 4.64 | 0 | 0.355 | 5.78 |
| baseline | B | 0.254 | 0 | 0.2 | 3.22 | 0.218 | 6.03 | 0 | 0.291 | 8.44 |
| delayed | A | 0.316 | 0 | 46.4 | 3.26 | 1.93 | 15.5 | 0 | 0.458 | 72.2 |
| delayed | B | 0.287 | 0 | 45.6 | 3.22 | 0.213 | 18 | 0 | 0.411 | 76.1 |
| load-ramp | A | 0.319 | 146 | 0.532 | 8.74 | 8.99 | 10.1 | 312 | 0.349 | 22.6 |
| load-ramp | B | 0.257 | 0 | 54.2 | 8.09 | 0.255 | 17.9 | 0 | 0.325 | 40.3 |
